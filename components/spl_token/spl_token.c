#include "spl_token.h"
#include "base58.h"
#include "tweetnacl.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "spl_token";

// Token Program ID: TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA
const uint8_t SPL_TOKEN_PROGRAM_ID[32] = {
    0x06, 0xdd, 0xf6, 0xe1, 0xd7, 0x65, 0xa1, 0x93,
    0xd9, 0xcb, 0xe1, 0x46, 0xce, 0xeb, 0x79, 0xac,
    0x1c, 0xb4, 0x85, 0xed, 0x5f, 0x5b, 0x37, 0x91,
    0x3a, 0x8c, 0xf5, 0x85, 0x7e, 0xff, 0x00, 0xa9
};

// Associated Token Program ID: ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL
const uint8_t SPL_ASSOCIATED_TOKEN_PROGRAM_ID[32] = {
    0x8c, 0x97, 0x25, 0x8f, 0x4e, 0x24, 0x89, 0xf1,
    0xbb, 0x3d, 0x10, 0x29, 0x14, 0x8e, 0x0d, 0x83,
    0x0b, 0x5a, 0x13, 0x99, 0xda, 0xff, 0x10, 0x84,
    0x04, 0x8e, 0x7b, 0xd8, 0xdb, 0xe9, 0xf8, 0x59
};

// System Program ID: 11111111111111111111111111111111
const uint8_t SYSTEM_PROGRAM_ID[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// USDC Devnet Mint: 4zMMC9srt5Ri5X14GAgXhaHii3GnPAEERYPJgZJDncDU  
// From Kora demo kora.toml - Official Circle USDC on devnet
// Verified against: https://explorer.solana.com/address/4zMMC9srt5Ri5X14GAgXhaHii3GnPAEERYPJgZJDncDU?cluster=devnet
const uint8_t USDC_DEVNET_MINT[32] = {
    0x3b, 0x44, 0x2c, 0xb3, 0x91, 0x21, 0x57, 0xf1, 
    0x3a, 0x93, 0x3d, 0x01, 0x34, 0x28, 0x2d, 0x03, 
    0x2b, 0x5f, 0xfe, 0xcd, 0x01, 0xa2, 0xdb, 0xf1, 
    0xb7, 0x79, 0x06, 0x08, 0xdf, 0x00, 0x2e, 0xa7
};


/**
 * @brief SHA256 hash implementation using mbedtls
 */
static void sha256(const uint8_t *data, size_t len, uint8_t *hash_out) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, hash_out);
    mbedtls_sha256_free(&ctx);
}

/**
 * @brief TweetNaCl's unpackneg function for Ed25519 curve checking
 * 
 * This function unpacks a compressed Ed25519 point and validates if it's on the curve.
 * Returns 0 if on curve (valid point), -1 if off curve (invalid point).
 * Now exposed in tweetnacl.h
 */

/**
 * @brief Check if a 32-byte value is a valid Ed25519 curve point
 * 
 * A valid PDA must NOT be on the Ed25519 curve. This function checks
 * if the bytes represent a point on the curve using TweetNaCl's internal
 * curve validation function.
 * 
 * @param bytes The 32-byte value to check
 * @return true if on curve (invalid PDA), false if off curve (valid PDA)
 */
static bool is_on_curve(const uint8_t *bytes) {
    // Use TweetNaCl's unpackneg function to check if point is on curve
    // unpackneg returns:
    //   0 = point is on curve (valid Ed25519 point) - NOT a valid PDA
    //  -1 = point is off curve (invalid Ed25519 point) - this IS a valid PDA
    
    gf r[4];  // Temporary storage for unpacked point
    int result = unpackneg(r, bytes);
    
    // result == 0 means on curve (invalid PDA)
    // result == -1 means off curve (valid PDA)
    return (result == 0);
}

/**
 * @brief Find Program Derived Address (PDA)
 * 
 * This is Solana's PDA algorithm for deriving Associated Token Accounts.
 * A valid PDA must be OFF the Ed25519 curve.
 */
static esp_err_t find_program_address(
    const uint8_t **seeds,
    const size_t *seed_lens,
    size_t num_seeds,
    const uint8_t *program_id,
    uint8_t *pda_out,
    uint8_t *bump_out
) {
    // PDA derivation: hash(seeds + [bump] + program_id + "ProgramDerivedAddress")
    // Try bump values from 255 down to 0 until we find one that's OFF the curve
    
    for (int bump = 255; bump >= 0; bump--) {
        // Build buffer: seeds + bump + program_id + "ProgramDerivedAddress"
        uint8_t buffer[1024];
        size_t offset = 0;
        
        // Add all seeds
        for (size_t i = 0; i < num_seeds; i++) {
            if (offset + seed_lens[i] > sizeof(buffer)) {
                return ESP_ERR_NO_MEM;
            }
            memcpy(buffer + offset, seeds[i], seed_lens[i]);
            offset += seed_lens[i];
        }
        
        // Add bump
        buffer[offset++] = (uint8_t)bump;
        
        // Add program ID
        memcpy(buffer + offset, program_id, 32);
        offset += 32;
        
        // Add "ProgramDerivedAddress" marker
        const char *marker = "ProgramDerivedAddress";
        size_t marker_len = strlen(marker);
        memcpy(buffer + offset, marker, marker_len);
        offset += marker_len;
        
        // Hash
        uint8_t hash[32];
        sha256(buffer, offset, hash);
        
        // Check if this hash is OFF the Ed25519 curve (valid PDA)
        if (!is_on_curve(hash)) {
            // Found a valid PDA (off curve)!
            memcpy(pda_out, hash, 32);
            if (bump_out) *bump_out = (uint8_t)bump;
            
            ESP_LOGD(TAG, "Found PDA at bump %d", bump);
            return ESP_OK;
        }
        
        // This bump produced an on-curve point, try next bump
        ESP_LOGV(TAG, "Bump %d is on curve, trying next", bump);
    }
    
    // Exhausted all bumps without finding valid PDA (extremely unlikely)
    ESP_LOGE(TAG, "Failed to find valid PDA after trying all bumps");
    return ESP_FAIL;
}

/**
 * @brief HTTP event handler for RPC response
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        // Append response data
        char **response_buffer = (char **)evt->user_data;
        if (*response_buffer == NULL) {
            *response_buffer = malloc(evt->data_len + 1);
            if (*response_buffer) {
                memcpy(*response_buffer, evt->data, evt->data_len);
                (*response_buffer)[evt->data_len] = '\0';
            }
        } else {
            size_t old_len = strlen(*response_buffer);
            char *new_buffer = realloc(*response_buffer, old_len + evt->data_len + 1);
            if (new_buffer) {
                *response_buffer = new_buffer;
                memcpy(*response_buffer + old_len, evt->data, evt->data_len);
                (*response_buffer)[old_len + evt->data_len] = '\0';
            }
        }
    }
    return ESP_OK;
}

esp_err_t spl_token_get_mint_program(
    const char *rpc_url,
    const uint8_t *mint_pubkey,
    uint8_t *program_id_out
) {
    esp_err_t err;
    char *response_buffer = NULL;
    
    // Convert mint pubkey to base58
    char mint_b58[64];
    if (!base58_encode(mint_pubkey, 32, mint_b58, sizeof(mint_b58))) {
        ESP_LOGE(TAG, "Failed to encode mint to base58");
        return ESP_FAIL;
    }
    
    // Build RPC request
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", 1);
    cJSON_AddStringToObject(root, "method", "getAccountInfo");
    
    cJSON *params = cJSON_CreateArray();
    cJSON_AddItemToArray(params, cJSON_CreateString(mint_b58));
    
    cJSON *config = cJSON_CreateObject();
    cJSON_AddStringToObject(config, "encoding", "jsonParsed");
    cJSON_AddItemToArray(params, config);
    
    cJSON_AddItemToObject(root, "params", params);
    
    char *request_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!request_body) {
        ESP_LOGE(TAG, "Failed to create RPC request");
        return ESP_ERR_NO_MEM;
    }
    
    // Make HTTP request
    esp_http_client_config_t config_http = {
        .url = rpc_url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &response_buffer,
        .timeout_ms = 10000,
        .skip_cert_common_name_check = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config_http);
    if (!client) {
        free(request_body);
        return ESP_FAIL;
    }
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, request_body, strlen(request_body));
    
    err = esp_http_client_perform(client);
    free(request_body);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        if (response_buffer) free(response_buffer);
        return err;
    }
    
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    
    if (status_code != 200 || !response_buffer) {
        ESP_LOGE(TAG, "RPC request failed with status %d", status_code);
        if (response_buffer) free(response_buffer);
        return ESP_FAIL;
    }
    
    // Parse response
    cJSON *response = cJSON_Parse(response_buffer);
    free(response_buffer);
    
    if (!response) {
        ESP_LOGE(TAG, "Failed to parse RPC response");
        return ESP_FAIL;
    }
    
    // Extract owner field: result.value.owner
    cJSON *result = cJSON_GetObjectItem(response, "result");
    cJSON *value = result ? cJSON_GetObjectItem(result, "value") : NULL;
    cJSON *owner = value ? cJSON_GetObjectItem(value, "owner") : NULL;
    
    if (!owner || !cJSON_IsString(owner)) {
        ESP_LOGE(TAG, "Missing or invalid 'owner' field in RPC response");
        cJSON_Delete(response);
        return ESP_FAIL;
    }
    
    // Decode owner from base58
    const char *owner_b58 = owner->valuestring;
    size_t decoded_len;
    if (!base58_decode(owner_b58, program_id_out, &decoded_len, 32)) {
        ESP_LOGE(TAG, "Failed to decode token program ID from base58");
        cJSON_Delete(response);
        return ESP_FAIL;
    }
    
    if (decoded_len != 32) {
        ESP_LOGE(TAG, "Invalid token program ID length: %zu", decoded_len);
        cJSON_Delete(response);
        return ESP_FAIL;
    }
    
    char program_b58[64];
    base58_encode(program_id_out, 32, program_b58, sizeof(program_b58));
    ESP_LOGI(TAG, "Mint %s is owned by program %s", mint_b58, program_b58);
    
    cJSON_Delete(response);
    return ESP_OK;
}

esp_err_t spl_token_get_associated_token_address_with_program(
    const uint8_t *wallet_pubkey,
    const uint8_t *mint_pubkey,
    const uint8_t *token_program_id,
    uint8_t *ata_out
) {
    // Derive PDA: [wallet, token_program, mint]
    const uint8_t *seeds[] = {
        wallet_pubkey,
        token_program_id,
        mint_pubkey
    };
    const size_t seed_lens[] = {32, 32, 32};
    
    uint8_t bump;
    return find_program_address(
        seeds, seed_lens, 3,
        SPL_ASSOCIATED_TOKEN_PROGRAM_ID,
        ata_out, &bump
    );
}

esp_err_t spl_token_get_associated_token_address(
    const uint8_t *wallet_pubkey,
    const uint8_t *mint_pubkey,
    uint8_t *ata_out
) {
    // Use standard Token Program by default
    return spl_token_get_associated_token_address_with_program(
        wallet_pubkey,
        mint_pubkey,
        SPL_TOKEN_PROGRAM_ID,
        ata_out
    );
}

esp_err_t spl_token_build_transfer_instruction(
    const uint8_t *source_ata,
    const uint8_t *dest_ata,
    const uint8_t *owner,
    uint64_t amount,
    uint8_t *instruction_out,
    size_t *instruction_len,
    size_t max_instruction_len
) {
    if (!source_ata || !dest_ata || !owner || !instruction_out || !instruction_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // SPL Token Transfer instruction format:
    // [1 byte: instruction type (3 = Transfer)]
    // [8 bytes: amount (little-endian u64)]
    
    size_t needed = 1 + 8; // instruction + amount
    if (max_instruction_len < needed) {
        return ESP_ERR_NO_MEM;
    }
    
    size_t offset = 0;
    
    // Instruction type: Transfer = 3
    instruction_out[offset++] = SPL_TOKEN_TRANSFER_INSTRUCTION;
    
    // Amount (little-endian)
    instruction_out[offset++] = (amount >> 0) & 0xFF;
    instruction_out[offset++] = (amount >> 8) & 0xFF;
    instruction_out[offset++] = (amount >> 16) & 0xFF;
    instruction_out[offset++] = (amount >> 24) & 0xFF;
    instruction_out[offset++] = (amount >> 32) & 0xFF;
    instruction_out[offset++] = (amount >> 40) & 0xFF;
    instruction_out[offset++] = (amount >> 48) & 0xFF;
    instruction_out[offset++] = (amount >> 56) & 0xFF;
    
    *instruction_len = offset;
    
    ESP_LOGD(TAG, "Built SPL transfer instruction: %llu tokens", amount);
    
    return ESP_OK;
}

esp_err_t spl_token_create_transfer_transaction(
    const uint8_t *fee_payer,
    const uint8_t *from_wallet,
    const uint8_t *to_wallet,
    const uint8_t *mint,
    const uint8_t *token_program_id,
    uint64_t amount,
    const uint8_t *recent_blockhash,
    uint8_t *tx_out,
    size_t *tx_len,
    size_t max_tx_len
) {
    if (!fee_payer || !from_wallet || !to_wallet || !mint || !token_program_id || !recent_blockhash || !tx_out || !tx_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err;
    
    // Step 1: Derive ATAs using the correct token program
    uint8_t source_ata[32];
    uint8_t dest_ata[32];
    
    err = spl_token_get_associated_token_address_with_program(from_wallet, mint, token_program_id, source_ata);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to derive source ATA");
        return err;
    }
    
    err = spl_token_get_associated_token_address_with_program(to_wallet, mint, token_program_id, dest_ata);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to derive dest ATA");
        return err;
    }
    
    // Step 2: Build instruction data
    uint8_t instruction_data[32];
    size_t instruction_len;
    
    err = spl_token_build_transfer_instruction(
        source_ata, dest_ata, from_wallet,
        amount, instruction_data, &instruction_len, sizeof(instruction_data)
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build instruction");
        return err;
    }
    
    // Step 3: Build transaction
    // Solana transaction format (multi-sig for Kora):
    // - Compact array of signatures (2: user + fee payer)
    // - Message:
    //   - Header (3 bytes)
    //   - Compact array of account keys
    //   - Recent blockhash (32 bytes)
    //   - Compact array of instructions
    
    size_t offset = 0;
    
    // Number of signatures (2: user signature + fee payer signature)
    // Kora/facilitator will add the second signature
    if (offset + 1 > max_tx_len) return ESP_ERR_NO_MEM;
    tx_out[offset++] = 2; // Will have 2 signatures
    
    // Placeholder for user signature (64 bytes of zeros, we'll sign later)
    if (offset + 64 > max_tx_len) return ESP_ERR_NO_MEM;
    memset(tx_out + offset, 0, 64);
    offset += 64;
    
    // Placeholder for fee payer signature (64 bytes of zeros, Kora fills this)
    if (offset + 64 > max_tx_len) return ESP_ERR_NO_MEM;
    memset(tx_out + offset, 0, 64);
    offset += 64;
    
    // Message header
    // Solana account ordering rules:
    // 1. Signer accounts (writable first, then readonly)
    // 2. Non-signer writable accounts
    // 3. Non-signer readonly accounts (programs)
    if (offset + 3 > max_tx_len) return ESP_ERR_NO_MEM;
    tx_out[offset++] = 2; // num_required_signatures (fee_payer + from_wallet)
    tx_out[offset++] = 1; // num_readonly_signed_accounts (from_wallet is readonly)
    tx_out[offset++] = 1; // num_readonly_unsigned_accounts (token_program)
    
    // Account keys (compact array)
    // Order: [fee_payer(signer,writable), from_wallet(signer,readonly), source_ata(writable), dest_ata(writable), token_program(readonly)]
    if (offset + 1 > max_tx_len) return ESP_ERR_NO_MEM;
    tx_out[offset++] = 5; // 5 accounts
    
    // Account 0: fee_payer (signer, writable) - pays transaction fees
    if (offset + 32 > max_tx_len) return ESP_ERR_NO_MEM;
    memcpy(tx_out + offset, fee_payer, 32);
    offset += 32;
    
    // Account 1: from_wallet (signer, readonly) - authorizes token transfer
    if (offset + 32 > max_tx_len) return ESP_ERR_NO_MEM;
    memcpy(tx_out + offset, from_wallet, 32);
    offset += 32;
    
    // Account 2: source_ata (writable)
    if (offset + 32 > max_tx_len) return ESP_ERR_NO_MEM;
    memcpy(tx_out + offset, source_ata, 32);
    offset += 32;
    
    // Account 3: dest_ata (writable)
    if (offset + 32 > max_tx_len) return ESP_ERR_NO_MEM;
    memcpy(tx_out + offset, dest_ata, 32);
    offset += 32;
    
    // Account 4: token_program (readonly)
    if (offset + 32 > max_tx_len) return ESP_ERR_NO_MEM;
    memcpy(tx_out + offset, token_program_id, 32);
    offset += 32;
    
    // Recent blockhash
    if (offset + 32 > max_tx_len) return ESP_ERR_NO_MEM;
    memcpy(tx_out + offset, recent_blockhash, 32);
    offset += 32;
    
    // Instructions (compact array)
    if (offset + 1 > max_tx_len) return ESP_ERR_NO_MEM;
    tx_out[offset++] = 1; // 1 instruction
    
    // Instruction:
    // - Program ID index (u8)
    // - Compact array of account indices
    // - Compact array of instruction data
    
    // Program ID index (4 = token_program, was 3 before fee_payer added)
    if (offset + 1 > max_tx_len) return ESP_ERR_NO_MEM;
    tx_out[offset++] = 4;
    
    // Account indices for SPL Token Transfer instruction
    // Old: [source_ata(1), dest_ata(2), owner(0)]
    // New: [source_ata(2), dest_ata(3), owner(1)] - all indices +1 due to fee_payer
    if (offset + 1 > max_tx_len) return ESP_ERR_NO_MEM;
    tx_out[offset++] = 3; // 3 accounts
    tx_out[offset++] = 2; // source_ata index (was 1)
    tx_out[offset++] = 3; // dest_ata index (was 2)
    tx_out[offset++] = 1; // owner index (was 0)
    
    // Instruction data
    if (offset + 1 + instruction_len > max_tx_len) return ESP_ERR_NO_MEM;
    tx_out[offset++] = instruction_len;
    memcpy(tx_out + offset, instruction_data, instruction_len);
    offset += instruction_len;
    
    *tx_len = offset;
    
    ESP_LOGI(TAG, "Created SPL transfer transaction: %zu bytes", offset);
    
    return ESP_OK;
}

esp_err_t spl_token_parse_usd_amount(
    const char *amount_str,
    uint8_t decimals,
    uint64_t *amount_out
) {
    if (!amount_str || !amount_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Skip '$' if present
    const char *str = amount_str;
    if (*str == '$') str++;
    
    // Parse as float
    double value = atof(str);
    
    // Convert to integer with decimals
    // e.g., $0.10 with 6 decimals = 0.10 * 10^6 = 100000
    uint64_t multiplier = 1;
    for (uint8_t i = 0; i < decimals; i++) {
        multiplier *= 10;
    }
    
    *amount_out = (uint64_t)(value * multiplier);
    
    ESP_LOGD(TAG, "Parsed USD amount: %s -> %llu (with %d decimals)", 
             amount_str, *amount_out, decimals);
    
    return ESP_OK;
}

esp_err_t spl_token_check_ata_exists(
    void *rpc_client,
    const uint8_t *wallet_pubkey,
    const uint8_t *mint_pubkey,
    bool *exists_out
) {
    if (!rpc_client || !wallet_pubkey || !mint_pubkey || !exists_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // TODO: Implement RPC query to check account existence
    // Full implementation would:
    // 1. Derive ATA address
    // 2. Call getAccountInfo RPC method
    // 3. Check if account exists and is initialized
    // 4. Return true/false
    
    // For now, assume ATAs exist to avoid breaking payments
    // Pre-fund recipient accounts with USDC for testing
    *exists_out = true;
    
    ESP_LOGW(TAG, "ATA existence check not yet implemented - assuming ATA exists");
    ESP_LOGW(TAG, "Make sure recipient has USDC account initialized!");
    
    return ESP_OK;
}

esp_err_t spl_token_build_create_ata_instruction(
    const uint8_t *payer,
    const uint8_t *wallet,
    const uint8_t *mint,
    uint8_t *instruction_out,
    size_t *instruction_len,
    size_t max_len
) {
    if (!payer || !wallet || !mint || !instruction_out || !instruction_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // TODO: Implement ATA creation instruction
    // Full implementation would:
    // 1. Build accounts list: [payer, ata, wallet, mint, system_program, token_program, ata_program]
    // 2. Set instruction data (empty for ATA program)
    // 3. Return serialized instruction
    
    ESP_LOGE(TAG, "ATA creation not yet implemented");
    return ESP_FAIL;
}


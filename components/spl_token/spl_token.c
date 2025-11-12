#include "spl_token.h"
#include "base58.h"
#include "tweetnacl.h"
#include "esp_log.h"
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
    0x36, 0xc9, 0x1e, 0x90, 0xde, 0x9e, 0x7c, 0xd0,
    0x5f, 0xe0, 0x77, 0xe4, 0x35, 0xd1, 0x96, 0xe1,
    0xd9, 0x7d, 0x55, 0x34, 0x5e, 0x6b, 0x08, 0x25,
    0xf8, 0x86, 0xf9, 0xc9, 0x60, 0x23, 0x57, 0x69
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
 * @brief Find Program Derived Address (PDA)
 * 
 * This is Solana's PDA algorithm for deriving Associated Token Accounts
 */
static esp_err_t find_program_address(
    const uint8_t **seeds,
    const size_t *seed_lens,
    size_t num_seeds,
    const uint8_t *program_id,
    uint8_t *pda_out,
    uint8_t *bump_out
) {
    // PDA derivation: hash(seeds + [bump] + program_id)
    // Try bump values from 255 down to 0
    
    for (uint8_t bump = 255; ; bump--) {
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
        buffer[offset++] = bump;
        
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
        
        // Check if this is a valid PDA (not on curve)
        // For simplicity, we'll accept the first hash
        // In real implementation, check if point is on Ed25519 curve
        
        memcpy(pda_out, hash, 32);
        if (bump_out) *bump_out = bump;
        
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

esp_err_t spl_token_get_associated_token_address(
    const uint8_t *wallet_pubkey,
    const uint8_t *mint_pubkey,
    uint8_t *ata_out
) {
    if (!wallet_pubkey || !mint_pubkey || !ata_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // ATA is derived from: [wallet, token_program, mint]
    const uint8_t *seeds[] = {
        wallet_pubkey,
        SPL_TOKEN_PROGRAM_ID,
        mint_pubkey
    };
    const size_t seed_lens[] = {32, 32, 32};
    
    uint8_t bump;
    esp_err_t err = find_program_address(
        seeds, seed_lens, 3,
        SPL_ASSOCIATED_TOKEN_PROGRAM_ID,
        ata_out, &bump
    );
    
    if (err == ESP_OK) {
        char ata_b58[64];
        if (base58_encode(ata_out, 32, ata_b58, sizeof(ata_b58))) {
            ESP_LOGD(TAG, "Derived ATA: %s (bump: %d)", ata_b58, bump);
        }
    }
    
    return err;
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
    const uint8_t *from_wallet,
    const uint8_t *to_wallet,
    const uint8_t *mint,
    uint64_t amount,
    const uint8_t *recent_blockhash,
    uint8_t *tx_out,
    size_t *tx_len,
    size_t max_tx_len
) {
    if (!from_wallet || !to_wallet || !mint || !recent_blockhash || !tx_out || !tx_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err;
    
    // Step 1: Derive ATAs
    uint8_t source_ata[32];
    uint8_t dest_ata[32];
    
    err = spl_token_get_associated_token_address(from_wallet, mint, source_ata);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to derive source ATA");
        return err;
    }
    
    err = spl_token_get_associated_token_address(to_wallet, mint, dest_ata);
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
    if (offset + 3 > max_tx_len) return ESP_ERR_NO_MEM;
    tx_out[offset++] = 2; // num_required_signatures (user + fee payer)
    tx_out[offset++] = 0; // num_readonly_signed_accounts  
    tx_out[offset++] = 1; // num_readonly_unsigned_accounts
    
    // Account keys (compact array)
    // Accounts: [from_wallet(signer), source_ata(writable), dest_ata(writable), token_program]
    if (offset + 1 > max_tx_len) return ESP_ERR_NO_MEM;
    tx_out[offset++] = 4; // 4 accounts
    
    // Account 0: from_wallet (signer)
    if (offset + 32 > max_tx_len) return ESP_ERR_NO_MEM;
    memcpy(tx_out + offset, from_wallet, 32);
    offset += 32;
    
    // Account 1: source_ata (writable)
    if (offset + 32 > max_tx_len) return ESP_ERR_NO_MEM;
    memcpy(tx_out + offset, source_ata, 32);
    offset += 32;
    
    // Account 2: dest_ata (writable)
    if (offset + 32 > max_tx_len) return ESP_ERR_NO_MEM;
    memcpy(tx_out + offset, dest_ata, 32);
    offset += 32;
    
    // Account 3: token_program (readonly)
    if (offset + 32 > max_tx_len) return ESP_ERR_NO_MEM;
    memcpy(tx_out + offset, SPL_TOKEN_PROGRAM_ID, 32);
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
    
    // Program ID index (3 = token_program)
    if (offset + 1 > max_tx_len) return ESP_ERR_NO_MEM;
    tx_out[offset++] = 3;
    
    // Account indices for instruction: [source_ata(1), dest_ata(2), owner(0)]
    if (offset + 1 > max_tx_len) return ESP_ERR_NO_MEM;
    tx_out[offset++] = 3; // 3 accounts
    tx_out[offset++] = 1; // source_ata index
    tx_out[offset++] = 2; // dest_ata index
    tx_out[offset++] = 0; // owner index
    
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


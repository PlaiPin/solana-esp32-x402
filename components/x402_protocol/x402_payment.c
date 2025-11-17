#include "x402_payment.h"
#include "x402_requirements.h"
#include "x402_encoding.h"
#include "spl_token.h"
#include "base58.h"
#include "solana_rpc.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "x402_payment";

// Global RPC client for blockhash queries
static solana_rpc_handle_t g_rpc_client = NULL;

/**
 * @brief Initialize RPC client for payment creation
 */
static esp_err_t ensure_rpc_client(void) {
    if (g_rpc_client) {
        return ESP_OK;
    }
    
    g_rpc_client = solana_rpc_init("https://api.devnet.solana.com");
    if (!g_rpc_client) {
        ESP_LOGE(TAG, "Failed to initialize RPC client");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t x402_build_payment_transaction(
    solana_wallet_t *wallet,
    const uint8_t *fee_payer_pubkey,
    const uint8_t *recipient_pubkey,
    const uint8_t *mint_pubkey,
    const uint8_t *token_program_id,
    uint64_t amount,
    uint8_t *tx_out,
    size_t *tx_len,
    size_t max_tx_len
) {
    if (!wallet || !fee_payer_pubkey || !recipient_pubkey || !mint_pubkey || !token_program_id || !tx_out || !tx_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err;
    
    // Get wallet pubkey
    uint8_t wallet_pubkey[32];
    err = solana_wallet_get_pubkey(wallet, wallet_pubkey);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get wallet public key");
        return err;
    }
    
    // Get recent blockhash
    err = ensure_rpc_client();
    if (err != ESP_OK) {
        return err;
    }
    
    solana_rpc_response_t rpc_response;
    err = solana_rpc_get_latest_blockhash(g_rpc_client, &rpc_response);
    if (err != ESP_OK || !rpc_response.success) {
        ESP_LOGE(TAG, "Failed to get recent blockhash");
        if (err == ESP_OK) {
            solana_rpc_free_response(&rpc_response);
        }
        return ESP_FAIL;
    }
    
    // Parse blockhash from JSON response
    cJSON *root = cJSON_Parse(rpc_response.data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse blockhash response");
        solana_rpc_free_response(&rpc_response);
        return ESP_FAIL;
    }
    
    cJSON *result = cJSON_GetObjectItem(root, "result");
    cJSON *value = cJSON_GetObjectItem(result, "value");
    cJSON *blockhash_json = cJSON_GetObjectItem(value, "blockhash");
    
    if (!cJSON_IsString(blockhash_json)) {
        ESP_LOGE(TAG, "Invalid blockhash in response");
        cJSON_Delete(root);
        solana_rpc_free_response(&rpc_response);
        return ESP_FAIL;
    }
    
    // Decode blockhash from Base58
    uint8_t blockhash[32];
    size_t blockhash_len;
    if (!base58_decode(blockhash_json->valuestring, blockhash, &blockhash_len, sizeof(blockhash)) || 
        blockhash_len != 32) {
        ESP_LOGE(TAG, "Failed to decode blockhash");
        cJSON_Delete(root);
        solana_rpc_free_response(&rpc_response);
        return ESP_FAIL;
    }
    
    cJSON_Delete(root);
    solana_rpc_free_response(&rpc_response);
    
    ESP_LOGI(TAG, "Building SPL token transfer transaction...");
    
    // Build SPL token transfer transaction with fee payer
    err = spl_token_create_transfer_transaction(
        fee_payer_pubkey,
        wallet_pubkey,
        recipient_pubkey,
        mint_pubkey,
        token_program_id,
        amount,
        blockhash,
        tx_out,
        tx_len,
        max_tx_len
    );
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build SPL transfer transaction");
        return err;
    }
    
    ESP_LOGI(TAG, "Transaction built: %zu bytes", *tx_len);
    
    return ESP_OK;
}

esp_err_t x402_create_solana_payment(
    solana_wallet_t *wallet,
    const x402_payment_requirements_t *requirements,
    x402_payment_payload_t *payload_out
) {
    if (!wallet || !requirements || !payload_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!requirements->valid) {
        ESP_LOGE(TAG, "Invalid payment requirements");
        return ESP_ERR_INVALID_STATE;
    }
    
    memset(payload_out, 0, sizeof(x402_payment_payload_t));
    
    esp_err_t err;
    
    // Step 1: Parse recipient address (Base58 -> bytes)
    uint8_t recipient_pubkey[32];
    size_t decoded_len;
    if (!base58_decode(
        requirements->recipient,
        recipient_pubkey,
        &decoded_len,
        sizeof(recipient_pubkey)
    )) {
        ESP_LOGE(TAG, "Failed to decode recipient address");
        return ESP_FAIL;
    }
    
    if (decoded_len != 32) {
        ESP_LOGE(TAG, "Invalid recipient address length: %zu", decoded_len);
        return ESP_FAIL;
    }
    
    // Step 2: Decode mint pubkey from requirements.asset
    uint8_t mint_pubkey[32];
    size_t mint_len;
    if (!base58_decode(
        requirements->asset,
        mint_pubkey,
        &mint_len,
        sizeof(mint_pubkey)
    ) || mint_len != 32) {
        ESP_LOGE(TAG, "Failed to decode asset/mint address");
        return ESP_FAIL;
    }
    
    // Step 3: Parse amount (already in base units/lamports from API)
    // The maxAmountRequired field is sent as base units (e.g., "100" = 100 lamports = 0.0001 USDC)
    uint64_t amount = 0;
    char *endptr;
    amount = strtoull(requirements->price.amount, &endptr, 10);
    
    if (endptr == requirements->price.amount || *endptr != '\0') {
        ESP_LOGE(TAG, "Failed to parse amount as integer: %s", requirements->price.amount);
        return ESP_FAIL;
    }
    
    if (amount == 0) {
        ESP_LOGE(TAG, "Invalid amount: %s (parsed as 0)", requirements->price.amount);
        return ESP_FAIL;
    }
    
    // Calculate USD equivalent for logging (USDC has 6 decimals)
    double amount_usdc = (double)amount / 1000000.0;
    ESP_LOGI(TAG, "Creating payment: %llu lamports (%.6f USDC) to %s",
             amount, amount_usdc, requirements->recipient);
    
    // Step 4: Use fee payer from requirements (already parsed from extra.feePayer)
    if (!requirements->facilitator.fee_payer[0]) {
        ESP_LOGE(TAG, "No fee payer provided in payment requirements");
        return ESP_FAIL;
    }
    
    // Decode fee payer address
    uint8_t fee_payer_pubkey[32];
    size_t fee_payer_len;
    if (!base58_decode(
        requirements->facilitator.fee_payer,
        fee_payer_pubkey,
        &fee_payer_len,
        sizeof(fee_payer_pubkey)
    ) || fee_payer_len != 32) {
        ESP_LOGE(TAG, "Failed to decode fee payer address");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Fee payer: %s", requirements->facilitator.fee_payer);
    
    // Step 5: Get token program ID for the mint (Token or Token-2022)
    uint8_t token_program_id[32];
    err = spl_token_get_mint_program(
        "https://api.devnet.solana.com",
        mint_pubkey,
        token_program_id
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get token program for mint");
        return err;
    }
    
    // Step 6: Build transaction with fee payer
    uint8_t tx_data[2048];
    size_t tx_len;
    
    err = x402_build_payment_transaction(
        wallet,
        fee_payer_pubkey,
        recipient_pubkey,
        mint_pubkey,
        token_program_id,
        amount,
        tx_data,
        &tx_len,
        sizeof(tx_data)
    );
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build payment transaction");
        return err;
    }
    
    // Step 7: Sign transaction
    // The transaction has 2 signature placeholders:
    // [1 byte sig count][64 bytes fee_payer sig][64 bytes user sig][message]
    // We sign the message and put our signature in the SECOND slot
    // Kora/facilitator will add fee payer signature in the FIRST slot
    
    // Extract message (skip sig count + 2 sig placeholders)
    const uint8_t *message = tx_data + 1 + 64 + 64; // After both sig slots
    size_t message_len = tx_len - 1 - 64 - 64;
    
    uint8_t signature[64];
    err = solana_wallet_sign(wallet, message, message_len, signature);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to sign transaction");
        return err;
    }
    
    // Put our signature in SECOND slot (user signature, account[1])
    memcpy(tx_data + 1 + 64, signature, 64); // After fee_payer slot
    // First slot (fee payer signature, account[0]) remains zeros - Kora fills this
    
    ESP_LOGI(TAG, "Transaction signed successfully");
    
    // Step 8: Base64 encode transaction
    char *tx_b64 = malloc(4096);
    if (!tx_b64) {
        ESP_LOGE(TAG, "Failed to allocate base64 buffer");
        return ESP_ERR_NO_MEM;
    }
    
    size_t tx_b64_len;
    err = x402_base64_encode(tx_data, tx_len, tx_b64, 4096, &tx_b64_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to encode transaction");
        free(tx_b64);
        return err;
    }
    
    ESP_LOGD(TAG, "Transaction base64 (first 80 chars): %.80s", tx_b64);
    
    // Step 9: Create PaymentPayload structure (FLAT structure)
    payload_out->x402_version = 1;
    strncpy(payload_out->scheme, X402_SCHEME_EXACT,
            sizeof(payload_out->scheme) - 1);
    payload_out->scheme[sizeof(payload_out->scheme) - 1] = '\0';
    strncpy(payload_out->network, requirements->network,
            sizeof(payload_out->network) - 1);
    payload_out->network[sizeof(payload_out->network) - 1] = '\0';
    payload_out->payload.transaction = tx_b64; // Transfer ownership
    
    ESP_LOGI(TAG, "✓ Payment payload created successfully");
    
    return ESP_OK;
}

void x402_payment_free(x402_payment_payload_t *payload) {
    if (payload && payload->payload.transaction) {
        free(payload->payload.transaction);
        payload->payload.transaction = NULL;
    }
}


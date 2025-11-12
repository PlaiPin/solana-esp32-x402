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
    const uint8_t *recipient_pubkey,
    const uint8_t *mint_pubkey,
    uint64_t amount,
    uint8_t *tx_out,
    size_t *tx_len,
    size_t max_tx_len
) {
    if (!wallet || !recipient_pubkey || !mint_pubkey || !tx_out || !tx_len) {
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
    
    // Build SPL token transfer transaction
    err = spl_token_create_transfer_transaction(
        wallet_pubkey,
        recipient_pubkey,
        mint_pubkey,
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
    
    // Step 2: Get mint pubkey (assume USDC for now)
    const uint8_t *mint_pubkey = USDC_DEVNET_MINT;
    
    // Step 3: Parse amount (handles both USD "$0.0001" and raw amounts "100")
    uint64_t amount;
    err = spl_token_parse_usd_amount(
        requirements->price.amount, 
        USDC_DECIMALS, 
        &amount
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse amount from: %s", requirements->price.amount);
        return err;
    }
    
    ESP_LOGI(TAG, "Creating payment: %llu to %s",
             amount, requirements->recipient);
    
    // Step 4: Build transaction
    uint8_t tx_data[2048];
    size_t tx_len;
    
    err = x402_build_payment_transaction(
        wallet,
        recipient_pubkey,
        mint_pubkey,
        amount,
        tx_data,
        &tx_len,
        sizeof(tx_data)
    );
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build payment transaction");
        return err;
    }
    
    // Step 5: Sign transaction
    // The transaction has 2 signature placeholders:
    // [1 byte sig count][64 bytes user sig][64 bytes fee payer sig][message]
    // We sign the message and put our signature in the first slot
    // Kora/facilitator will add fee payer signature in the second slot
    
    // Extract message (skip sig count + 2 sig placeholders)
    const uint8_t *message = tx_data + 1 + 64 + 64; // After both sig slots
    size_t message_len = tx_len - 1 - 64 - 64;
    
    uint8_t signature[64];
    err = solana_wallet_sign(wallet, message, message_len, signature);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to sign transaction");
        return err;
    }
    
    // Put our signature in first slot (user signature)
    memcpy(tx_data + 1, signature, 64);
    // Second slot (fee payer signature) remains zeros - Kora fills this
    
    ESP_LOGI(TAG, "Transaction signed successfully");
    
    // Step 6: Base64 encode transaction
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
    
    // Step 7: Create PaymentPayload structure
    payload_out->version = 1;
    payload_out->kind.x402_version = 1;
    strncpy(payload_out->kind.scheme, X402_SCHEME_EXACT,
            sizeof(payload_out->kind.scheme) - 1);
    payload_out->kind.scheme[sizeof(payload_out->kind.scheme) - 1] = '\0';
    strncpy(payload_out->kind.network, requirements->network,
            sizeof(payload_out->kind.network) - 1);
    payload_out->kind.network[sizeof(payload_out->kind.network) - 1] = '\0';
    payload_out->kind.payload.transaction = tx_b64; // Transfer ownership
    
    ESP_LOGI(TAG, "✓ Payment payload created successfully");
    
    return ESP_OK;
}

void x402_payment_free(x402_payment_payload_t *payload) {
    if (payload && payload->kind.payload.transaction) {
        free(payload->kind.payload.transaction);
        payload->kind.payload.transaction = NULL;
    }
}


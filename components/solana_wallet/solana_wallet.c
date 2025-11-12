#include "solana_wallet.h"
#include "tweetnacl.h"
#include "base58.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <cJSON.h>

static const char *TAG = "SolanaWallet";

struct solana_wallet_t {
    uint8_t secret_key[64];  // Ed25519 secret key
    uint8_t public_key[32];  // Ed25519 public key
    solana_rpc_handle_t rpc;
};

solana_wallet_t* solana_wallet_from_keypair(const uint8_t *secret_key, 
                                              solana_rpc_handle_t rpc_client) {
    if (!secret_key || !rpc_client) {
        ESP_LOGE(TAG, "Invalid arguments");
        return NULL;
    }
    
    solana_wallet_t *wallet = (solana_wallet_t *)calloc(1, sizeof(solana_wallet_t));
    if (!wallet) {
        ESP_LOGE(TAG, "Failed to allocate wallet");
        return NULL;
    }
    
    // Copy secret key
    memcpy(wallet->secret_key, secret_key, 64);
    
    // Extract public key (last 32 bytes of Ed25519 secret key)
    memcpy(wallet->public_key, secret_key + 32, 32);
    
    wallet->rpc = rpc_client;
    
    char address[64];
    if (base58_encode(wallet->public_key, 32, address, sizeof(address))) {
        ESP_LOGI(TAG, "Wallet created: %s", address);
    }
    
    return wallet;
}

esp_err_t solana_wallet_get_address(solana_wallet_t *wallet, char *address_out, size_t max_len) {
    if (!wallet || !address_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!base58_encode(wallet->public_key, 32, address_out, max_len)) {
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t solana_wallet_get_pubkey(solana_wallet_t *wallet, uint8_t *pubkey_out) {
    if (!wallet || !pubkey_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(pubkey_out, wallet->public_key, 32);
    return ESP_OK;
}

esp_err_t solana_wallet_get_balance(solana_wallet_t *wallet, uint64_t *balance_out) {
    if (!wallet || !balance_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get address
    char address[64];
    esp_err_t err = solana_wallet_get_address(wallet, address, sizeof(address));
    if (err != ESP_OK) {
        return err;
    }
    
    // Query balance
    solana_rpc_response_t response;
    err = solana_rpc_get_balance(wallet->rpc, address, &response);
    if (err != ESP_OK || !response.success) {
        ESP_LOGE(TAG, "Failed to get balance");
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    // Parse JSON response
    cJSON *root = cJSON_Parse(response.data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse balance response");
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    cJSON *result = cJSON_GetObjectItem(root, "result");
    cJSON *value = cJSON_GetObjectItem(result, "value");
    
    if (cJSON_IsNumber(value)) {
        *balance_out = (uint64_t)value->valuedouble;
        ESP_LOGI(TAG, "Balance: %llu lamports (%.9f SOL)", 
                 *balance_out, (double)*balance_out / 1000000000.0);
    } else {
        ESP_LOGE(TAG, "Invalid balance in response");
        cJSON_Delete(root);
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    cJSON_Delete(root);
    solana_rpc_free_response(&response);
    return ESP_OK;
}

esp_err_t solana_wallet_sign(solana_wallet_t *wallet, const uint8_t *message, 
                               size_t message_len, uint8_t *signature_out) {
    if (!wallet || !message || !signature_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // TweetNaCl signing: creates [signature][message]
    uint8_t *signed_msg = (uint8_t *)malloc(64 + message_len);
    if (!signed_msg) {
        return ESP_ERR_NO_MEM;
    }
    
    unsigned long long signed_len;
    int result = crypto_sign(signed_msg, &signed_len, message, message_len, wallet->secret_key);
    
    if (result != 0) {
        ESP_LOGE(TAG, "Signing failed");
        free(signed_msg);
        return ESP_FAIL;
    }
    
    // Extract just the signature (first 64 bytes)
    memcpy(signature_out, signed_msg, 64);
    free(signed_msg);
    
    ESP_LOGI(TAG, "Message signed successfully");
    return ESP_OK;
}

esp_err_t solana_wallet_send_sol(solana_wallet_t *wallet, const char *to_address,
                                   uint64_t lamports, char *signature_out, size_t max_sig_len) {
    if (!wallet || !to_address || !signature_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Sending %llu lamports to %s", lamports, to_address);
    
    // Step 1: Get latest blockhash
    solana_rpc_response_t response;
    esp_err_t err = solana_rpc_get_latest_blockhash(wallet->rpc, &response);
    if (err != ESP_OK || !response.success) {
        ESP_LOGE(TAG, "Failed to get blockhash");
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    // Parse blockhash from response
    cJSON *root = cJSON_Parse(response.data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse blockhash response");
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    cJSON *result = cJSON_GetObjectItem(root, "result");
    cJSON *value = cJSON_GetObjectItem(result, "value");
    cJSON *blockhash_json = cJSON_GetObjectItem(value, "blockhash");
    
    if (!cJSON_IsString(blockhash_json)) {
        ESP_LOGE(TAG, "Invalid blockhash in response");
        cJSON_Delete(root);
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    char *blockhash = cJSON_GetStringValue(blockhash_json);
    ESP_LOGI(TAG, "Latest blockhash: %s", blockhash);
    
    // Step 2: Build transaction
    solana_pubkey_t from_pubkey, to_pubkey;
    memcpy(from_pubkey.data, wallet->public_key, 32);
    
    err = solana_pubkey_from_base58(to_address, &to_pubkey);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Invalid destination address");
        cJSON_Delete(root);
        solana_rpc_free_response(&response);
        return err;
    }
    
    solana_tx_t *tx = solana_tx_new(blockhash, &from_pubkey);
    if (!tx) {
        ESP_LOGE(TAG, "Failed to create transaction");
        cJSON_Delete(root);
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    err = solana_tx_add_transfer(tx, &from_pubkey, &to_pubkey, lamports);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add transfer instruction");
        solana_tx_destroy(tx);
        cJSON_Delete(root);
        solana_rpc_free_response(&response);
        return err;
    }
    
    // Step 3: Sign transaction
    uint8_t message[1024];
    size_t message_len;
    err = solana_tx_get_message(tx, message, &message_len, sizeof(message));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to serialize message");
        solana_tx_destroy(tx);
        cJSON_Delete(root);
        solana_rpc_free_response(&response);
        return err;
    }
    
    uint8_t signature[64];
    err = solana_wallet_sign(wallet, message, message_len, signature);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to sign transaction");
        solana_tx_destroy(tx);
        cJSON_Delete(root);
        solana_rpc_free_response(&response);
        return err;
    }
    
    err = solana_tx_add_signature(tx, signature);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add signature");
        solana_tx_destroy(tx);
        cJSON_Delete(root);
        solana_rpc_free_response(&response);
        return err;
    }
    
    // Step 4: Serialize complete transaction
    uint8_t serialized_tx[2048];
    size_t serialized_len;
    err = solana_tx_serialize(tx, serialized_tx, &serialized_len, sizeof(serialized_tx));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to serialize transaction");
        solana_tx_destroy(tx);
        cJSON_Delete(root);
        solana_rpc_free_response(&response);
        return err;
    }
    
    // Step 5: Encode to Base58
    char tx_base58[3000];
    if (!base58_encode(serialized_tx, serialized_len, tx_base58, sizeof(tx_base58))) {
        ESP_LOGE(TAG, "Failed to encode transaction to Base58");
        solana_tx_destroy(tx);
        cJSON_Delete(root);
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Transaction encoded (%zu bytes -> %d chars)", 
             serialized_len, strlen(tx_base58));
    
    // Step 6: Send transaction
    cJSON_Delete(root);
    solana_rpc_free_response(&response);
    
    err = solana_rpc_send_transaction(wallet->rpc, tx_base58, &response);
    if (err != ESP_OK || !response.success) {
        ESP_LOGE(TAG, "Failed to send transaction");
        if (response.data) {
            ESP_LOGE(TAG, "Response: %s", response.data);
        }
        solana_tx_destroy(tx);
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    // Parse signature from response
    ESP_LOGI(TAG, "Raw RPC response: %s", response.data);
    
    root = cJSON_Parse(response.data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse send response");
        solana_tx_destroy(tx);
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    // Check for error in response
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *message = cJSON_GetObjectItem(error, "message");
        if (cJSON_IsString(message)) {
            ESP_LOGE(TAG, "Solana RPC Error: %s", cJSON_GetStringValue(message));
        } else {
            ESP_LOGE(TAG, "Solana RPC returned an error (no message)");
        }
        cJSON_Delete(root);
        solana_tx_destroy(tx);
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    result = cJSON_GetObjectItem(root, "result");
    if (cJSON_IsString(result)) {
        strncpy(signature_out, cJSON_GetStringValue(result), max_sig_len - 1);
        signature_out[max_sig_len - 1] = '\0';
        ESP_LOGI(TAG, "Transaction sent! Signature: %s", signature_out);
    } else {
        ESP_LOGE(TAG, "Invalid signature in response (result is not a string)");
        cJSON_Delete(root);
        solana_tx_destroy(tx);
        solana_rpc_free_response(&response);
        return ESP_FAIL;
    }
    
    cJSON_Delete(root);
    solana_tx_destroy(tx);
    solana_rpc_free_response(&response);
    
    return ESP_OK;
}

void solana_wallet_destroy(solana_wallet_t *wallet) {
    if (wallet) {
        // Zero out secret key
        memset(wallet->secret_key, 0, 64);
        free(wallet);
        ESP_LOGI(TAG, "Wallet destroyed");
    }
}


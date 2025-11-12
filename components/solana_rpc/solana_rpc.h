#ifndef SOLANA_RPC_H
#define SOLANA_RPC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLANA_RPC_MAX_RESPONSE_SIZE 16384  // 16KB max response
#define SOLANA_RPC_TIMEOUT_MS 30000         // 30 second timeout

/**
 * @brief Solana RPC client handle
 */
typedef struct solana_rpc_client_t* solana_rpc_handle_t;

/**
 * @brief Solana RPC response structure
 */
typedef struct {
    char *data;           // Response data (JSON string)
    size_t length;        // Length of response data
    int status_code;      // HTTP status code
    bool success;         // Whether request was successful
} solana_rpc_response_t;

/**
 * @brief Create a new Solana RPC client
 * 
 * @param rpc_url Solana RPC endpoint URL (e.g., "https://api.mainnet-beta.solana.com")
 * @return Handle to RPC client or NULL on failure
 */
solana_rpc_handle_t solana_rpc_init(const char *rpc_url);

/**
 * @brief Free RPC response data
 * 
 * @param response Response structure to free
 */
void solana_rpc_free_response(solana_rpc_response_t *response);

/**
 * @brief Get latest blockhash
 * 
 * @param client RPC client handle
 * @param response Response structure to populate
 * @return ESP_OK on success
 */
esp_err_t solana_rpc_get_latest_blockhash(solana_rpc_handle_t client, solana_rpc_response_t *response);

/**
 * @brief Get balance for a public key
 * 
 * @param client RPC client handle
 * @param pubkey_base58 Public key in Base58 format
 * @param response Response structure to populate
 * @return ESP_OK on success
 */
esp_err_t solana_rpc_get_balance(solana_rpc_handle_t client, const char *pubkey_base58, 
                                  solana_rpc_response_t *response);

/**
 * @brief Send a raw transaction
 * 
 * @param client RPC client handle
 * @param transaction_base58 Serialized transaction in Base58 format
 * @param response Response structure to populate
 * @return ESP_OK on success
 */
esp_err_t solana_rpc_send_transaction(solana_rpc_handle_t client, const char *transaction_base58,
                                       solana_rpc_response_t *response);

/**
 * @brief Get transaction status
 * 
 * @param client RPC client handle
 * @param signature_base58 Transaction signature in Base58 format
 * @param response Response structure to populate
 * @return ESP_OK on success
 */
esp_err_t solana_rpc_get_transaction(solana_rpc_handle_t client, const char *signature_base58,
                                      solana_rpc_response_t *response);

/**
 * @brief Make a generic JSON-RPC call
 * 
 * @param client RPC client handle
 * @param method RPC method name
 * @param params JSON array of parameters (can be NULL)
 * @param response Response structure to populate
 * @return ESP_OK on success
 */
esp_err_t solana_rpc_call(solana_rpc_handle_t client, const char *method, const char *params,
                           solana_rpc_response_t *response);

/**
 * @brief Destroy RPC client and free resources
 * 
 * @param client RPC client handle
 */
void solana_rpc_destroy(solana_rpc_handle_t client);

#ifdef __cplusplus
}
#endif

#endif // SOLANA_RPC_H


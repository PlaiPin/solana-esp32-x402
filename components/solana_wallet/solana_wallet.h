#ifndef SOLANA_WALLET_H
#define SOLANA_WALLET_H

#include "esp_err.h"
#include "solana_tx.h"
#include "solana_rpc.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Solana wallet handle
 */
typedef struct solana_wallet_t solana_wallet_t;

/**
 * @brief Create wallet from hardcoded keypair (for testing)
 * 
 * @param secret_key 64-byte Ed25519 secret key
 * @param rpc_client RPC client handle
 * @return Wallet handle or NULL on failure
 */
solana_wallet_t* solana_wallet_from_keypair(const uint8_t *secret_key, 
                                              solana_rpc_handle_t rpc_client);

/**
 * @brief Get wallet's public key (address)
 * 
 * @param wallet Wallet handle
 * @param address_out Output buffer for Base58 address
 * @param max_len Maximum size of output buffer
 * @return ESP_OK on success
 */
esp_err_t solana_wallet_get_address(solana_wallet_t *wallet, char *address_out, size_t max_len);

/**
 * @brief Get wallet's public key (raw bytes)
 * 
 * @param wallet Wallet handle
 * @param pubkey_out Output buffer for 32-byte public key
 * @return ESP_OK on success
 */
esp_err_t solana_wallet_get_pubkey(solana_wallet_t *wallet, uint8_t *pubkey_out);

/**
 * @brief Get wallet balance in lamports
 * 
 * @param wallet Wallet handle
 * @param balance_out Output balance in lamports
 * @return ESP_OK on success
 */
esp_err_t solana_wallet_get_balance(solana_wallet_t *wallet, uint64_t *balance_out);

/**
 * @brief Send SOL to another address
 * 
 * @param wallet Wallet handle
 * @param to_address Destination address (Base58)
 * @param lamports Amount in lamports (1 SOL = 1,000,000,000 lamports)
 * @param signature_out Output buffer for transaction signature (Base58)
 * @param max_sig_len Maximum size of signature buffer
 * @return ESP_OK on success
 */
esp_err_t solana_wallet_send_sol(solana_wallet_t *wallet, const char *to_address,
                                   uint64_t lamports, char *signature_out, size_t max_sig_len);

/**
 * @brief Sign a transaction message
 * 
 * @param wallet Wallet handle
 * @param message Message bytes to sign
 * @param message_len Length of message
 * @param signature_out Output buffer for 64-byte signature
 * @return ESP_OK on success
 */
esp_err_t solana_wallet_sign(solana_wallet_t *wallet, const uint8_t *message, 
                               size_t message_len, uint8_t *signature_out);

/**
 * @brief Destroy wallet
 * 
 * @param wallet Wallet handle
 */
void solana_wallet_destroy(solana_wallet_t *wallet);

#ifdef __cplusplus
}
#endif

#endif // SOLANA_WALLET_H


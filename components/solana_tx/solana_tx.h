#ifndef SOLANA_TX_H
#define SOLANA_TX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLANA_PUBKEY_SIZE 32
#define SOLANA_SIGNATURE_SIZE 64
#define SOLANA_BLOCKHASH_SIZE 32

/**
 * @brief Solana public key
 */
typedef struct {
    uint8_t data[SOLANA_PUBKEY_SIZE];
} solana_pubkey_t;

/**
 * @brief Solana transaction builder
 */
typedef struct solana_tx_t solana_tx_t;

/**
 * @brief Create a new transaction builder
 * 
 * @param recent_blockhash Recent blockhash from network (Base58 string)
 * @param payer Fee payer public key
 * @return Transaction builder handle or NULL on failure
 */
solana_tx_t* solana_tx_new(const char *recent_blockhash, const solana_pubkey_t *payer);

/**
 * @brief Add a SOL transfer instruction
 * 
 * @param tx Transaction builder
 * @param from Source public key
 * @param to Destination public key
 * @param lamports Amount in lamports (1 SOL = 1,000,000,000 lamports)
 * @return ESP_OK on success
 */
esp_err_t solana_tx_add_transfer(solana_tx_t *tx, 
                                  const solana_pubkey_t *from,
                                  const solana_pubkey_t *to, 
                                  uint64_t lamports);

/**
 * @brief Get the transaction message to sign
 * 
 * @param tx Transaction builder
 * @param message_out Output buffer for message bytes
 * @param message_len Output message length
 * @param max_len Maximum size of output buffer
 * @return ESP_OK on success
 */
esp_err_t solana_tx_get_message(solana_tx_t *tx, uint8_t *message_out, 
                                 size_t *message_len, size_t max_len);

/**
 * @brief Add a signature to the transaction
 * 
 * @param tx Transaction builder
 * @param signature 64-byte signature
 * @return ESP_OK on success
 */
esp_err_t solana_tx_add_signature(solana_tx_t *tx, const uint8_t *signature);

/**
 * @brief Serialize the complete transaction (with signatures)
 * 
 * @param tx Transaction builder
 * @param output Output buffer for serialized transaction
 * @param output_len Output transaction length
 * @param max_len Maximum size of output buffer
 * @return ESP_OK on success
 */
esp_err_t solana_tx_serialize(solana_tx_t *tx, uint8_t *output, 
                               size_t *output_len, size_t max_len);

/**
 * @brief Destroy transaction builder
 * 
 * @param tx Transaction builder
 */
void solana_tx_destroy(solana_tx_t *tx);

/**
 * @brief Convert Base58 string to public key
 * 
 * @param base58_str Base58 encoded public key
 * @param pubkey Output public key
 * @return ESP_OK on success
 */
esp_err_t solana_pubkey_from_base58(const char *base58_str, solana_pubkey_t *pubkey);

/**
 * @brief Convert public key to Base58 string
 * 
 * @param pubkey Public key
 * @param base58_out Output buffer for Base58 string
 * @param max_len Maximum size of output buffer
 * @return ESP_OK on success
 */
esp_err_t solana_pubkey_to_base58(const solana_pubkey_t *pubkey, char *base58_out, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // SOLANA_TX_H


#ifndef X402_PAYMENT_H
#define X402_PAYMENT_H

#include "x402_types.h"
#include "solana_wallet.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create complete x402 payment payload for Solana
 * 
 * This is the main payment creation function:
 * 1. Parse payment requirements
 * 2. Build SPL token transfer transaction
 * 3. Sign transaction with wallet
 * 4. Create PaymentPayload structure
 * 
 * @param wallet User wallet (for signing)
 * @param requirements Payment requirements (from 402 response)
 * @param payload_out Output: payment payload (ready to encode)
 * @return ESP_OK on success
 */
esp_err_t x402_create_solana_payment(
    solana_wallet_t *wallet,
    const x402_payment_requirements_t *requirements,
    x402_payment_payload_t *payload_out
);

/**
 * @brief Build Solana transaction for x402 payment
 * 
 * Low-level function to build the actual transaction
 * 
 * @param wallet User wallet
 * @param recipient_pubkey Recipient public key (32 bytes)
 * @param mint_pubkey Token mint public key (32 bytes, e.g., USDC)
 * @param token_program_id Token program ID (32 bytes, Token or Token-2022)
 * @param amount Amount in token's smallest unit
 * @param tx_out Output: serialized unsigned transaction
 * @param tx_len Output: transaction length
 * @param max_tx_len Maximum transaction buffer size
 * @return ESP_OK on success
 */
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
);

/**
 * @brief Free resources allocated for payment payload
 * 
 * @param payload Payment payload to free
 */
void x402_payment_free(x402_payment_payload_t *payload);

#ifdef __cplusplus
}
#endif

#endif // X402_PAYMENT_H


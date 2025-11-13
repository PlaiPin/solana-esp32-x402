#ifndef X402_REQUIREMENTS_H
#define X402_REQUIREMENTS_H

#include "x402_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse payment requirements from 402 response body
 * 
 * The 402 response body contains JSON with payment requirements:
 * {
 *   "recipient": "<address>",
 *   "network": "solana-devnet",
 *   "price": {
 *     "amount": "100000",
 *     "currency": "USDC"
 *   },
 *   "facilitator": {
 *     "url": "http://localhost:3000"
 *   }
 * }
 * 
 * @param response_body JSON string from 402 response body
 * @param requirements_out Output: parsed payment requirements
 * @return ESP_OK on success
 */
esp_err_t x402_parse_payment_requirements(
    const char *response_body,
    x402_payment_requirements_t *requirements_out
);

/**
 * @brief Get facilitator URL from requirements
 * 
 * @param requirements Payment requirements
 * @return Facilitator URL string (pointer to field in struct)
 */
const char* x402_get_facilitator_url(
    const x402_payment_requirements_t *requirements
);

/**
 * @brief Convert price to token amount
 * 
 * Parses price.amount and applies token decimals
 * e.g., "100000" with USDC (6 decimals) = 100000
 * 
 * @param requirements Payment requirements
 * @param decimals Token decimals (6 for USDC)
 * @param amount_out Output: amount in smallest unit
 * @return ESP_OK on success
 */
esp_err_t x402_parse_price_to_amount(
    const x402_payment_requirements_t *requirements,
    uint8_t decimals,
    uint64_t *amount_out
);

/**
 * @brief Query facilitator /supported endpoint for fee payer
 * 
 * Queries the facilitator's /supported endpoint to get the fee payer address
 * that will be used to pay transaction fees.
 * 
 * @param facilitator_url Facilitator base URL
 * @param network Network name (e.g., "solana-devnet")
 * @param fee_payer_out Output: fee payer address (Base58)
 * @param max_len Maximum length of fee_payer_out buffer
 * @return ESP_OK on success
 */
esp_err_t x402_query_fee_payer(
    const char *facilitator_url,
    const char *network,
    char *fee_payer_out,
    size_t max_len
);

#ifdef __cplusplus
}
#endif

#endif // X402_REQUIREMENTS_H


#ifndef X402_CLIENT_H
#define X402_CLIENT_H

#include "x402_types.h"
#include "solana_wallet.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Main x402 fetch function (protocol-compliant)
 * 
 * Complete x402 payment flow:
 * 1. Initial HTTP request (no payment)
 * 2. Detect 402 Payment Required
 * 3. Parse payment requirements from response body
 * 4. Create and sign payment
 * 5. Encode payment in X-PAYMENT header
 * 6. Retry request with payment
 * 7. Parse X-PAYMENT-RESPONSE header
 * 8. Return final response
 * 
 * @param wallet User wallet for signing payments
 * @param url API endpoint URL
 * @param method HTTP method ("GET", "POST", etc.)
 * @param headers Optional additional headers (can be NULL)
 * @param body Optional request body (can be NULL)
 * @param response_out Output: complete x402 response
 * @return ESP_OK on success
 */
esp_err_t x402_fetch(
    solana_wallet_t *wallet,
    const char *url,
    const char *method,
    const char *headers,
    const char *body,
    x402_response_t *response_out
);

/**
 * @brief Free x402 response resources
 * 
 * @param response Response to free
 */
void x402_response_free(x402_response_t *response);

/**
 * @brief Check if response requires payment (402 status)
 * 
 * @param status_code HTTP status code
 * @return true if payment required
 */
static inline bool x402_is_payment_required(int status_code) {
    return status_code == X402_STATUS_PAYMENT_REQUIRED;
}

/**
 * @brief Extract header value from response headers
 * 
 * Helper function to extract a specific header value
 * 
 * @param headers Full header string
 * @param header_name Name of header to find
 * @param value_out Output buffer for header value
 * @param max_len Maximum length of value buffer
 * @return true if header found
 */
bool x402_extract_header(
    const char *headers,
    const char *header_name,
    char *value_out,
    size_t max_len
);

/**
 * @brief Verify a payment transaction on-chain
 *
 * Queries Solana RPC to verify the transaction was actually settled
 * and went to the correct recipient with the correct amount.
 *
 * @param rpc_url Solana RPC URL
 * @param tx_signature Transaction signature to verify (Base58)
 * @param expected_recipient Expected payment recipient
 * @param expected_amount Expected payment amount
 * @return ESP_OK on success (even if not fully implemented)
 */
esp_err_t x402_verify_payment(
    const char *rpc_url,
    const char *tx_signature,
    const char *expected_recipient,
    uint64_t expected_amount
);

#ifdef __cplusplus
}
#endif

#endif // X402_CLIENT_H


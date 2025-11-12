#ifndef X402_ENCODING_H
#define X402_ENCODING_H

#include "x402_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Serialize PaymentPayload to JSON string
 * 
 * @param payload Payment payload structure
 * @return JSON string (caller must free with free())
 */
char* x402_payload_to_json(const x402_payment_payload_t *payload);

/**
 * @brief Parse Settlement Response from JSON string
 * 
 * @param json_str JSON string
 * @param response_out Output: parsed settlement response
 * @return ESP_OK on success
 */
esp_err_t x402_parse_settlement_json(
    const char *json_str,
    x402_settlement_response_t *response_out
);

/**
 * @brief Encode PaymentPayload: struct → JSON → Base64
 * 
 * Complete encoding pipeline for X-PAYMENT header
 * 
 * @param payload Payment payload structure
 * @param encoded_out Output: base64-encoded string
 * @param max_len Maximum length of output buffer
 * @return ESP_OK on success
 */
esp_err_t x402_encode_payment_payload(
    const x402_payment_payload_t *payload,
    char *encoded_out,
    size_t max_len
);

/**
 * @brief Decode Settlement Response: Base64 → JSON → struct
 * 
 * Complete decoding pipeline for X-PAYMENT-RESPONSE header
 * 
 * @param encoded_b64 Base64-encoded string from header
 * @param response_out Output: parsed settlement response
 * @return ESP_OK on success
 */
esp_err_t x402_decode_settlement_response(
    const char *encoded_b64,
    x402_settlement_response_t *response_out
);

/**
 * @brief Base64 encode data
 * 
 * Wrapper around mbedtls base64 encoding
 * 
 * @param data Input data
 * @param data_len Length of input data
 * @param encoded_out Output buffer
 * @param max_len Maximum length of output buffer
 * @param out_len Actual length of encoded data
 * @return ESP_OK on success
 */
esp_err_t x402_base64_encode(
    const uint8_t *data,
    size_t data_len,
    char *encoded_out,
    size_t max_len,
    size_t *out_len
);

/**
 * @brief Base64 decode data
 * 
 * Wrapper around mbedtls base64 decoding
 * 
 * @param encoded Input base64 string
 * @param decoded_out Output buffer
 * @param max_len Maximum length of output buffer
 * @param out_len Actual length of decoded data
 * @return ESP_OK on success
 */
esp_err_t x402_base64_decode(
    const char *encoded,
    uint8_t *decoded_out,
    size_t max_len,
    size_t *out_len
);

#ifdef __cplusplus
}
#endif

#endif // X402_ENCODING_H


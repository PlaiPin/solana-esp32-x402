#ifndef BASE58_H
#define BASE58_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encode binary data to Base58 string (Bitcoin alphabet)
 * 
 * @param input Input binary data
 * @param input_len Length of input data
 * @param output Output buffer for Base58 string
 * @param output_size Size of output buffer
 * @return true if encoding succeeded, false otherwise
 */
bool base58_encode(const uint8_t *input, size_t input_len, char *output, size_t output_size);

/**
 * @brief Decode Base58 string to binary data
 * 
 * @param input Input Base58 string
 * @param output Output buffer for binary data
 * @param output_len Pointer to store output length
 * @param max_output_len Maximum size of output buffer
 * @return true if decoding succeeded, false otherwise
 */
bool base58_decode(const char *input, uint8_t *output, size_t *output_len, size_t max_output_len);

/**
 * @brief Get the maximum output size for encoding
 * 
 * @param input_len Length of input data
 * @return Maximum size needed for encoded output
 */
size_t base58_encode_size(size_t input_len);

/**
 * @brief Get the maximum output size for decoding
 * 
 * @param input_len Length of Base58 string
 * @return Maximum size needed for decoded output
 */
size_t base58_decode_size(size_t input_len);

#ifdef __cplusplus
}
#endif

#endif // BASE58_H


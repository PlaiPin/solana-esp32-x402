#include "base58.h"
#include <string.h>
#include <stdlib.h>

// Bitcoin Base58 alphabet (used by Solana)
static const char BASE58_ALPHABET[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

// Reverse lookup table for Base58 decoding
static const int8_t BASE58_DECODE_TABLE[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8, -1, -1, -1, -1, -1, -1,
    -1,  9, 10, 11, 12, 13, 14, 15, 16, -1, 17, 18, 19, 20, 21, -1,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, -1, -1, -1, -1, -1,
    -1, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, -1, 44, 45, 46,
    47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

size_t base58_encode_size(size_t input_len) {
    // Worst case: log(256) / log(58) ≈ 1.37
    return (input_len * 138 / 100) + 2;
}

size_t base58_decode_size(size_t input_len) {
    // Worst case: log(58) / log(256) ≈ 0.73
    return (input_len * 73 / 100) + 1;
}

bool base58_encode(const uint8_t *input, size_t input_len, char *output, size_t output_size) {
    if (!input || !output || input_len == 0 || output_size == 0) {
        return false;
    }

    // Count leading zeros
    size_t leading_zeros = 0;
    while (leading_zeros < input_len && input[leading_zeros] == 0) {
        leading_zeros++;
    }

    // Allocate buffer for base conversion (worst case size)
    size_t buffer_size = base58_encode_size(input_len);
    uint8_t *buffer = (uint8_t *)malloc(buffer_size);
    if (!buffer) {
        return false;
    }
    memset(buffer, 0, buffer_size);

    // Perform base conversion: binary to base58
    // Start with one digit (the algorithm grows the digits array as needed)
    size_t digits_len = 1;
    
    for (size_t i = 0; i < input_len; i++) {
        uint32_t carry = input[i];
        
        // Multiply existing digits by 256 and add new byte
        for (size_t j = 0; j < digits_len; j++) {
            carry += (uint32_t)buffer[j] << 8;
            buffer[j] = (uint8_t)(carry % 58);
            carry /= 58;
        }
        
        // If there's still carry, extend the digits array
        while (carry > 0) {
            if (digits_len >= buffer_size) {
                free(buffer);
                return false;  // Buffer overflow protection
            }
            buffer[digits_len++] = (uint8_t)(carry % 58);
            carry /= 58;
        }
    }

    // Calculate output length
    size_t final_len = leading_zeros + digits_len;
    if (final_len + 1 > output_size) {
        free(buffer);
        return false;
    }

    // Encode leading zeros as '1'
    size_t out_pos = 0;
    for (size_t i = 0; i < leading_zeros; i++) {
        output[out_pos++] = '1';
    }

    // Encode the digits in reverse order (buffer is little-endian)
    for (int i = (int)digits_len - 1; i >= 0; i--) {
        output[out_pos++] = BASE58_ALPHABET[buffer[i]];
    }
    
    output[out_pos] = '\0';
    free(buffer);
    return true;
}

bool base58_decode(const char *input, uint8_t *output, size_t *output_len, size_t max_output_len) {
    if (!input || !output || !output_len) {
        return false;
    }

    size_t input_len = strlen(input);
    if (input_len == 0) {
        return false;
    }

    // Count leading '1's (represent zero bytes)
    size_t leading_ones = 0;
    while (leading_ones < input_len && input[leading_ones] == '1') {
        leading_ones++;
    }

    // Allocate buffer for base conversion
    // Start with input_len as conservative size (base58 can't decode to more bytes than chars)
    size_t buffer_size = input_len;
    uint8_t *buffer = (uint8_t *)malloc(buffer_size);
    if (!buffer) {
        return false;
    }
    memset(buffer, 0, buffer_size);

    // Perform base conversion: base58 to binary
    // Start with zero bytes (algorithm grows as we process)
    size_t bytes_len = 1;
    buffer[0] = 0;

    // Process each base58 character
    for (size_t i = 0; i < input_len; i++) {
        int8_t digit = BASE58_DECODE_TABLE[(uint8_t)input[i]];
        if (digit < 0) {
            free(buffer);
            return false;  // Invalid character
        }

        uint32_t carry = (uint32_t)digit;
        
        // Multiply existing bytes by 58 and add new digit
        for (size_t j = 0; j < bytes_len; j++) {
            carry += (uint32_t)buffer[j] * 58;
            buffer[j] = (uint8_t)(carry & 0xFF);
            carry >>= 8;
        }
        
        // If there's still carry, extend the bytes array
        while (carry > 0) {
            if (bytes_len >= buffer_size) {
                free(buffer);
                return false;  // Buffer overflow protection
            }
            buffer[bytes_len++] = (uint8_t)(carry & 0xFF);
            carry >>= 8;
        }
    }

    // Count leading '1's and add corresponding zero bytes
    size_t zeros_to_add = 0;
    for (size_t i = 0; i < input_len && input[i] == '1'; i++) {
        zeros_to_add++;
    }

    // Calculate final output length
    size_t final_len = zeros_to_add + bytes_len;
    
    if (final_len > max_output_len) {
        free(buffer);
        return false;
    }

    // Write leading zeros
    memset(output, 0, zeros_to_add);

    // Reverse and copy the decoded bytes (buffer is little-endian, need big-endian)
    for (size_t i = 0; i < bytes_len; i++) {
        output[zeros_to_add + i] = buffer[bytes_len - 1 - i];
    }
    
    *output_len = final_len;
    free(buffer);
    return true;
}


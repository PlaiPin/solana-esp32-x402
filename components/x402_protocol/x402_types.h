#ifndef X402_TYPES_H
#define X402_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief x402 Protocol Constants
 */
#define X402_VERSION 1
#define X402_HEADER_PAYMENT "X-PAYMENT"
#define X402_HEADER_PAYMENT_RESPONSE "X-PAYMENT-RESPONSE"
#define X402_STATUS_PAYMENT_REQUIRED 402

#define X402_SCHEME_EXACT "exact"
#define X402_SCHEME_SPONSORED "sponsored"
#define X402_SCHEME_SUBSCRIPTION "subscription"

#define X402_NETWORK_SOLANA_DEVNET "solana-devnet"
#define X402_NETWORK_SOLANA_MAINNET "solana-mainnet"

/**
 * @brief Network-specific payload (part of PaymentPayload kind)
 * This is generic and works for Solana, Ethereum, etc.
 */
typedef struct {
    char *transaction;              // Base64-encoded signed transaction
    // Future: additional fields can go in "extra"
} x402_network_payload_t;

/**
 * @brief Payment Kind (x402 spec)
 */
typedef struct {
    int x402_version;               // Always 1
    char scheme[32];                // "exact", "sponsored", "subscription"
    char network[64];               // "solana-devnet", "solana-mainnet", etc.
    x402_network_payload_t payload; // Network-specific payload
} x402_payment_kind_t;

/**
 * @brief Complete Payment Payload (x402 spec)
 * 
 * This structure is serialized to JSON, then base64-encoded,
 * and sent in the X-PAYMENT header
 */
typedef struct {
    int version;                    // Always 1
    x402_payment_kind_t kind;       // Payment kind details
} x402_payment_payload_t;

/**
 * @brief Payment Requirements (from 402 response body)
 */
typedef struct {
    char recipient[64];             // Payment recipient address (Base58)
    char network[64];               // "solana-devnet"
    struct {
        char amount[32];            // Amount as string
        char currency[16];          // "USDC", "SOL", etc.
    } price;
    struct {
        char url[256];              // Facilitator URL
    } facilitator;
    bool valid;                     // Whether parsed successfully
} x402_payment_requirements_t;

/**
 * @brief Settlement Response (from X-PAYMENT-RESPONSE header)
 * 
 * This is base64-decoded from the header, then parsed from JSON
 */
typedef struct {
    char transaction[128];          // Transaction signature (Base58)
    bool success;                   // Whether settlement succeeded
    char network[64];               // Network identifier
} x402_settlement_response_t;

/**
 * @brief Complete x402 Response
 */
typedef struct {
    int status_code;                // HTTP status code
    char *headers;                  // Full response headers (caller must free)
    char *body;                     // Response body (caller must free)
    size_t body_len;                // Length of body
    bool payment_made;              // True if we made a payment
    x402_settlement_response_t settlement;  // Payment details (if payment_made)
} x402_response_t;

/**
 * @brief Facilitator Capabilities (from /supported endpoint)
 */
typedef struct {
    int x402_version;
    char scheme[32];
    char network[64];
    char fee_payer[64];             // Fee payer address
} x402_facilitator_info_t;

#ifdef __cplusplus
}
#endif

#endif // X402_TYPES_H


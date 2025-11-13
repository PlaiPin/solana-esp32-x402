#ifndef SPL_TOKEN_H
#define SPL_TOKEN_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SPL Token Program Constants
 */

// Token Program ID: TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA
extern const uint8_t SPL_TOKEN_PROGRAM_ID[32];

// Associated Token Program ID: ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL
extern const uint8_t SPL_ASSOCIATED_TOKEN_PROGRAM_ID[32];

// System Program ID: 11111111111111111111111111111111
extern const uint8_t SYSTEM_PROGRAM_ID[32];

// USDC Devnet Mint: 4zMMC9srt5Ri5X14GAgXhaHii3GnPAEERYPJgZJDncDU
extern const uint8_t USDC_DEVNET_MINT[32];

// USDC Decimals
#define USDC_DECIMALS 6

/**
 * @brief SPL Token Transfer Instruction Layout
 * 
 * Instruction: Transfer
 * Accounts:
 *   0. [writable] Source account
 *   1. [writable] Destination account
 *   2. [signer] Owner/Authority
 */
#define SPL_TOKEN_TRANSFER_INSTRUCTION 3

/**
 * @brief Get the token program ID that owns a mint
 * 
 * Queries the Solana RPC to get the mint account info and extracts the owner
 * (which is the token program ID - either Token or Token-2022).
 * 
 * @param rpc_url Solana RPC URL (e.g., "https://api.devnet.solana.com")
 * @param mint_pubkey Token mint public key (32 bytes)
 * @param program_id_out Output: Token program ID (32 bytes)
 * @return ESP_OK on success
 */
esp_err_t spl_token_get_mint_program(
    const char *rpc_url,
    const uint8_t *mint_pubkey,
    uint8_t *program_id_out
);

/**
 * @brief Derive Associated Token Account (ATA) address
 * 
 * Uses the standard ATA derivation: PDA of [wallet, token_program, mint]
 * Uses SPL_TOKEN_PROGRAM_ID by default.
 * 
 * @param wallet_pubkey Wallet public key (32 bytes)
 * @param mint_pubkey Token mint public key (32 bytes)
 * @param ata_out Output: ATA address (32 bytes)
 * @return ESP_OK on success
 */
esp_err_t spl_token_get_associated_token_address(
    const uint8_t *wallet_pubkey,
    const uint8_t *mint_pubkey,
    uint8_t *ata_out
);

/**
 * @brief Derive Associated Token Account (ATA) address with custom token program
 * 
 * Uses the standard ATA derivation: PDA of [wallet, token_program, mint]
 * Allows specifying a custom token program ID (e.g., Token-2022).
 * 
 * @param wallet_pubkey Wallet public key (32 bytes)
 * @param mint_pubkey Token mint public key (32 bytes)
 * @param token_program_id Token program ID (32 bytes)
 * @param ata_out Output: ATA address (32 bytes)
 * @return ESP_OK on success
 */
esp_err_t spl_token_get_associated_token_address_with_program(
    const uint8_t *wallet_pubkey,
    const uint8_t *mint_pubkey,
    const uint8_t *token_program_id,
    uint8_t *ata_out
);

/**
 * @brief Build SPL Token Transfer instruction
 * 
 * Creates a transfer instruction for SPL tokens (not SOL)
 * 
 * @param source_ata Source Associated Token Account (32 bytes)
 * @param dest_ata Destination Associated Token Account (32 bytes)
 * @param owner Owner/authority public key (32 bytes)
 * @param amount Amount to transfer (in token's smallest unit)
 * @param instruction_out Output: serialized instruction data
 * @param instruction_len Output: length of instruction data
 * @param max_instruction_len Maximum size of instruction buffer
 * @return ESP_OK on success
 */
esp_err_t spl_token_build_transfer_instruction(
    const uint8_t *source_ata,
    const uint8_t *dest_ata,
    const uint8_t *owner,
    uint64_t amount,
    uint8_t *instruction_out,
    size_t *instruction_len,
    size_t max_instruction_len
);

/**
 * @brief Build complete SPL token transfer transaction
 * 
 * Creates a transaction with:
 * - Recent blockhash
 * - SPL transfer instruction
 * - Proper account ordering
 * 
 * Transaction is unsigned - caller must sign it
 * 
 * @param from_wallet Sender wallet public key (32 bytes)
 * @param to_wallet Recipient wallet public key (32 bytes)
 * @param mint Token mint public key (32 bytes)
 * @param amount Amount to transfer
 * @param recent_blockhash Recent blockhash (32 bytes)
 * @param tx_out Output: serialized unsigned transaction
 * @param tx_len Output: transaction length
 * @param max_tx_len Maximum size of transaction buffer
 * @return ESP_OK on success
 */
esp_err_t spl_token_create_transfer_transaction(
    const uint8_t *fee_payer,
    const uint8_t *from_wallet,
    const uint8_t *to_wallet,
    const uint8_t *mint,
    const uint8_t *token_program_id,
    uint64_t amount,
    const uint8_t *recent_blockhash,
    uint8_t *tx_out,
    size_t *tx_len,
    size_t max_tx_len
);

/**
 * @brief Parse USD amount string to token amount
 * 
 * Converts "$0.10" to 100000 (for USDC with 6 decimals)
 * 
 * @param amount_str Amount string (e.g., "$0.10", "0.10")
 * @param decimals Token decimals (6 for USDC)
 * @param amount_out Output: amount in smallest unit
 * @return ESP_OK on success
 */
esp_err_t spl_token_parse_usd_amount(
    const char *amount_str,
    uint8_t decimals,
    uint64_t *amount_out
);

/**
 * @brief Check if an Associated Token Account exists
 * 
 * Queries Solana RPC to check if an ATA exists for the given wallet and mint.
 * If the account doesn't exist, it will need to be created before transfer.
 * 
 * @param rpc_client RPC client handle
 * @param wallet_pubkey Wallet public key (32 bytes)
 * @param mint_pubkey Mint public key (32 bytes)
 * @param exists_out Output: true if ATA exists, false otherwise
 * @return ESP_OK on success (regardless of whether ATA exists)
 */
esp_err_t spl_token_check_ata_exists(
    void *rpc_client,
    const uint8_t *wallet_pubkey,
    const uint8_t *mint_pubkey,
    bool *exists_out
);

/**
 * @brief Build instruction to create Associated Token Account
 * 
 * Creates the instruction data to create an ATA. This instruction should
 * be added to the transaction before the transfer instruction if the
 * destination ATA doesn't exist.
 * 
 * @param payer Payer for rent (fee payer, usually)
 * @param wallet Wallet that will own the ATA
 * @param mint Mint for the token
 * @param instruction_out Output instruction buffer
 * @param instruction_len Output instruction length
 * @param max_len Maximum buffer size
 * @return ESP_OK on success
 */
esp_err_t spl_token_build_create_ata_instruction(
    const uint8_t *payer,
    const uint8_t *wallet,
    const uint8_t *mint,
    uint8_t *instruction_out,
    size_t *instruction_len,
    size_t max_len
);

#ifdef __cplusplus
}
#endif

#endif // SPL_TOKEN_H


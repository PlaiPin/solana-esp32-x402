#include "solana_tx.h"
#include "base58.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SolanaTx";

// System Program ID: 11111111111111111111111111111111
static const uint8_t SYSTEM_PROGRAM_ID[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Transfer instruction discriminator for System Program
#define SYSTEM_TRANSFER_INSTRUCTION 2

#define MAX_ACCOUNTS 10
#define MAX_INSTRUCTIONS 5

typedef struct {
    solana_pubkey_t pubkey;
    bool is_signer;
    bool is_writable;
} account_meta_t;

typedef struct {
    solana_pubkey_t program_id;
    uint8_t account_indexes[MAX_ACCOUNTS];
    uint8_t account_count;
    uint8_t *data;
    size_t data_len;
} instruction_t;

struct solana_tx_t {
    uint8_t blockhash[SOLANA_BLOCKHASH_SIZE];
    solana_pubkey_t fee_payer;
    
    account_meta_t accounts[MAX_ACCOUNTS];
    size_t account_count;
    
    instruction_t instructions[MAX_INSTRUCTIONS];
    size_t instruction_count;
    
    uint8_t signatures[SOLANA_SIGNATURE_SIZE];
    bool has_signature;
};

// Compact-u16 encoding for Solana
static size_t encode_compact_u16(uint16_t value, uint8_t *out) {
    if (value <= 0x7f) {
        out[0] = (uint8_t)value;
        return 1;
    } else if (value <= 0x3fff) {
        out[0] = (uint8_t)((value & 0x7f) | 0x80);
        out[1] = (uint8_t)(value >> 7);
        return 2;
    } else {
        out[0] = (uint8_t)((value & 0x7f) | 0x80);
        out[1] = (uint8_t)(((value >> 7) & 0x7f) | 0x80);
        out[2] = (uint8_t)(value >> 14);
        return 3;
    }
}

static int find_or_add_account(solana_tx_t *tx, const solana_pubkey_t *pubkey, 
                                 bool is_signer, bool is_writable) {
    // Check if account already exists
    for (size_t i = 0; i < tx->account_count; i++) {
        if (memcmp(&tx->accounts[i].pubkey, pubkey, SOLANA_PUBKEY_SIZE) == 0) {
            // Update flags if needed
            tx->accounts[i].is_signer |= is_signer;
            tx->accounts[i].is_writable |= is_writable;
            return (int)i;
        }
    }
    
    // Add new account
    if (tx->account_count >= MAX_ACCOUNTS) {
        return -1;
    }
    
    memcpy(&tx->accounts[tx->account_count].pubkey, pubkey, SOLANA_PUBKEY_SIZE);
    tx->accounts[tx->account_count].is_signer = is_signer;
    tx->accounts[tx->account_count].is_writable = is_writable;
    return (int)(tx->account_count++);
}

solana_tx_t* solana_tx_new(const char *recent_blockhash, const solana_pubkey_t *payer) {
    if (!recent_blockhash || !payer) {
        return NULL;
    }
    
    solana_tx_t *tx = (solana_tx_t *)calloc(1, sizeof(solana_tx_t));
    if (!tx) {
        ESP_LOGE(TAG, "Failed to allocate transaction");
        return NULL;
    }
    
    // Decode blockhash from Base58
    size_t decoded_len;
    if (!base58_decode(recent_blockhash, tx->blockhash, &decoded_len, SOLANA_BLOCKHASH_SIZE)) {
        ESP_LOGE(TAG, "Failed to decode blockhash");
        free(tx);
        return NULL;
    }
    
    if (decoded_len != SOLANA_BLOCKHASH_SIZE) {
        ESP_LOGE(TAG, "Invalid blockhash length: %zu", decoded_len);
        free(tx);
        return NULL;
    }
    
    // Set fee payer (always first account, signer, writable)
    memcpy(&tx->fee_payer, payer, sizeof(solana_pubkey_t));
    find_or_add_account(tx, payer, true, true);
    
    ESP_LOGI(TAG, "Created new transaction");
    return tx;
}

esp_err_t solana_tx_add_transfer(solana_tx_t *tx, const solana_pubkey_t *from,
                                  const solana_pubkey_t *to, uint64_t lamports) {
    if (!tx || !from || !to) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (tx->instruction_count >= MAX_INSTRUCTIONS) {
        ESP_LOGE(TAG, "Too many instructions");
        return ESP_ERR_NO_MEM;
    }
    
    // Add accounts FIRST (must be before program for correct ordering)
    int from_idx = find_or_add_account(tx, from, true, true);
    int to_idx = find_or_add_account(tx, to, false, true);
    
    if (from_idx < 0 || to_idx < 0) {
        ESP_LOGE(TAG, "Failed to add accounts");
        return ESP_ERR_NO_MEM;
    }
    
    // Add System Program LAST (programs must be at end of account list)
    solana_pubkey_t system_program;
    memcpy(&system_program, SYSTEM_PROGRAM_ID, SOLANA_PUBKEY_SIZE);
    int prog_idx = find_or_add_account(tx, &system_program, false, false);
    if (prog_idx < 0) {
        ESP_LOGE(TAG, "Failed to add System Program");
        return ESP_ERR_NO_MEM;
    }
    
    // Build transfer instruction data: [u32 instruction, u64 lamports]
    uint8_t *data = (uint8_t *)malloc(12);
    if (!data) {
        return ESP_ERR_NO_MEM;
    }
    
    // Instruction discriminator (little-endian u32)
    data[0] = SYSTEM_TRANSFER_INSTRUCTION;
    data[1] = 0;
    data[2] = 0;
    data[3] = 0;
    
    // Amount in lamports (little-endian u64)
    data[4] = (uint8_t)(lamports & 0xFF);
    data[5] = (uint8_t)((lamports >> 8) & 0xFF);
    data[6] = (uint8_t)((lamports >> 16) & 0xFF);
    data[7] = (uint8_t)((lamports >> 24) & 0xFF);
    data[8] = (uint8_t)((lamports >> 32) & 0xFF);
    data[9] = (uint8_t)((lamports >> 40) & 0xFF);
    data[10] = (uint8_t)((lamports >> 48) & 0xFF);
    data[11] = (uint8_t)((lamports >> 56) & 0xFF);
    
    instruction_t *instr = &tx->instructions[tx->instruction_count];
    memcpy(&instr->program_id, SYSTEM_PROGRAM_ID, SOLANA_PUBKEY_SIZE);
    instr->account_indexes[0] = (uint8_t)from_idx;
    instr->account_indexes[1] = (uint8_t)to_idx;
    instr->account_count = 2;
    instr->data = data;
    instr->data_len = 12;
    
    tx->instruction_count++;
    
    ESP_LOGI(TAG, "Added transfer instruction: %llu lamports", lamports);
    return ESP_OK;
}

esp_err_t solana_tx_get_message(solana_tx_t *tx, uint8_t *message_out, 
                                 size_t *message_len, size_t max_len) {
    if (!tx || !message_out || !message_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t *ptr = message_out;
    size_t remaining = max_len;
    
    // Message header (3 bytes)
    if (remaining < 3) return ESP_ERR_NO_MEM;
    
    // Count signers and writable accounts
    uint8_t num_required_sigs = 0;
    uint8_t num_readonly_signed = 0;
    uint8_t num_readonly_unsigned = 0;
    
    for (size_t i = 0; i < tx->account_count; i++) {
        if (tx->accounts[i].is_signer) {
            num_required_sigs++;
            if (!tx->accounts[i].is_writable) {
                num_readonly_signed++;
            }
        } else if (!tx->accounts[i].is_writable) {
            num_readonly_unsigned++;
        }
    }
    
    *ptr++ = num_required_sigs;
    *ptr++ = num_readonly_signed;
    *ptr++ = num_readonly_unsigned;
    remaining -= 3;
    
    // Account addresses
    size_t encoded = encode_compact_u16((uint16_t)tx->account_count, ptr);
    ptr += encoded;
    remaining -= encoded;
    
    for (size_t i = 0; i < tx->account_count; i++) {
        if (remaining < SOLANA_PUBKEY_SIZE) return ESP_ERR_NO_MEM;
        memcpy(ptr, &tx->accounts[i].pubkey, SOLANA_PUBKEY_SIZE);
        ptr += SOLANA_PUBKEY_SIZE;
        remaining -= SOLANA_PUBKEY_SIZE;
    }
    
    // Recent blockhash
    if (remaining < SOLANA_BLOCKHASH_SIZE) return ESP_ERR_NO_MEM;
    memcpy(ptr, tx->blockhash, SOLANA_BLOCKHASH_SIZE);
    ptr += SOLANA_BLOCKHASH_SIZE;
    remaining -= SOLANA_BLOCKHASH_SIZE;
    
    // Instructions
    encoded = encode_compact_u16((uint16_t)tx->instruction_count, ptr);
    ptr += encoded;
    remaining -= encoded;
    
    for (size_t i = 0; i < tx->instruction_count; i++) {
        instruction_t *instr = &tx->instructions[i];
        
        // Program ID index (find it in accounts) - MUST already be there!
        int prog_idx = -1;
        for (size_t j = 0; j < tx->account_count; j++) {
            if (memcmp(&tx->accounts[j].pubkey, &instr->program_id, SOLANA_PUBKEY_SIZE) == 0) {
                prog_idx = (int)j;
                break;
            }
        }
        
        // Program MUST be in accounts already (added when instruction was created)
        if (prog_idx < 0) {
            ESP_LOGE(TAG, "Program ID not found in accounts - this is a bug!");
            return ESP_ERR_INVALID_STATE;
        }
        
        if (remaining < 1) return ESP_ERR_NO_MEM;
        *ptr++ = (uint8_t)prog_idx;
        remaining--;
        
        // Account indexes
        encoded = encode_compact_u16(instr->account_count, ptr);
        ptr += encoded;
        remaining -= encoded;
        
        if (remaining < instr->account_count) return ESP_ERR_NO_MEM;
        memcpy(ptr, instr->account_indexes, instr->account_count);
        ptr += instr->account_count;
        remaining -= instr->account_count;
        
        // Instruction data
        encoded = encode_compact_u16((uint16_t)instr->data_len, ptr);
        ptr += encoded;
        remaining -= encoded;
        
        if (remaining < instr->data_len) return ESP_ERR_NO_MEM;
        memcpy(ptr, instr->data, instr->data_len);
        ptr += instr->data_len;
        remaining -= instr->data_len;
    }
    
    *message_len = (size_t)(ptr - message_out);
    ESP_LOGI(TAG, "Serialized message: %zu bytes", *message_len);
    return ESP_OK;
}

esp_err_t solana_tx_add_signature(solana_tx_t *tx, const uint8_t *signature) {
    if (!tx || !signature) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(tx->signatures, signature, SOLANA_SIGNATURE_SIZE);
    tx->has_signature = true;
    
    ESP_LOGI(TAG, "Added signature to transaction");
    return ESP_OK;
}

esp_err_t solana_tx_serialize(solana_tx_t *tx, uint8_t *output, 
                               size_t *output_len, size_t max_len) {
    if (!tx || !output || !output_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!tx->has_signature) {
        ESP_LOGE(TAG, "Transaction has no signature");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t *ptr = output;
    size_t remaining = max_len;
    
    // Signature count (compact-u16)
    size_t encoded = encode_compact_u16(1, ptr);
    ptr += encoded;
    remaining -= encoded;
    
    // Signature
    if (remaining < SOLANA_SIGNATURE_SIZE) return ESP_ERR_NO_MEM;
    memcpy(ptr, tx->signatures, SOLANA_SIGNATURE_SIZE);
    ptr += SOLANA_SIGNATURE_SIZE;
    remaining -= SOLANA_SIGNATURE_SIZE;
    
    // Message
    size_t message_len;
    esp_err_t err = solana_tx_get_message(tx, ptr, &message_len, remaining);
    if (err != ESP_OK) {
        return err;
    }
    
    ptr += message_len;
    *output_len = (size_t)(ptr - output);
    
    ESP_LOGI(TAG, "Serialized transaction: %zu bytes", *output_len);
    return ESP_OK;
}

void solana_tx_destroy(solana_tx_t *tx) {
    if (tx) {
        // Free instruction data
        for (size_t i = 0; i < tx->instruction_count; i++) {
            if (tx->instructions[i].data) {
                free(tx->instructions[i].data);
            }
        }
        free(tx);
        ESP_LOGI(TAG, "Destroyed transaction");
    }
}

esp_err_t solana_pubkey_from_base58(const char *base58_str, solana_pubkey_t *pubkey) {
    if (!base58_str || !pubkey) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t decoded_len;
    if (!base58_decode(base58_str, pubkey->data, &decoded_len, SOLANA_PUBKEY_SIZE)) {
        return ESP_FAIL;
    }
    
    if (decoded_len != SOLANA_PUBKEY_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    return ESP_OK;
}

esp_err_t solana_pubkey_to_base58(const solana_pubkey_t *pubkey, char *base58_out, size_t max_len) {
    if (!pubkey || !base58_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!base58_encode(pubkey->data, SOLANA_PUBKEY_SIZE, base58_out, max_len)) {
        return ESP_FAIL;
    }
    
    return ESP_OK;
}


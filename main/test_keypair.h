/**
 * HARDCODED KEYPAIR FOR TESTING
 * 
 * To test with a funded devnet wallet:
 * 1. Generate a keypair using Solana CLI: solana-keygen new --outfile test-keypair.json
 * 2. Get the address: solana address --keypair test-keypair.json
 * 3. Fund it with devnet SOL: solana airdrop 2 <address> --url devnet
 * 4. Extract the secret key bytes from test-keypair.json and paste below
 * 
 * NOTE: This is for TESTING ONLY! Never hardcode real mainnet keys!
 */

#ifndef TEST_KEYPAIR_H
#define TEST_KEYPAIR_H

#include <stdint.h>

// Set to 1 to enable hardcoded test keypair
#define USE_TEST_KEYPAIR 1

#if USE_TEST_KEYPAIR

// Example keypair (NOT FUNDED - replace with your own!)
// This is a 64-byte Ed25519 secret key
static const uint8_t TEST_SECRET_KEY[64] = {
    // Paste your secret key bytes here from test-keypair.json
    // Format: [byte0, byte1, byte2, ..., byte63]
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

};

#endif // USE_TEST_KEYPAIR

#endif // TEST_KEYPAIR_H


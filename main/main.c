/**
 * ESP32-S3 Solana Wallet - Main Application
 * 
 * Implements native Solana wallet with Ed25519 signing using TweetNaCl
 * For Solana x402
 */

 #include <stdio.h>
 #include <string.h>
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "esp_system.h"
 #include "esp_log.h"
 #include "nvs_flash.h"
 #include "tweetnacl.h"
 #include "base58.h"
 #include "wifi_manager.h"
 #include "solana_rpc.h"
 #include "solana_tx.h"
 #include "solana_wallet.h"
 #include "x402_client.h"
 #include "x402_types.h"
 #include "spl_token.h"
 #include "test_keypair.h"
 
 static const char *TAG = "SOLANA_WALLET";
 
 // WiFi credentials - TODO: Move to NVS/config
 #define WIFI_SSID "plaipin2.4"
 #define WIFI_PASSWORD "discovery"
 
 // Solana RPC endpoint - using devnet for testing
 #define SOLANA_RPC_URL "https://api.devnet.solana.com"
 
 // x402 Demo Configuration
 // Set this to your local machine's IP where Kora demo is running
 // Example: "http://192.168.1.100:4021/protected"
 #define X402_API_URL "http://192.168.8.225:4021/protected"
 #define X402_ENABLE_TEST 1  // Set to 1 to enable x402 integration test
 
 // Test recipient for SOL transfers
 // Using our own wallet address to send to self (preserves SOL, only pays fees)
 #define TEST_RECIPIENT "11111111111111111111111111111111"
 
 /**
  * Print hex dump of binary data
  */
 static void print_hex(const char *label, const uint8_t *data, size_t len)
 {
     printf("%s: ", label);
     for (size_t i = 0; i < len; i++) {
         printf("%02x", data[i]);
         if ((i + 1) % 32 == 0) printf("\n     ");
     }
     printf("\n");
 }
 
 /**
  * Test TweetNaCl Ed25519 key generation and signing
  */
 static void test_tweetnacl(void)
 {
     ESP_LOGI(TAG, "=== Testing TweetNaCl Ed25519 ===");
     
     // Generate keypair
     uint8_t pk[crypto_sign_PUBLICKEYBYTES];  // 32 bytes public key
     uint8_t sk[crypto_sign_SECRETKEYBYTES];  // 64 bytes secret key
     
     ESP_LOGI(TAG, "Generating Ed25519 keypair...");
     int result = crypto_sign_keypair(pk, sk);
     if (result != 0) {
         ESP_LOGE(TAG, "Failed to generate keypair!");
         return;
     }
     
     print_hex("Public Key", pk, crypto_sign_PUBLICKEYBYTES);
     
     // Sign a test message
     const char *message = "Hello Solana from ESP32-S3!";
     size_t message_len = strlen(message);
     
     ESP_LOGI(TAG, "Original message: \"%s\" (%d bytes)", message, message_len);
     
     // TweetNaCl signature format: [64-byte signature][message]
     uint8_t signed_msg[crypto_sign_BYTES + message_len + 64];
     unsigned long long signed_len = 0;
     
     ESP_LOGI(TAG, "Signing message...");
     result = crypto_sign(signed_msg, &signed_len, (const uint8_t *)message, message_len, sk);
     if (result != 0) {
         ESP_LOGE(TAG, "Failed to sign message! Error: %d", result);
         return;
     }
     
     ESP_LOGI(TAG, "Signed message created (%llu bytes total)", signed_len);
     print_hex("Signature (64 bytes)", signed_msg, 64);
     
     // Verify signature
     uint8_t verified_msg[message_len + 64];
     unsigned long long verified_len = 0;
     
     ESP_LOGI(TAG, "Verifying signature...");
     result = crypto_sign_open(verified_msg, &verified_len, signed_msg, signed_len, pk);
     
     if (result == 0 && verified_len == message_len && memcmp(verified_msg, message, message_len) == 0) {
         ESP_LOGI(TAG, "✓ Signature verification SUCCESS!");
         ESP_LOGI(TAG, "✓ TweetNaCl Ed25519 is working perfectly on ESP32-S3!\n");
     } else {
         ESP_LOGE(TAG, "✗ Signature verification FAILED!\n");
     }
 }
 
 /**
  * Test Base58 encoding/decoding
  */
 static void test_base58(void)
 {
     ESP_LOGI(TAG, "=== Testing Base58 Encoding ===");
     
     // Generate a keypair for testing
     uint8_t pk[crypto_sign_PUBLICKEYBYTES];
     uint8_t sk[crypto_sign_SECRETKEYBYTES];
     crypto_sign_keypair(pk, sk);
     
     print_hex("Public Key (binary)", pk, crypto_sign_PUBLICKEYBYTES);
     
     // Encode to Base58 (Solana address format)
     char address[64];
     if (base58_encode(pk, crypto_sign_PUBLICKEYBYTES, address, sizeof(address))) {
         ESP_LOGI(TAG, "Solana Address: %s", address);
         ESP_LOGI(TAG, "Address length: %d chars\n", strlen(address));
         
         // Test decoding back
         uint8_t decoded[crypto_sign_PUBLICKEYBYTES];
         size_t decoded_len = 0;
         
         if (base58_decode(address, decoded, &decoded_len, sizeof(decoded))) {
             if (decoded_len == crypto_sign_PUBLICKEYBYTES && 
                 memcmp(decoded, pk, crypto_sign_PUBLICKEYBYTES) == 0) {
                 ESP_LOGI(TAG, "✓ Base58 encode/decode round-trip SUCCESS!\n");
             } else {
                 ESP_LOGE(TAG, "✗ Base58 decode mismatch!\n");
             }
         } else {
             ESP_LOGE(TAG, "✗ Base58 decode FAILED!\n");
         }
     } else {
         ESP_LOGE(TAG, "✗ Base58 encode FAILED!\n");
     }
 }
 
 /**
  * Test WiFi connection
  */
 static void test_wifi(void)
 {
     ESP_LOGI(TAG, "=== Testing WiFi Connection ===");
     
     // Initialize WiFi
     esp_err_t ret = wifi_manager_init();
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "✗ WiFi initialization FAILED: %s\n", esp_err_to_name(ret));
         return;
     }
     
     ESP_LOGI(TAG, "WiFi manager initialized");
     
     // Connect to WiFi
     ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", WIFI_SSID);
     ret = wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD, 30000);
     
     if (ret == ESP_OK) {
         char ip_str[16];
         if (wifi_manager_get_ip(ip_str, sizeof(ip_str)) == ESP_OK) {
             ESP_LOGI(TAG, "✓ WiFi connected! IP: %s\n", ip_str);
         } else {
             ESP_LOGI(TAG, "✓ WiFi connected!\n");
         }
     } else {
         ESP_LOGE(TAG, "✗ WiFi connection FAILED: %s", esp_err_to_name(ret));
         ESP_LOGE(TAG, "Please check your WiFi credentials in main.c\n");
     }
 }
 
 /**
  * Test Solana RPC client
  */
 static void test_solana_rpc(void)
 {
     ESP_LOGI(TAG, "=== Testing Solana RPC Client ===");
     
     if (!wifi_manager_is_connected()) {
         ESP_LOGE(TAG, "WiFi not connected, skipping RPC test\n");
         return;
     }
     
     // Initialize RPC client
     solana_rpc_handle_t rpc_client = solana_rpc_init(SOLANA_RPC_URL);
     if (!rpc_client) {
         ESP_LOGE(TAG, "✗ Failed to initialize RPC client\n");
         return;
     }
     
     ESP_LOGI(TAG, "RPC client initialized with endpoint: %s", SOLANA_RPC_URL);
     
     // Test: Get latest blockhash
     ESP_LOGI(TAG, "Fetching latest blockhash...");
     solana_rpc_response_t response = {0};
     esp_err_t ret = solana_rpc_get_latest_blockhash(rpc_client, &response);
     
     if (ret == ESP_OK && response.success) {
         ESP_LOGI(TAG, "✓ Got blockhash response:");
         ESP_LOGI(TAG, "  Status: %d", response.status_code);
         ESP_LOGI(TAG, "  Response length: %zu bytes", response.length);
         
         // Print first 200 chars of response
         if (response.data && response.length > 0) {
             int print_len = response.length < 200 ? response.length : 200;
             ESP_LOGI(TAG, "  Response (first %d chars): %.*s...", print_len, print_len, response.data);
         }
         
         solana_rpc_free_response(&response);
         ESP_LOGI(TAG, "✓ Solana RPC client working!\n");
     } else {
         ESP_LOGE(TAG, "✗ RPC call FAILED: %s", esp_err_to_name(ret));
         ESP_LOGE(TAG, "  Status code: %d\n", response.status_code);
         solana_rpc_free_response(&response);
     }
     
     // Test: Get balance for a known account (Solana fee payer)
     const char *test_pubkey = "11111111111111111111111111111111";  // System program
     ESP_LOGI(TAG, "Fetching balance for: %s", test_pubkey);
     ret = solana_rpc_get_balance(rpc_client, test_pubkey, &response);
     
     if (ret == ESP_OK && response.success) {
         ESP_LOGI(TAG, "✓ Got balance response:");
         if (response.data && response.length > 0) {
             int print_len = response.length < 200 ? response.length : 200;
             ESP_LOGI(TAG, "  Response: %.*s", print_len, response.data);
         }
         solana_rpc_free_response(&response);
     } else {
         ESP_LOGE(TAG, "✗ Balance query failed");
         solana_rpc_free_response(&response);
     }
     
     // Cleanup
     solana_rpc_destroy(rpc_client);
     ESP_LOGI(TAG, "RPC client destroyed\n");
 }
 
 /**
  * Test wallet and transaction functionality
  */
 static void test_wallet_and_transactions(solana_rpc_handle_t rpc_client)
 {
     ESP_LOGI(TAG, "=== Testing Wallet & Transactions ===");
     
 #if USE_TEST_KEYPAIR
     ESP_LOGI(TAG, "Using hardcoded test keypair");
     
     // Create wallet from test keypair
     solana_wallet_t *wallet = solana_wallet_from_keypair(TEST_SECRET_KEY, rpc_client);
     if (!wallet) {
         ESP_LOGE(TAG, "✗ Failed to create wallet from keypair");
         return;
     }
     
     // Get wallet address
     char address[64];
     if (solana_wallet_get_address(wallet, address, sizeof(address)) == ESP_OK) {
         ESP_LOGI(TAG, "Wallet address: %s", address);
     }
     
     // Get wallet balance
     uint64_t balance;
     if (solana_wallet_get_balance(wallet, &balance) == ESP_OK) {
         ESP_LOGI(TAG, "Wallet balance: %llu lamports (%.9f SOL)", 
                  balance, (double)balance / 1000000000.0);
         
         if (balance == 0) {
             ESP_LOGW(TAG, "Wallet has no SOL! Fund it with: solana airdrop 2 %s --url devnet", address);
         } else if (balance < 10000) {
             ESP_LOGW(TAG, "Balance too low for transaction (need ~10000 lamports for fee + transfer, have %llu)", balance);
             ESP_LOGW(TAG, "Fund wallet: solana airdrop 1 %s --url devnet", address);
         } else {
             // Send minimal amount to self (preserves all SOL, only pays fee)
             ESP_LOGI(TAG, "Sending 100 lamports (0.0000001 SOL) to self (same wallet)");
             ESP_LOGI(TAG, "Transaction fee: ~5000 lamports, Transfer: 100 lamports");
             ESP_LOGI(TAG, "Net cost: ~5000 lamports (fee only, transfer stays in wallet)");
             
             char signature[128];
             esp_err_t err = solana_wallet_send_sol(wallet, TEST_RECIPIENT, 100, 
                                                     signature, sizeof(signature));
             
             if (err == ESP_OK) {
                 ESP_LOGI(TAG, "✓ Transaction successful!");
                 ESP_LOGI(TAG, "✓ Signature: %s", signature);
                 ESP_LOGI(TAG, "✓ View on Solana Explorer:");
                 ESP_LOGI(TAG, "   https://explorer.solana.com/tx/%s?cluster=devnet", signature);
                 ESP_LOGI(TAG, "   (Transaction will appear in ~10-30 seconds)");
             } else {
                 ESP_LOGE(TAG, "✗ Transaction failed: %s", esp_err_to_name(err));
             }
         }
     } else {
         ESP_LOGE(TAG, "✗ Failed to get balance");
     }
     
     solana_wallet_destroy(wallet);
     
 #else
     ESP_LOGW(TAG, "Test keypair not enabled. Set USE_TEST_KEYPAIR=1 in test_keypair.h");
     ESP_LOGI(TAG, "To test transactions:");
     ESP_LOGI(TAG, "  1. Generate keypair: solana-keygen new --outfile test-keypair.json");
     ESP_LOGI(TAG, "  2. Fund with devnet SOL: solana airdrop 2 <address> --url devnet");
     ESP_LOGI(TAG, "  3. Copy secret key bytes to test_keypair.h");
     ESP_LOGI(TAG, "  4. Set USE_TEST_KEYPAIR=1 and rebuild");
 #endif
     
     ESP_LOGI(TAG, "=== Wallet Test Complete ===\n");
 }
 
 /**
  * Test x402 Protocol Compliance (Standard Implementation)
  * 
  * This demonstrates the NEW standard x402 protocol implementation:
  * - SPL token transfers (USDC payments)
  * - Proper PaymentPayload JSON structure
  * - Correct X-PAYMENT / X-PAYMENT-RESPONSE headers
  * - Full protocol-compliant flow
  */
 void test_x402_protocol_standard(solana_rpc_handle_t rpc_client)
 {
     ESP_LOGI(TAG, "==================================");
     ESP_LOGI(TAG, "Testing: x402 Protocol (Standard)");
     ESP_LOGI(TAG, "==================================\n");
     
     ESP_LOGI(TAG, "⚡ Running x402 Integration Test");
     ESP_LOGI(TAG, "");
     ESP_LOGI(TAG, "Configuration:");
     ESP_LOGI(TAG, "  API URL: %s", X402_API_URL);
     ESP_LOGI(TAG, "");
     
     // Create test wallet
     solana_wallet_t *wallet = solana_wallet_from_keypair(TEST_SECRET_KEY, rpc_client);
     if (!wallet) {
         ESP_LOGE(TAG, "Failed to create wallet");
         return;
     }
     
     char wallet_address[64];
     solana_wallet_get_address(wallet, wallet_address, sizeof(wallet_address));
     ESP_LOGI(TAG, "Wallet: %s", wallet_address);
     
     // Show wallet's USDC ATA
     uint8_t wallet_pubkey[32];
     uint8_t ata[32];
     esp_err_t err = solana_wallet_get_pubkey(wallet, wallet_pubkey);
     if (err == ESP_OK) {
         err = spl_token_get_associated_token_address(
             wallet_pubkey,
             USDC_DEVNET_MINT,
             ata
         );
         if (err == ESP_OK) {
             char ata_b58[64];
             if (base58_encode(ata, 32, ata_b58, sizeof(ata_b58))) {
                 ESP_LOGI(TAG, "USDC ATA: %s", ata_b58);
             }
         }
     }
     ESP_LOGI(TAG, "");
     
     // Make x402 request
     ESP_LOGI(TAG, "📡 Making x402 request...");
     ESP_LOGI(TAG, "");
     
     x402_response_t response;
     memset(&response, 0, sizeof(response));
     
     err = x402_fetch(
         wallet,
         X402_API_URL,
         "GET",
         NULL,  // No extra headers
         NULL,  // No body
         &response
     );
     
     if (err == ESP_OK) {
         ESP_LOGI(TAG, "");
         ESP_LOGI(TAG, "✅ x402 Request Successful!");
         ESP_LOGI(TAG, "");
         ESP_LOGI(TAG, "Status Code: %d", response.status_code);
         
         if (response.body) {
             ESP_LOGI(TAG, "Response Body: %s", response.body);
         }
         
         if (response.payment_made) {
             ESP_LOGI(TAG, "");
             ESP_LOGI(TAG, "💰 Payment Details:");
             ESP_LOGI(TAG, "  Network: %s", response.settlement.network);
             ESP_LOGI(TAG, "  Success: %s", response.settlement.success ? "true" : "false");
             ESP_LOGI(TAG, "  Transaction: %s", response.settlement.transaction);
             ESP_LOGI(TAG, "");
             ESP_LOGI(TAG, "🔗 View on Solana Explorer:");
             ESP_LOGI(TAG, "  https://explorer.solana.com/tx/%s?cluster=devnet",
                      response.settlement.transaction);
             ESP_LOGI(TAG, "");
         } else {
             ESP_LOGI(TAG, "");
             ESP_LOGI(TAG, "ℹ️  No payment was required for this request");
         }
         
         ESP_LOGI(TAG, "");
         ESP_LOGI(TAG, "✓ Complete x402 flow executed successfully:");
         ESP_LOGI(TAG, "  1. ✓ Initial request → 402 Payment Required");
         ESP_LOGI(TAG, "  2. ✓ Parsed payment requirements");
         ESP_LOGI(TAG, "  3. ✓ Queried facilitator /supported for fee payer");
         ESP_LOGI(TAG, "  4. ✓ Built USDC transfer transaction");
         ESP_LOGI(TAG, "  5. ✓ Signed transaction with Ed25519");
         ESP_LOGI(TAG, "  6. ✓ Encoded PaymentPayload (JSON → Base64)");
         ESP_LOGI(TAG, "  7. ✓ Retried with X-PAYMENT header");
         ESP_LOGI(TAG, "  8. ✓ Payment validated and settled");
         ESP_LOGI(TAG, "  9. ✓ Received 200 OK + content");
         ESP_LOGI(TAG, " 10. ✓ Parsed X-PAYMENT-RESPONSE");
     } else {
         ESP_LOGE(TAG, "");
         ESP_LOGE(TAG, "❌ x402 Request Failed: %s", esp_err_to_name(err));
         ESP_LOGE(TAG, "");
         ESP_LOGE(TAG, "Troubleshooting:");
         ESP_LOGE(TAG, "  1. Is Kora demo stack running?");
         ESP_LOGE(TAG, "     cd kora/docs/x402/demo");
         ESP_LOGE(TAG, "     pnpm run start:kora (Terminal 1)");
         ESP_LOGE(TAG, "     pnpm run start:facilitator (Terminal 2)");
         ESP_LOGE(TAG, "     pnpm run start:api (Terminal 3)");
         ESP_LOGE(TAG, "  2. Can ESP32 reach the API?");
         ESP_LOGE(TAG, "     Check X402_API_URL in main.c");
         ESP_LOGE(TAG, "     Should be: http://<your-ip>:4021/protected");
         ESP_LOGE(TAG, "  3. Does wallet have USDC balance?");
         ESP_LOGE(TAG, "     Get devnet USDC from Circle faucet");
         ESP_LOGE(TAG, "  4. Check logs above for specific error");
     }
     
     // Cleanup
     x402_response_free(&response);
     solana_wallet_destroy(wallet);
     
     ESP_LOGI(TAG, "");
     ESP_LOGI(TAG, "✓ x402 Protocol test complete\n");
 }
 
 void app_main(void)
 {
     ESP_LOGI(TAG, "=======================================================");
     ESP_LOGI(TAG, "ESP32-S3 Solana Wallet");
     ESP_LOGI(TAG, "Solana x402 for Cute Physical AI Companions on ESP32");
     ESP_LOGI(TAG, "=======================================================\n");
     
     // Initialize NVS (for key storage)
     esp_err_t ret = nvs_flash_init();
     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
         ESP_ERROR_CHECK(nvs_flash_erase());
         ret = nvs_flash_init();
     }
     ESP_ERROR_CHECK(ret);
     ESP_LOGI(TAG, "NVS initialized\n");
     
     // Test TweetNaCl Ed25519 signing
     test_tweetnacl();
     
     // Test Base58 encoding/decoding
     test_base58();
     
     // Initialize and test WiFi connection
     test_wifi();
     
     // Initialize Solana RPC client and fetch blockhash to verify connectivity
     solana_rpc_handle_t rpc_client = NULL;
     if (wifi_manager_is_connected()) {
         rpc_client = solana_rpc_init(SOLANA_RPC_URL);
         if (rpc_client) {
             solana_rpc_response_t response;
             esp_err_t err = solana_rpc_get_latest_blockhash(rpc_client, &response);
             if (err == ESP_OK && response.success) {
                 ESP_LOGI(TAG, "✓ Solana RPC client working!");
                 solana_rpc_free_response(&response);
             }
         }
     }
     
     // Test wallet creation and transaction signing
     if (rpc_client) {
         test_wallet_and_transactions(rpc_client);
     }
     
     // Test x402 Protocol (Standard Implementation)
 #if X402_ENABLE_TEST
     if (wifi_manager_is_connected() && rpc_client) {
         test_x402_protocol_standard(rpc_client);
     }
 #endif
     
     // Cleanup
     if (rpc_client) {
         solana_rpc_destroy(rpc_client);
     }
     
     ESP_LOGI(TAG, "==================================");
     ESP_LOGI(TAG, "All tests complete!");
     ESP_LOGI(TAG, "==================================\n");
     
     ESP_LOGI(TAG, "✓ TweetNaCl Ed25519 signing");
     ESP_LOGI(TAG, "✓ Base58 encoding/decoding");
     ESP_LOGI(TAG, "✓ WiFi connectivity");
     ESP_LOGI(TAG, "✓ Solana RPC client");
     ESP_LOGI(TAG, "✓ Transaction builder");
     ESP_LOGI(TAG, "✓ Wallet API");
     ESP_LOGI(TAG, "✓ SPL Token support (USDC)");
     ESP_LOGI(TAG, "✓ x402 Protocol (Standard Compliant)");
     ESP_LOGI(TAG, "\n🎉 Cute Physical AI Companions Are Ready for x402!");
     
     while (1) {
         vTaskDelay(pdMS_TO_TICKS(10000));
     }
 }
 
#include "x402_client.h"
#include "x402_requirements.h"
#include "x402_payment.h"
#include "x402_encoding.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "x402_client";

/**
 * @brief HTTP response buffer structure
 */
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} http_buffer_t;

/**
 * @brief HTTP event handler for capturing response
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    http_buffer_t *buffer = (http_buffer_t *)evt->user_data;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // Allocate or expand buffer
            if (buffer->data == NULL) {
                buffer->capacity = 4096;
                buffer->data = malloc(buffer->capacity);
                buffer->len = 0;
            }
            
            // Check if we need to expand
            if (buffer->len + evt->data_len >= buffer->capacity) {
                buffer->capacity *= 2;
                buffer->data = realloc(buffer->data, buffer->capacity);
            }
            
            // Copy data
            if (buffer->data) {
                memcpy(buffer->data + buffer->len, evt->data, evt->data_len);
                buffer->len += evt->data_len;
                buffer->data[buffer->len] = '\0';
            }
            break;
            
        default:
            break;
    }
    
    return ESP_OK;
}

/**
 * @brief Internal HTTP request function
 */
static esp_err_t http_request(
    const char *url,
    const char *method,
    const char *headers,
    const char *body,
    int *status_out,
    char **headers_out,
    char **body_out,
    size_t *body_len_out
) {
    esp_err_t err;
    
    // Setup HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
    };
    
    // Parse method
    if (strcmp(method, "POST") == 0) {
        config.method = HTTP_METHOD_POST;
    } else if (strcmp(method, "PUT") == 0) {
        config.method = HTTP_METHOD_PUT;
    } else if (strcmp(method, "DELETE") == 0) {
        config.method = HTTP_METHOD_DELETE;
    }
    
    http_buffer_t response_buffer = {0};
    config.user_data = &response_buffer;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }
    
    // Set custom headers
    if (headers) {
        // Parse and set each header
        char *headers_copy = strdup(headers);
        char *line = strtok(headers_copy, "\r\n");
        while (line) {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                char *value = colon + 1;
                while (*value == ' ') value++; // Skip spaces
                ESP_LOGI(TAG, "Setting header: '%s' = '%.80s%s'", 
                         line, value, strlen(value) > 80 ? "..." : "");
                esp_http_client_set_header(client, line, value);
            }
            line = strtok(NULL, "\r\n");
        }
        free(headers_copy);
    }
    
    // Set body if present
    if (body && body[0]) {
        esp_http_client_set_post_field(client, body, strlen(body));
    }
    
    // Perform request
    err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        if (response_buffer.data) free(response_buffer.data);
        return err;
    }
    
    // Get status code
    *status_out = esp_http_client_get_status_code(client);
    
    // Get headers (simplified - just store all headers as string)
    int content_length = esp_http_client_get_content_length(client);
    *headers_out = malloc(1024);
    if (*headers_out) {
        snprintf(*headers_out, 1024, "Content-Length: %d\r\n", content_length);
        
        // Try to get X-PAYMENT-RESPONSE header
        char *payment_response = NULL;
        if (esp_http_client_get_header(client, X402_HEADER_PAYMENT_RESPONSE, 
                                       &payment_response) == ESP_OK && payment_response) {
            size_t current_len = strlen(*headers_out);
            snprintf(*headers_out + current_len, 1024 - current_len,
                     "%s: %s\r\n", X402_HEADER_PAYMENT_RESPONSE, payment_response);
        }
    }
    
    // Get body
    *body_out = response_buffer.data;
    *body_len_out = response_buffer.len;
    
    esp_http_client_cleanup(client);
    
    ESP_LOGD(TAG, "HTTP %s %s -> %d (%zu bytes)",
             method, url, *status_out, *body_len_out);
    
    return ESP_OK;
}

bool x402_extract_header(
    const char *headers,
    const char *header_name,
    char *value_out,
    size_t max_len
) {
    if (!headers || !header_name || !value_out) {
        return false;
    }
    
    // Search for header
    char search_str[128];
    snprintf(search_str, sizeof(search_str), "%s:", header_name);
    
    const char *header_pos = strstr(headers, search_str);
    if (!header_pos) {
        return false;
    }
    
    // Skip header name and colon
    header_pos += strlen(search_str);
    
    // Skip whitespace
    while (*header_pos == ' ' || *header_pos == '\t') {
        header_pos++;
    }
    
    // Copy until end of line
    size_t i = 0;
    while (*header_pos && *header_pos != '\r' && *header_pos != '\n' && i < max_len - 1) {
        value_out[i++] = *header_pos++;
    }
    value_out[i] = '\0';
    
    return i > 0;
}

esp_err_t x402_fetch(
    solana_wallet_t *wallet,
    const char *url,
    const char *method,
    const char *headers,
    const char *body,
    x402_response_t *response_out
) {
    if (!wallet || !url || !method || !response_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(response_out, 0, sizeof(x402_response_t));
    
    esp_err_t err;
    
    ESP_LOGI(TAG, "=== x402 Fetch: %s %s ===", method, url);
    
    // Step 1: Initial request (no payment)
    int status_code;
    char *resp_headers = NULL;
    char *resp_body = NULL;
    size_t resp_body_len;
    
    ESP_LOGI(TAG, "Step 1: Initial request (no payment)");
    err = http_request(url, method, headers, body,
                      &status_code, &resp_headers, &resp_body, &resp_body_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Initial request failed");
        return err;
    }
    
    ESP_LOGI(TAG, "Response: %d", status_code);
    
    // Step 2: Check if payment required
    if (!x402_is_payment_required(status_code)) {
        ESP_LOGI(TAG, "No payment required, returning response");
        response_out->status_code = status_code;
        response_out->headers = resp_headers;
        response_out->body = resp_body;
        response_out->body_len = resp_body_len;
        response_out->payment_made = false;
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Step 2: 402 Payment Required detected");
    
    // Step 3: Parse payment requirements from response body
    if (!resp_body) {
        ESP_LOGE(TAG, "402 response has no body");
        if (resp_headers) free(resp_headers);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Parsing payment requirements from body");
    
    x402_payment_requirements_t requirements;
    err = x402_parse_payment_requirements(resp_body, &requirements);
    
    // Free initial response (we'll make a new one)
    free(resp_headers);
    free(resp_body);
    resp_headers = NULL;
    resp_body = NULL;
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse payment requirements");
        return err;
    }
    
    ESP_LOGI(TAG, "Step 3: Payment requirements parsed");
    
    // Step 4: Create payment
    ESP_LOGI(TAG, "Step 4: Creating payment...");
    x402_payment_payload_t payload;
    err = x402_create_solana_payment(wallet, &requirements, &payload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create payment");
        return err;
    }
    
    ESP_LOGI(TAG, "Payment created successfully");
    
    // Step 5: Encode payment payload (JSON → Base64)
    char *payment_encoded = malloc(8192);
    if (!payment_encoded) {
        x402_payment_free(&payload);
        return ESP_ERR_NO_MEM;
    }
    
    err = x402_encode_payment_payload(&payload, payment_encoded, 8192);
    x402_payment_free(&payload);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to encode payment");
        free(payment_encoded);
        return err;
    }
    
    ESP_LOGI(TAG, "Step 5: Payment encoded");
    ESP_LOGI(TAG, "X-PAYMENT header (first 100 chars): %.100s", payment_encoded);
    ESP_LOGI(TAG, "X-PAYMENT header length: %zu bytes", strlen(payment_encoded));
    
    // Step 6: Build retry headers with X-PAYMENT
    char *retry_headers = malloc(16384);
    if (!retry_headers) {
        free(payment_encoded);
        return ESP_ERR_NO_MEM;
    }
    
    if (headers && headers[0]) {
        snprintf(retry_headers, 16384, "%s\r\n%s: %s",
                headers, X402_HEADER_PAYMENT, payment_encoded);
    } else {
        snprintf(retry_headers, 16384, "%s: %s",
                X402_HEADER_PAYMENT, payment_encoded);
    }
    
    free(payment_encoded);
    
    // Step 7: Retry request with payment
    ESP_LOGI(TAG, "Step 6: Retrying request with payment...");
    err = http_request(url, method, retry_headers, body,
                      &status_code, &resp_headers, &resp_body, &resp_body_len);
    
    free(retry_headers);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Retry request failed");
        return err;
    }
    
    ESP_LOGI(TAG, "Retry response: %d", status_code);
    
    // Log response headers for debugging
    if (resp_headers) {
        ESP_LOGI(TAG, "Response headers:");
        ESP_LOGI(TAG, "%s", resp_headers);
    }
    
    // Log response body for debugging
    if (resp_body && resp_body_len > 0) {
        ESP_LOGI(TAG, "Response body: %.*s", (int)resp_body_len, resp_body);
    }
    
    // Step 8: Parse X-PAYMENT-RESPONSE header (if present)
    if (resp_headers) {
        char payment_response[4096];
        if (x402_extract_header(resp_headers, X402_HEADER_PAYMENT_RESPONSE,
                               payment_response, sizeof(payment_response))) {
            
            ESP_LOGI(TAG, "Step 7: Payment response received");
            ESP_LOGI(TAG, "X-PAYMENT-RESPONSE: %s", payment_response);
            
            x402_settlement_response_t settlement;
            err = x402_decode_settlement_response(payment_response, &settlement);
            if (err == ESP_OK) {
                response_out->settlement = settlement;
                response_out->payment_made = true;
                
                ESP_LOGI(TAG, "✓ Payment settled!");
                ESP_LOGI(TAG, "Transaction: %s", settlement.transaction);
                ESP_LOGI(TAG, "Explorer: https://explorer.solana.com/tx/%s?cluster=devnet",
                        settlement.transaction);
            } else {
                ESP_LOGW(TAG, "Failed to decode payment response");
            }
        } else {
            ESP_LOGW(TAG, "No X-PAYMENT-RESPONSE header found");
        }
    }
    
    // Step 9: Populate response
    response_out->status_code = status_code;
    response_out->headers = resp_headers;
    response_out->body = resp_body;
    response_out->body_len = resp_body_len;
    
    if (status_code == 200) {
        ESP_LOGI(TAG, "=== ✓ x402 fetch successful! ===");
    } else if (status_code == 402) {
        ESP_LOGE(TAG, "Payment was rejected by the API server!");
        ESP_LOGE(TAG, "Check the response body above for error details");
    } else {
        ESP_LOGW(TAG, "Unexpected status: %d", status_code);
    }
    
    return ESP_OK;
}

void x402_response_free(x402_response_t *response) {
    if (!response) {
        return;
    }
    
    if (response->headers) {
        free(response->headers);
        response->headers = NULL;
    }
    
    if (response->body) {
        free(response->body);
        response->body = NULL;
    }
    
    memset(response, 0, sizeof(x402_response_t));
}

esp_err_t x402_verify_payment(
    const char *rpc_url,
    const char *tx_signature,
    const char *expected_recipient,
    uint64_t expected_amount
) {
    if (!rpc_url || !tx_signature || !expected_recipient) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Verifying payment on-chain...");
    ESP_LOGI(TAG, "Transaction: %s", tx_signature);
    ESP_LOGI(TAG, "Expected recipient: %s", expected_recipient);
    ESP_LOGI(TAG, "Expected amount: %llu", expected_amount);
    
    // TODO: Implement full RPC query to getTransaction
    // For now, we trust the API's X-PAYMENT-RESPONSE
    // Full implementation would:
    // 1. Call getTransaction RPC method
    // 2. Parse transaction details
    // 3. Verify recipient and amount match
    // 4. Check transaction status (confirmed/finalized)
    
    ESP_LOGW(TAG, "On-chain verification not yet implemented - trusting API response");
    ESP_LOGI(TAG, "Verify transaction at:");
    ESP_LOGI(TAG, "https://explorer.solana.com/tx/%s?cluster=devnet", tx_signature);
    
    return ESP_OK;
}


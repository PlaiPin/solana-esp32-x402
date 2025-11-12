#include "solana_rpc.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SolanaRPC";

typedef struct solana_rpc_client_t {
    char *rpc_url;
    int timeout_ms;
    int request_id;
} solana_rpc_client_t;

typedef struct {
    char *buffer;
    size_t size;
    size_t capacity;
} http_response_buffer_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_buffer_t *response = (http_response_buffer_t *)evt->user_data;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Expand buffer if needed
                if (response->size + evt->data_len >= response->capacity) {
                    size_t new_capacity = response->capacity * 2;
                    if (new_capacity < response->size + evt->data_len + 1) {
                        new_capacity = response->size + evt->data_len + 1;
                    }
                    if (new_capacity > SOLANA_RPC_MAX_RESPONSE_SIZE) {
                        ESP_LOGE(TAG, "Response too large: %zu bytes", new_capacity);
                        return ESP_FAIL;
                    }
                    
                    char *new_buffer = realloc(response->buffer, new_capacity);
                    if (!new_buffer) {
                        ESP_LOGE(TAG, "Failed to expand response buffer");
                        return ESP_ERR_NO_MEM;
                    }
                    response->buffer = new_buffer;
                    response->capacity = new_capacity;
                }
                
                // Append data
                memcpy(response->buffer + response->size, evt->data, evt->data_len);
                response->size += evt->data_len;
                response->buffer[response->size] = '\0';
            }
            break;
            
        default:
            break;
    }
    
    return ESP_OK;
}

solana_rpc_handle_t solana_rpc_init(const char *rpc_url)
{
    if (!rpc_url) {
        ESP_LOGE(TAG, "RPC URL cannot be NULL");
        return NULL;
    }

    solana_rpc_client_t *client = malloc(sizeof(solana_rpc_client_t));
    if (!client) {
        ESP_LOGE(TAG, "Failed to allocate RPC client");
        return NULL;
    }

    client->rpc_url = strdup(rpc_url);
    if (!client->rpc_url) {
        ESP_LOGE(TAG, "Failed to duplicate RPC URL");
        free(client);
        return NULL;
    }

    client->timeout_ms = SOLANA_RPC_TIMEOUT_MS;
    client->request_id = 1;

    ESP_LOGI(TAG, "Initialized Solana RPC client with URL: %s", rpc_url);
    return client;
}

void solana_rpc_free_response(solana_rpc_response_t *response)
{
    if (response && response->data) {
        free(response->data);
        response->data = NULL;
        response->length = 0;
    }
}

esp_err_t solana_rpc_call(solana_rpc_handle_t client, const char *method, const char *params,
                           solana_rpc_response_t *response)
{
    if (!client || !method || !response) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize response
    memset(response, 0, sizeof(solana_rpc_response_t));

    // Build JSON-RPC request
    char *request_body = NULL;
    if (params) {
        asprintf(&request_body, 
                 "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\",\"params\":%s}",
                 client->request_id++, method, params);
    } else {
        asprintf(&request_body, 
                 "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\"}",
                 client->request_id++, method);
    }

    if (!request_body) {
        ESP_LOGE(TAG, "Failed to allocate request body");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(TAG, "Request: %s", request_body);

    // Prepare response buffer
    http_response_buffer_t http_response = {
        .buffer = malloc(4096),
        .size = 0,
        .capacity = 4096
    };

    if (!http_response.buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        free(request_body);
        return ESP_ERR_NO_MEM;
    }

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = client->rpc_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = client->timeout_ms,
        .event_handler = http_event_handler,
        .user_data = &http_response,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };

    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    if (!http_client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        free(request_body);
        free(http_response.buffer);
        return ESP_FAIL;
    }

    // Set headers
    esp_http_client_set_header(http_client, "Content-Type", "application/json");

    // Set POST data
    esp_http_client_set_post_field(http_client, request_body, strlen(request_body));

    // Perform request
    esp_err_t err = esp_http_client_perform(http_client);
    response->status_code = esp_http_client_get_status_code(http_client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Status: %d, Response length: %zu", 
                 response->status_code, http_response.size);
        
        if (response->status_code == 200 && http_response.size > 0) {
            response->data = http_response.buffer;
            response->length = http_response.size;
            response->success = true;
            ESP_LOGD(TAG, "Response: %s", response->data);
        } else {
            ESP_LOGE(TAG, "RPC call failed with status %d", response->status_code);
            free(http_response.buffer);
            response->success = false;
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        free(http_response.buffer);
        response->success = false;
    }

    // Cleanup
    esp_http_client_cleanup(http_client);
    free(request_body);

    return err;
}

esp_err_t solana_rpc_get_latest_blockhash(solana_rpc_handle_t client, solana_rpc_response_t *response)
{
    const char *params = "[{\"commitment\":\"finalized\"}]";
    return solana_rpc_call(client, "getLatestBlockhash", params, response);
}

esp_err_t solana_rpc_get_balance(solana_rpc_handle_t client, const char *pubkey_base58, 
                                  solana_rpc_response_t *response)
{
    if (!pubkey_base58) {
        return ESP_ERR_INVALID_ARG;
    }

    char *params = NULL;
    asprintf(&params, "[\"%s\",{\"commitment\":\"finalized\"}]", pubkey_base58);
    
    if (!params) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = solana_rpc_call(client, "getBalance", params, response);
    free(params);
    return ret;
}

esp_err_t solana_rpc_send_transaction(solana_rpc_handle_t client, const char *transaction_base58,
                                       solana_rpc_response_t *response)
{
    if (!transaction_base58) {
        return ESP_ERR_INVALID_ARG;
    }

    char *params = NULL;
    asprintf(&params, "[\"%s\",{\"encoding\":\"base58\",\"skipPreflight\":false,\"preflightCommitment\":\"finalized\"}]",
             transaction_base58);
    
    if (!params) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = solana_rpc_call(client, "sendTransaction", params, response);
    free(params);
    return ret;
}

esp_err_t solana_rpc_get_transaction(solana_rpc_handle_t client, const char *signature_base58,
                                      solana_rpc_response_t *response)
{
    if (!signature_base58) {
        return ESP_ERR_INVALID_ARG;
    }

    char *params = NULL;
    asprintf(&params, "[\"%s\",{\"encoding\":\"json\",\"commitment\":\"finalized\"}]",
             signature_base58);
    
    if (!params) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = solana_rpc_call(client, "getTransaction", params, response);
    free(params);
    return ret;
}

void solana_rpc_destroy(solana_rpc_handle_t client)
{
    if (client) {
        if (client->rpc_url) {
            free(client->rpc_url);
        }
        free(client);
        ESP_LOGI(TAG, "RPC client destroyed");
    }
}


#include "x402_requirements.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "x402_requirements";

esp_err_t x402_parse_payment_requirements(
    const char *response_body,
    x402_payment_requirements_t *requirements_out
) {
    if (!response_body || !requirements_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(requirements_out, 0, sizeof(x402_payment_requirements_t));
    
    cJSON *root = cJSON_Parse(response_body);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse requirements JSON");
        return ESP_FAIL;
    }
    
    // Parse "accepts" array
    cJSON *accepts = cJSON_GetObjectItem(root, "accepts");
    if (!accepts || !cJSON_IsArray(accepts) || cJSON_GetArraySize(accepts) == 0) {
        ESP_LOGE(TAG, "Missing or empty 'accepts' array");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Get first payment option
    cJSON *option = cJSON_GetArrayItem(accepts, 0);
    if (!option) {
        ESP_LOGE(TAG, "Failed to get first payment option");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Parse "payTo" (recipient)
    cJSON *payTo = cJSON_GetObjectItem(option, "payTo");
    if (payTo && cJSON_IsString(payTo)) {
        strncpy(requirements_out->recipient, payTo->valuestring,
                sizeof(requirements_out->recipient) - 1);
    } else {
        ESP_LOGE(TAG, "Missing or invalid 'payTo' field");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Parse "network"
    cJSON *network = cJSON_GetObjectItem(option, "network");
    if (network && cJSON_IsString(network)) {
        strncpy(requirements_out->network, network->valuestring,
                sizeof(requirements_out->network) - 1);
    } else {
        ESP_LOGW(TAG, "Missing 'network' field, assuming solana-devnet");
        strncpy(requirements_out->network, X402_NETWORK_SOLANA_DEVNET,
                sizeof(requirements_out->network) - 1);
    }
    
    // Parse "asset" (token mint)
    cJSON *asset = cJSON_GetObjectItem(option, "asset");
    if (asset && cJSON_IsString(asset)) {
        strncpy(requirements_out->asset, asset->valuestring,
                sizeof(requirements_out->asset) - 1);
    } else {
        ESP_LOGW(TAG, "Missing 'asset' field");
    }
    
    // Parse "maxAmountRequired"
    cJSON *maxAmount = cJSON_GetObjectItem(option, "maxAmountRequired");
    if (maxAmount && cJSON_IsString(maxAmount)) {
        strncpy(requirements_out->price.amount, maxAmount->valuestring,
                sizeof(requirements_out->price.amount) - 1);
        // Assume USDC for now
        strncpy(requirements_out->price.currency, "USDC",
                sizeof(requirements_out->price.currency) - 1);
    } else {
        ESP_LOGE(TAG, "Missing or invalid 'maxAmountRequired' field");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Parse "extra.feePayer"
    cJSON *extra = cJSON_GetObjectItem(option, "extra");
    if (extra && cJSON_IsObject(extra)) {
        cJSON *feePayer = cJSON_GetObjectItem(extra, "feePayer");
        if (feePayer && cJSON_IsString(feePayer)) {
            strncpy(requirements_out->facilitator.fee_payer, feePayer->valuestring,
                    sizeof(requirements_out->facilitator.fee_payer) - 1);
        }
    }
    
    cJSON_Delete(root);
    
    requirements_out->valid = true;
    
    // Parse amount for logging (show both base units and USD equivalent)
    uint64_t amount_lamports = strtoull(requirements_out->price.amount, NULL, 10);
    double amount_usdc = (double)amount_lamports / 1000000.0; // USDC has 6 decimals
    
    ESP_LOGI(TAG, "Parsed payment requirements:");
    ESP_LOGI(TAG, "  Recipient: %s", requirements_out->recipient);
    ESP_LOGI(TAG, "  Network: %s", requirements_out->network);
    ESP_LOGI(TAG, "  Asset: %s", requirements_out->asset);
    ESP_LOGI(TAG, "  Amount: %s lamports (%.6f USDC)", 
             requirements_out->price.amount,
             amount_usdc);
    if (requirements_out->facilitator.fee_payer[0]) {
        ESP_LOGI(TAG, "  Fee Payer: %s", requirements_out->facilitator.fee_payer);
    }
    
    return ESP_OK;
}

const char* x402_get_facilitator_url(
    const x402_payment_requirements_t *requirements
) {
    if (!requirements || !requirements->valid) {
        return NULL;
    }
    
    if (requirements->facilitator.url[0] == '\0') {
        return NULL;
    }
    
    return requirements->facilitator.url;
}

esp_err_t x402_parse_price_to_amount(
    const x402_payment_requirements_t *requirements,
    uint8_t decimals,
    uint64_t *amount_out
) {
    if (!requirements || !amount_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!requirements->valid) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Parse amount string as integer
    // The amount is already in the token's smallest unit
    *amount_out = strtoull(requirements->price.amount, NULL, 10);
    
    ESP_LOGD(TAG, "Parsed price: %llu (with %d decimals)", *amount_out, decimals);
    
    return ESP_OK;
}

esp_err_t x402_query_fee_payer(
    const char *facilitator_url,
    const char *network,
    char *fee_payer_out,
    size_t max_len
) {
    if (!facilitator_url || !network || !fee_payer_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Querying facilitator /supported for fee payer...");
    
    // Build supported endpoint URL
    char url[512];
    snprintf(url, sizeof(url), "%s/supported", facilitator_url);
    
    // Setup HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "/supported returned status %d", status);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    
    // Read response
    int content_length = esp_http_client_get_content_length(client);
    if (content_length <= 0 || content_length > 4096) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_length);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    
    char *response = malloc(content_length + 1);
    if (!response) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    
    int read_len = esp_http_client_read(client, response, content_length);
    esp_http_client_cleanup(client);
    
    if (read_len != content_length) {
        ESP_LOGW(TAG, "Read length mismatch: %d != %d", read_len, content_length);
    }
    response[read_len] = '\0';
    
    ESP_LOGD(TAG, "/supported response: %s", response);
    
    // Parse JSON response
    // Expected: {"kinds":[{"x402Version":1,"scheme":"exact","network":"solana-devnet","extra":{"feePayer":"..."}}]}
    cJSON *root = cJSON_Parse(response);
    free(response);
    
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse /supported JSON");
        return ESP_FAIL;
    }
    
    cJSON *kinds = cJSON_GetObjectItem(root, "kinds");
    if (!cJSON_IsArray(kinds) || cJSON_GetArraySize(kinds) == 0) {
        ESP_LOGE(TAG, "No kinds in /supported response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Find matching kind for our network
    bool found = false;
    cJSON *kind = NULL;
    cJSON_ArrayForEach(kind, kinds) {
        cJSON *kind_network = cJSON_GetObjectItem(kind, "network");
        if (cJSON_IsString(kind_network) && 
            strcmp(kind_network->valuestring, network) == 0) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        ESP_LOGE(TAG, "Network %s not supported by facilitator", network);
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Extract feePayer from extra
    cJSON *extra = cJSON_GetObjectItem(kind, "extra");
    cJSON *fee_payer_json = cJSON_GetObjectItem(extra, "feePayer");
    
    if (!cJSON_IsString(fee_payer_json)) {
        ESP_LOGE(TAG, "No feePayer in /supported response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Copy fee payer address
    strncpy(fee_payer_out, fee_payer_json->valuestring, max_len - 1);
    fee_payer_out[max_len - 1] = '\0';
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Fee payer from /supported: %s", fee_payer_out);
    
    return ESP_OK;
}


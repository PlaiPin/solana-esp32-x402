#include "x402_requirements.h"
#include "esp_log.h"
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
    
    // Parse "recipient"
    cJSON *recipient = cJSON_GetObjectItem(root, "recipient");
    if (recipient && cJSON_IsString(recipient)) {
        strncpy(requirements_out->recipient, recipient->valuestring,
                sizeof(requirements_out->recipient) - 1);
    } else {
        ESP_LOGE(TAG, "Missing or invalid 'recipient' field");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Parse "network"
    cJSON *network = cJSON_GetObjectItem(root, "network");
    if (network && cJSON_IsString(network)) {
        strncpy(requirements_out->network, network->valuestring,
                sizeof(requirements_out->network) - 1);
    } else {
        ESP_LOGW(TAG, "Missing 'network' field, assuming solana-devnet");
        strncpy(requirements_out->network, X402_NETWORK_SOLANA_DEVNET,
                sizeof(requirements_out->network) - 1);
    }
    
    // Parse "price" object
    cJSON *price = cJSON_GetObjectItem(root, "price");
    if (price && cJSON_IsObject(price)) {
        // Parse "amount"
        cJSON *amount = cJSON_GetObjectItem(price, "amount");
        if (amount && cJSON_IsString(amount)) {
            strncpy(requirements_out->price.amount, amount->valuestring,
                    sizeof(requirements_out->price.amount) - 1);
        } else {
            ESP_LOGE(TAG, "Missing or invalid 'price.amount' field");
            cJSON_Delete(root);
            return ESP_FAIL;
        }
        
        // Parse "currency"
        cJSON *currency = cJSON_GetObjectItem(price, "currency");
        if (currency && cJSON_IsString(currency)) {
            strncpy(requirements_out->price.currency, currency->valuestring,
                    sizeof(requirements_out->price.currency) - 1);
        } else {
            ESP_LOGW(TAG, "Missing 'price.currency' field, assuming USDC");
            strncpy(requirements_out->price.currency, "USDC",
                    sizeof(requirements_out->price.currency) - 1);
        }
    } else {
        ESP_LOGE(TAG, "Missing or invalid 'price' object");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Parse "facilitator" object (optional)
    cJSON *facilitator = cJSON_GetObjectItem(root, "facilitator");
    if (facilitator && cJSON_IsObject(facilitator)) {
        cJSON *url = cJSON_GetObjectItem(facilitator, "url");
        if (url && cJSON_IsString(url)) {
            strncpy(requirements_out->facilitator.url, url->valuestring,
                    sizeof(requirements_out->facilitator.url) - 1);
        }
    }
    
    cJSON_Delete(root);
    
    requirements_out->valid = true;
    
    ESP_LOGI(TAG, "Parsed payment requirements:");
    ESP_LOGI(TAG, "  Recipient: %s", requirements_out->recipient);
    ESP_LOGI(TAG, "  Network: %s", requirements_out->network);
    ESP_LOGI(TAG, "  Amount: %s %s", 
             requirements_out->price.amount,
             requirements_out->price.currency);
    if (requirements_out->facilitator.url[0]) {
        ESP_LOGI(TAG, "  Facilitator: %s", requirements_out->facilitator.url);
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


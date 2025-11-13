#include "x402_encoding.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "x402_encoding";

char* x402_payload_to_json(const x402_payment_payload_t *payload) {
    if (!payload) {
        return NULL;
    }
    
    // Create root JSON object
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return NULL;
    }
    
    // Add fields at root level (FLAT structure)
    cJSON_AddNumberToObject(root, "x402Version", payload->x402_version);
    cJSON_AddStringToObject(root, "scheme", payload->scheme);
    cJSON_AddStringToObject(root, "network", payload->network);
    
    // Create "payload" object
    cJSON *network_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(network_payload, "transaction", payload->payload.transaction);
    cJSON_AddItemToObject(root, "payload", network_payload);
    
    // Serialize to string
    char *json_str = cJSON_PrintUnformatted(root);
    
    cJSON_Delete(root);
    
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return NULL;
    }
    
    ESP_LOGI(TAG, "Serialized PaymentPayload JSON (first 200 chars): %.200s", json_str);
    ESP_LOGI(TAG, "JSON length: %zu bytes", strlen(json_str));
    
    return json_str;
}

esp_err_t x402_parse_settlement_json(
    const char *json_str,
    x402_settlement_response_t *response_out
) {
    if (!json_str || !response_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(response_out, 0, sizeof(x402_settlement_response_t));
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse settlement JSON");
        return ESP_FAIL;
    }
    
    // Parse "transaction"
    cJSON *transaction = cJSON_GetObjectItem(root, "transaction");
    if (transaction && cJSON_IsString(transaction)) {
        strncpy(response_out->transaction, transaction->valuestring, 
                sizeof(response_out->transaction) - 1);
    }
    
    // Parse "success"
    cJSON *success = cJSON_GetObjectItem(root, "success");
    if (success && cJSON_IsBool(success)) {
        response_out->success = cJSON_IsTrue(success);
    }
    
    // Parse "network"
    cJSON *network = cJSON_GetObjectItem(root, "network");
    if (network && cJSON_IsString(network)) {
        strncpy(response_out->network, network->valuestring,
                sizeof(response_out->network) - 1);
    }
    
    cJSON_Delete(root);
    
    ESP_LOGD(TAG, "Parsed settlement: tx=%s, success=%d",
             response_out->transaction, response_out->success);
    
    return ESP_OK;
}

esp_err_t x402_base64_encode(
    const uint8_t *data,
    size_t data_len,
    char *encoded_out,
    size_t max_len,
    size_t *out_len
) {
    if (!data || !encoded_out || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t required_len;
    
    // Get required length
    int ret = mbedtls_base64_encode(NULL, 0, &required_len, data, data_len);
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        ESP_LOGE(TAG, "Failed to get base64 length");
        return ESP_FAIL;
    }
    
    if (required_len > max_len) {
        ESP_LOGE(TAG, "Base64 buffer too small: need %zu, have %zu",
                 required_len, max_len);
        return ESP_ERR_NO_MEM;
    }
    
    // Encode
    ret = mbedtls_base64_encode((unsigned char *)encoded_out, max_len, out_len,
                                data, data_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 encoding failed: %d", ret);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t x402_base64_decode(
    const char *encoded,
    uint8_t *decoded_out,
    size_t max_len,
    size_t *out_len
) {
    if (!encoded || !decoded_out || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int ret = mbedtls_base64_decode(decoded_out, max_len, out_len,
                                     (const unsigned char *)encoded,
                                     strlen(encoded));
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 decoding failed: %d", ret);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t x402_encode_payment_payload(
    const x402_payment_payload_t *payload,
    char *encoded_out,
    size_t max_len
) {
    if (!payload || !encoded_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Step 1: Serialize to JSON
    char *json_str = x402_payload_to_json(payload);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to serialize payload to JSON");
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "JSON payload: %s", json_str);
    
    // Step 2: Base64 encode
    size_t encoded_len;
    esp_err_t err = x402_base64_encode(
        (const uint8_t *)json_str, strlen(json_str),
        encoded_out, max_len, &encoded_len
    );
    
    free(json_str);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to base64 encode payload");
        return err;
    }
    
    ESP_LOGI(TAG, "Encoded payment payload: %zu bytes", encoded_len);
    ESP_LOGD(TAG, "Base64 (first 80 chars): %.80s", encoded_out);
    
    return ESP_OK;
}

esp_err_t x402_decode_settlement_response(
    const char *encoded_b64,
    x402_settlement_response_t *response_out
) {
    if (!encoded_b64 || !response_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Step 1: Base64 decode
    uint8_t decoded[2048];
    size_t decoded_len;
    
    esp_err_t err = x402_base64_decode(
        encoded_b64, decoded, sizeof(decoded), &decoded_len
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to base64 decode settlement response");
        return err;
    }
    
    // Null-terminate for JSON parsing
    decoded[decoded_len] = '\0';
    
    ESP_LOGD(TAG, "Decoded settlement JSON: %s", (char *)decoded);
    
    // Step 2: Parse JSON
    err = x402_parse_settlement_json((const char *)decoded, response_out);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse settlement JSON");
        return err;
    }
    
    ESP_LOGI(TAG, "Decoded settlement response: %s", response_out->transaction);
    
    return ESP_OK;
}


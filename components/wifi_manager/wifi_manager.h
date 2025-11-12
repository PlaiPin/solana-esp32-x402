#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi in station mode
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Connect to WiFi network
 * 
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @param timeout_ms Timeout in milliseconds (0 for no timeout)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password, uint32_t timeout_ms);

/**
 * @brief Disconnect from WiFi network
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief Check if WiFi is connected
 * 
 * @return true if connected, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get IP address as string
 * 
 * @param ip_str Buffer to store IP address string
 * @param max_len Maximum length of buffer
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_ip(char *ip_str, size_t max_len);

/**
 * @brief Deinitialize WiFi
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H


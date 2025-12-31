/*
 * Copyright (c) 2025, Adam G. Sweeney <agsweeney@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "system_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "system_config";
static const char *NVS_NAMESPACE = "system";
static const char *NVS_KEY_IPCONFIG = "ipconfig";
static const char *NVS_KEY_RS022 = "rs022";

void system_ip_config_get_defaults(system_ip_config_t *config)
{
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(system_ip_config_t));
    config->use_dhcp = true;  // Default to DHCP
    // All other fields are 0 (DHCP will assign)
}

bool system_ip_config_load(system_ip_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved IP configuration found, using defaults");
            system_ip_config_get_defaults(config);
            return false;
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    size_t required_size = sizeof(system_ip_config_t);
    err = nvs_get_blob(handle, NVS_KEY_IPCONFIG, config, &required_size);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved IP configuration found, using defaults");
        system_ip_config_get_defaults(config);
        return false;
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load IP configuration: %s", esp_err_to_name(err));
        return false;
    }
    
    if (required_size != sizeof(system_ip_config_t)) {
        ESP_LOGW(TAG, "IP configuration size mismatch (expected %zu, got %zu), using defaults",
                 sizeof(system_ip_config_t), required_size);
        system_ip_config_get_defaults(config);
        return false;
    }
    
    ESP_LOGI(TAG, "IP configuration loaded successfully from NVS (DHCP=%s)", 
             config->use_dhcp ? "enabled" : "disabled");
    return true;
}

bool system_ip_config_save(const system_ip_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_blob(handle, NVS_KEY_IPCONFIG, config, sizeof(system_ip_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save IP configuration: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit IP configuration: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "IP configuration saved successfully to NVS");
    return true;
}

bool system_motoman_rs022_load(bool *instance_direct)
{
    if (instance_direct == NULL) {
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            *instance_direct = false;
            return false;
        }
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    uint8_t value = 0;
    err = nvs_get_u8(handle, NVS_KEY_RS022, &value);
    nvs_close(handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *instance_direct = false;
        return false;
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load RS022 config: %s", esp_err_to_name(err));
        return false;
    }
    
    *instance_direct = (value != 0);
    ESP_LOGI(TAG, "RS022 instance mapping loaded (direct=%s)", *instance_direct ? "true" : "false");
    return true;
}

bool system_motoman_rs022_save(bool instance_direct)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    uint8_t value = instance_direct ? 1 : 0;
    err = nvs_set_u8(handle, NVS_KEY_RS022, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save RS022 config: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit RS022 config: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "RS022 instance mapping saved (direct=%s)", instance_direct ? "true" : "false");
    return true;
}

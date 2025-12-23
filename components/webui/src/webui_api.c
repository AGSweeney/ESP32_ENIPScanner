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

#include "webui_api.h"
#include "enip_scanner.h"
#include "system_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif.h"
#include "sdkconfig.h"
#include <cJSON.h>
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "webui_api";

// Helper function to send JSON response
static esp_err_t send_json_response(httpd_req_t *req, cJSON *json, esp_err_t http_status)
{
    char *json_str = cJSON_Print(json);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, http_status == ESP_OK ? HTTPD_200 : HTTPD_500);
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// Forward declarations
static esp_err_t api_scanner_register_session_handler(httpd_req_t *req);
static esp_err_t api_scanner_unregister_session_handler(httpd_req_t *req);
static esp_err_t api_network_config_get_handler(httpd_req_t *req);
static esp_err_t api_network_config_set_handler(httpd_req_t *req);
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
static esp_err_t api_scanner_read_tag_handler(httpd_req_t *req);
static esp_err_t api_scanner_write_tag_handler(httpd_req_t *req);
#endif

// GET /api/scanner/scan
static esp_err_t api_scanner_scan_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /api/scanner/scan");
    
    cJSON *response = cJSON_CreateObject();
    cJSON *devices = cJSON_CreateArray();
    
    // Allocate device list on heap to avoid stack overflow
    const int max_devices = 32;
    enip_scanner_device_info_t *device_list = malloc(max_devices * sizeof(enip_scanner_device_info_t));
    if (device_list == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for device list");
        cJSON_Delete(response);
        cJSON_Delete(devices);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }
    
    int device_count = enip_scanner_scan_devices(device_list, max_devices, 5000);
    
    for (int i = 0; i < device_count; i++) {
        cJSON *device = cJSON_CreateObject();
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&device_list[i].ip_address));
        
        cJSON_AddStringToObject(device, "ip_address", ip_str);
        cJSON_AddNumberToObject(device, "vendor_id", device_list[i].vendor_id);
        cJSON_AddNumberToObject(device, "device_type", device_list[i].device_type);
        cJSON_AddNumberToObject(device, "product_code", device_list[i].product_code);
        cJSON_AddNumberToObject(device, "major_revision", device_list[i].major_revision);
        cJSON_AddNumberToObject(device, "minor_revision", device_list[i].minor_revision);
        cJSON_AddNumberToObject(device, "status", device_list[i].status);
        cJSON_AddNumberToObject(device, "serial_number", device_list[i].serial_number);
        cJSON_AddStringToObject(device, "product_name", device_list[i].product_name);
        cJSON_AddBoolToObject(device, "online", device_list[i].online);
        cJSON_AddNumberToObject(device, "response_time_ms", device_list[i].response_time_ms);
        
        cJSON_AddItemToArray(devices, device);
    }
    
    free(device_list);  // Free heap allocation
    
    cJSON_AddItemToObject(response, "devices", devices);
    cJSON_AddNumberToObject(response, "count", device_count);
    cJSON_AddStringToObject(response, "status", "ok");
    
    return send_json_response(req, response, ESP_OK);
}

// POST /api/scanner/read-assembly
static esp_err_t api_scanner_read_assembly_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/read-assembly");
    
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ip_item = cJSON_GetObjectItem(json, "ip_address");
    cJSON *instance_item = cJSON_GetObjectItem(json, "assembly_instance");
    
    if (ip_item == NULL || instance_item == NULL || 
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(instance_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid parameters");
        return ESP_FAIL;
    }
    
    ip4_addr_t ip_addr;
    if (!inet_aton(ip_item->valuestring, &ip_addr)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid IP address");
        return ESP_FAIL;
    }
    
    uint16_t assembly_instance = (uint16_t)instance_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    enip_scanner_assembly_result_t result;
    esp_err_t err = enip_scanner_read_assembly(&ip_addr, assembly_instance, &result, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK && result.success) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&result.ip_address));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddNumberToObject(response, "assembly_instance", result.assembly_instance);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "data_length", result.data_length);
        cJSON_AddNumberToObject(response, "response_time_ms", result.response_time_ms);
        
        // Convert binary data to hex string
        if (result.data != NULL && result.data_length > 0) {
            char *hex_data = malloc(result.data_length * 2 + 1);
            if (hex_data != NULL) {
                for (uint16_t i = 0; i < result.data_length; i++) {
                    snprintf(&hex_data[i * 2], 3, "%02x", result.data[i]);
                }
                hex_data[result.data_length * 2] = '\0';
                cJSON_AddStringToObject(response, "data_hex", hex_data);
                free(hex_data);
            }
            
            // Also provide as base64 or raw bytes array
            cJSON *data_array = cJSON_CreateArray();
            for (uint16_t i = 0; i < result.data_length; i++) {
                cJSON_AddItemToArray(data_array, cJSON_CreateNumber(result.data[i]));
            }
            cJSON_AddItemToObject(response, "data", data_array);
        }
        
        cJSON_AddStringToObject(response, "status", "ok");
        enip_scanner_free_assembly_result(&result);
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&result.ip_address));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddNumberToObject(response, "assembly_instance", result.assembly_instance);
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", result.error_message);
        cJSON_AddStringToObject(response, "status", "error");
        
        enip_scanner_free_assembly_result(&result);
        return send_json_response(req, response, ESP_FAIL);
    }
}

// POST /api/scanner/write-assembly
static esp_err_t api_scanner_write_assembly_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/write-assembly");
    
    char content[2048];  // Increased size for larger data payloads
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ip_item = cJSON_GetObjectItem(json, "ip_address");
    cJSON *instance_item = cJSON_GetObjectItem(json, "assembly_instance");
    cJSON *data_item = cJSON_GetObjectItem(json, "data");
    
    if (ip_item == NULL || instance_item == NULL || data_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(instance_item) || !cJSON_IsArray(data_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid parameters");
        return ESP_FAIL;
    }
    
    ip4_addr_t ip_addr;
    if (!inet_aton(ip_item->valuestring, &ip_addr)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid IP address");
        return ESP_FAIL;
    }
    
    uint16_t assembly_instance = (uint16_t)instance_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    // Extract data array
    int data_array_size = cJSON_GetArraySize(data_item);
    if (data_array_size <= 0 || data_array_size > 1024) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid data array size");
        return ESP_FAIL;
    }
    
    uint8_t *write_data = malloc(data_array_size);
    if (write_data == NULL) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }
    
    for (int i = 0; i < data_array_size; i++) {
        cJSON *byte_item = cJSON_GetArrayItem(data_item, i);
        if (byte_item == NULL || !cJSON_IsNumber(byte_item)) {
            free(write_data);
            cJSON_Delete(json);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid data byte");
            return ESP_FAIL;
        }
        write_data[i] = (uint8_t)(byte_item->valueint & 0xFF);
    }
    
    cJSON_Delete(json);
    
    char error_message[128];
    esp_err_t err = enip_scanner_write_assembly(&ip_addr, assembly_instance, write_data, data_array_size, timeout_ms, error_message);
    
    free(write_data);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddNumberToObject(response, "assembly_instance", assembly_instance);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "status", "ok");
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddNumberToObject(response, "assembly_instance", assembly_instance);
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", error_message);
        cJSON_AddStringToObject(response, "status", "error");
        return send_json_response(req, response, ESP_FAIL);
    }
}

// POST /api/scanner/check-writable
static esp_err_t api_scanner_check_writable_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/check-writable");
    
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ip_item = cJSON_GetObjectItem(json, "ip_address");
    cJSON *instance_item = cJSON_GetObjectItem(json, "assembly_instance");
    
    if (ip_item == NULL || instance_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(instance_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid parameters");
        return ESP_FAIL;
    }
    
    ip4_addr_t ip_addr;
    if (!inet_aton(ip_item->valuestring, &ip_addr)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid IP address");
        return ESP_FAIL;
    }
    
    uint16_t assembly_instance = (uint16_t)instance_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    bool writable = enip_scanner_is_assembly_writable(&ip_addr, assembly_instance, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
    
    cJSON_AddStringToObject(response, "ip_address", ip_str);
    cJSON_AddNumberToObject(response, "assembly_instance", assembly_instance);
    cJSON_AddBoolToObject(response, "writable", writable);
    cJSON_AddStringToObject(response, "status", "ok");
    
    return send_json_response(req, response, ESP_OK);
}

// POST /api/scanner/discover-assemblies
static esp_err_t api_scanner_discover_assemblies_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/discover-assemblies");
    
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ip_item = cJSON_GetObjectItem(json, "ip_address");
    
    if (ip_item == NULL || !cJSON_IsString(ip_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid IP address");
        return ESP_FAIL;
    }
    
    ip4_addr_t ip_addr;
    if (!inet_aton(ip_item->valuestring, &ip_addr)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid IP address");
        return ESP_FAIL;
    }
    
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    // Discover assemblies (limit to 32 instances)
    uint16_t discovered_instances[32];
    int count = enip_scanner_discover_assemblies(&ip_addr, discovered_instances, 32, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
    
    cJSON_AddStringToObject(response, "ip_address", ip_str);
    cJSON_AddNumberToObject(response, "count", count);
    
    cJSON *instances_array = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(instances_array, cJSON_CreateNumber(discovered_instances[i]));
    }
    cJSON_AddItemToObject(response, "instances", instances_array);
    cJSON_AddStringToObject(response, "status", "ok");
    
    return send_json_response(req, response, ESP_OK);
}

// POST /api/scanner/register-session
static esp_err_t api_scanner_register_session_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/register-session");
    
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ip_item = cJSON_GetObjectItem(json, "ip_address");
    if (ip_item == NULL || !cJSON_IsString(ip_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid IP address");
        return ESP_FAIL;
    }
    
    ip4_addr_t ip_addr;
    if (!inet_aton(ip_item->valuestring, &ip_addr)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid IP address");
        return ESP_FAIL;
    }
    
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    uint32_t session_handle = 0;
    char error_msg[128];
    esp_err_t err = enip_scanner_register_session(&ip_addr, &session_handle, timeout_ms, error_msg);
    
    cJSON *response = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddNumberToObject(response, "session_handle", session_handle);
    } else {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", error_msg);
    }
    
    return send_json_response(req, response, err);
}

// POST /api/scanner/unregister-session
static esp_err_t api_scanner_unregister_session_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/unregister-session");
    
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ip_item = cJSON_GetObjectItem(json, "ip_address");
    cJSON *session_item = cJSON_GetObjectItem(json, "session_handle");
    
    if (ip_item == NULL || !cJSON_IsString(ip_item) || 
        session_item == NULL || !cJSON_IsNumber(session_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid parameters");
        return ESP_FAIL;
    }
    
    ip4_addr_t ip_addr;
    if (!inet_aton(ip_item->valuestring, &ip_addr)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid IP address");
        return ESP_FAIL;
    }
    
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    uint32_t session_handle = (uint32_t)session_item->valueint;
    cJSON_Delete(json);
    
    esp_err_t err = enip_scanner_unregister_session(&ip_addr, session_handle, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(response, "status", "ok");
    } else {
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", esp_err_to_name(err));
    }
    
    return send_json_response(req, response, err);
}

// GET /api/status
static esp_err_t api_status_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /api/status");
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "service", "EtherNet/IP Scanner");
    cJSON_AddStringToObject(response, "version", "1.0.0");
    
    return send_json_response(req, response, ESP_OK);
}

#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT

// POST /api/scanner/read-tag
static esp_err_t api_scanner_read_tag_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/read-tag");
    
    // Get content length
    size_t content_len = req->content_len;
    if (content_len == 0 || content_len > 1024) {
        ESP_LOGE(TAG, "Invalid content length: %zu", content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }
    
    // Allocate buffer for request body
    char *content = malloc(content_len + 1);
    if (content == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for request body");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }
    
    // Read full request body
    int total_received = 0;
    int ret = 0;
    while (total_received < content_len) {
        ret = httpd_req_recv(req, content + total_received, content_len - total_received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "Timeout receiving request body");
                free(content);
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Request timeout");
                return ESP_FAIL;
            }
            ESP_LOGE(TAG, "Error receiving request body: %d", ret);
            free(content);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
            return ESP_FAIL;
        }
        total_received += ret;
    }
    content[content_len] = '\0';
    
    ESP_LOGD(TAG, "Received request body: %s", content);
    
    cJSON *json = cJSON_Parse(content);
    free(content);
    
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ip_item = cJSON_GetObjectItem(json, "ip_address");
    cJSON *tag_path_item = cJSON_GetObjectItem(json, "tag_path");
    
    if (ip_item == NULL || tag_path_item == NULL || 
        !cJSON_IsString(ip_item) || !cJSON_IsString(tag_path_item)) {
        ESP_LOGE(TAG, "Missing or invalid parameters");
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid parameters");
        return ESP_FAIL;
    }
    
    ip4_addr_t ip_addr;
    if (!inet_aton(ip_item->valuestring, &ip_addr)) {
        ESP_LOGE(TAG, "Invalid IP address: %s", ip_item->valuestring);
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid IP address");
        return ESP_FAIL;
    }
    
    const char *tag_path_json = tag_path_item->valuestring;
    const char *ip_str_param = ip_item->valuestring;
    
    // Copy tag path before deleting JSON (cJSON strings are part of JSON object)
    char tag_path[128];
    strncpy(tag_path, tag_path_json, sizeof(tag_path) - 1);
    tag_path[sizeof(tag_path) - 1] = '\0';
    
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    ESP_LOGI(TAG, "Reading tag '%s' from %s with timeout %lu ms", tag_path, ip_str_param, timeout_ms);
    
    cJSON_Delete(json);
    
    enip_scanner_tag_result_t result;
    memset(&result, 0, sizeof(result));
    esp_err_t err = enip_scanner_read_tag(&ip_addr, tag_path, &result, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    if (response == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON response");
        enip_scanner_free_tag_result(&result);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create response");
        return ESP_ERR_NO_MEM;
    }
    
    if (err == ESP_OK && result.success) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&result.ip_address));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddStringToObject(response, "tag_path", result.tag_path);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "data_length", result.data_length);
        cJSON_AddNumberToObject(response, "cip_data_type", result.cip_data_type);
        cJSON_AddStringToObject(response, "data_type_name", enip_scanner_get_data_type_name(result.cip_data_type));
        cJSON_AddNumberToObject(response, "response_time_ms", result.response_time_ms);
        
        // Convert binary data to hex string
        if (result.data != NULL && result.data_length > 0) {
            char *hex_data = malloc(result.data_length * 2 + 1);
            if (hex_data != NULL) {
                for (uint16_t i = 0; i < result.data_length; i++) {
                    snprintf(&hex_data[i * 2], 3, "%02x", result.data[i]);
                }
                hex_data[result.data_length * 2] = '\0';
                cJSON_AddStringToObject(response, "data_hex", hex_data);
                free(hex_data);
            }
            
            // Also provide as array
            cJSON *data_array = cJSON_CreateArray();
            for (uint16_t i = 0; i < result.data_length; i++) {
                cJSON_AddItemToArray(data_array, cJSON_CreateNumber(result.data[i]));
            }
            cJSON_AddItemToObject(response, "data", data_array);
            
            // Try to interpret common data types
            if (result.cip_data_type == CIP_DATA_TYPE_BOOL && result.data_length >= 1) {
                cJSON_AddBoolToObject(response, "value_bool", (result.data[0] != 0));
            } else if (result.cip_data_type == CIP_DATA_TYPE_SINT && result.data_length >= 1) {
                int8_t val = (int8_t)result.data[0];
                cJSON_AddNumberToObject(response, "value_sint", val);
            } else if (result.cip_data_type == CIP_DATA_TYPE_INT && result.data_length >= 2) {
                int16_t val = (result.data[0] | (result.data[1] << 8));
                cJSON_AddNumberToObject(response, "value_int", val);
            } else if (result.cip_data_type == CIP_DATA_TYPE_DINT && result.data_length >= 4) {
                int32_t val = (result.data[0] | (result.data[1] << 8) | 
                               (result.data[2] << 16) | (result.data[3] << 24));
                cJSON_AddNumberToObject(response, "value_dint", val);
            } else if (result.cip_data_type == CIP_DATA_TYPE_REAL && result.data_length >= 4) {
                // REAL is IEEE 754 single precision (little-endian)
                union {
                    uint32_t i;
                    float f;
                } u;
                u.i = (result.data[0] | (result.data[1] << 8) | 
                       (result.data[2] << 16) | (result.data[3] << 24));
                cJSON_AddNumberToObject(response, "value_real", u.f);
            } else if (result.cip_data_type == CIP_DATA_TYPE_STRING && result.data_length > 0) {
                // STRING format: [Length byte] [String bytes]
                uint8_t str_length = result.data[0];  // First byte is length
                if (str_length > 0 && result.data_length >= (1 + str_length)) {
                    // Allocate buffer for null-terminated string
                    char *str_buffer = malloc(str_length + 1);
                    if (str_buffer != NULL) {
                        // Copy string bytes (skip length byte)
                        memcpy(str_buffer, result.data + 1, str_length);
                        str_buffer[str_length] = '\0';  // Null terminate
                        cJSON_AddStringToObject(response, "value_string", str_buffer);
                        free(str_buffer);
                    }
                }
            }
        }
        
        cJSON_AddStringToObject(response, "status", "ok");
        enip_scanner_free_tag_result(&result);
        return send_json_response(req, response, ESP_OK);
    } else {
        // Handle error case
        char ip_str[16];
        if (err == ESP_OK) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&result.ip_address));
        } else {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        }
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        if (result.tag_path[0] != '\0') {
            cJSON_AddStringToObject(response, "tag_path", result.tag_path);
        } else {
            cJSON_AddStringToObject(response, "tag_path", tag_path);
        }
        cJSON_AddBoolToObject(response, "success", false);
        
        const char *error_msg = (result.error_message[0] != '\0') ? result.error_message : "Unknown error";
        ESP_LOGE(TAG, "Tag read failed: %s (err=%d)", error_msg, err);
        cJSON_AddStringToObject(response, "error", error_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        enip_scanner_free_tag_result(&result);
        // Return HTTP 200 with error JSON (not 500) - CIP errors are client/request errors, not server errors
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/write-tag
static esp_err_t api_scanner_write_tag_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/write-tag");
    
    char content[2048];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ip_item = cJSON_GetObjectItem(json, "ip_address");
    cJSON *tag_path_item = cJSON_GetObjectItem(json, "tag_path");
    cJSON *cip_data_type_item = cJSON_GetObjectItem(json, "cip_data_type");
    cJSON *data_item = cJSON_GetObjectItem(json, "data");
    
    if (ip_item == NULL || tag_path_item == NULL || cip_data_type_item == NULL || data_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsString(tag_path_item) || 
        !cJSON_IsNumber(cip_data_type_item) || !cJSON_IsArray(data_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid parameters");
        return ESP_FAIL;
    }
    
    ip4_addr_t ip_addr;
    if (!inet_aton(ip_item->valuestring, &ip_addr)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid IP address");
        return ESP_FAIL;
    }
    
    // Copy tag_path before deleting JSON (cJSON strings are part of JSON object)
    char tag_path[128];
    strncpy(tag_path, tag_path_item->valuestring, sizeof(tag_path) - 1);
    tag_path[sizeof(tag_path) - 1] = '\0';
    
    uint16_t cip_data_type = (uint16_t)cip_data_type_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    // Extract data array
    int data_array_size = cJSON_GetArraySize(data_item);
    if (data_array_size <= 0 || data_array_size > 1024) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid data array size");
        return ESP_FAIL;
    }
    
    uint8_t *write_data = malloc(data_array_size);
    if (write_data == NULL) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }
    
    for (int i = 0; i < data_array_size; i++) {
        cJSON *byte_item = cJSON_GetArrayItem(data_item, i);
        if (byte_item == NULL || !cJSON_IsNumber(byte_item)) {
            free(write_data);
            cJSON_Delete(json);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid data byte");
            return ESP_FAIL;
        }
        write_data[i] = (uint8_t)(byte_item->valueint & 0xFF);
    }
    
    cJSON_Delete(json);  // Safe to delete now - tag_path is copied
    
    char error_message[128];
    esp_err_t err = enip_scanner_write_tag(&ip_addr, tag_path, write_data, data_array_size, 
                                          cip_data_type, timeout_ms, error_message);
    
    free(write_data);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddStringToObject(response, "tag_path", tag_path);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "status", "ok");
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddStringToObject(response, "tag_path", tag_path);
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", error_message);
        cJSON_AddStringToObject(response, "status", "error");
        // Return HTTP 200 with error JSON (not 500) - CIP errors are client/request errors, not server errors
        return send_json_response(req, response, ESP_OK);
    }
}

#endif // CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT

// GET /api/network/config
static esp_err_t api_network_config_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /api/network/config");
    
    system_ip_config_t config;
    bool loaded = system_ip_config_load(&config);
    
    // If not loaded from NVS, use defaults (DHCP)
    if (!loaded) {
        system_ip_config_get_defaults(&config);
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "use_dhcp", config.use_dhcp);
    
    char ip_str[16];
    if (config.ip_address != 0) {
        ip4_addr_t ip;
        ip.addr = config.ip_address;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip));
        cJSON_AddStringToObject(response, "ip_address", ip_str);
    } else {
        cJSON_AddStringToObject(response, "ip_address", "");
    }
    
    if (config.netmask != 0) {
        ip4_addr_t nm;
        nm.addr = config.netmask;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&nm));
        cJSON_AddStringToObject(response, "netmask", ip_str);
    } else {
        cJSON_AddStringToObject(response, "netmask", "");
    }
    
    if (config.gateway != 0) {
        ip4_addr_t gw;
        gw.addr = config.gateway;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&gw));
        cJSON_AddStringToObject(response, "gateway", ip_str);
    } else {
        cJSON_AddStringToObject(response, "gateway", "");
    }
    
    if (config.dns1 != 0) {
        ip4_addr_t dns;
        dns.addr = config.dns1;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&dns));
        cJSON_AddStringToObject(response, "dns1", ip_str);
    } else {
        cJSON_AddStringToObject(response, "dns1", "");
    }
    
    if (config.dns2 != 0) {
        ip4_addr_t dns;
        dns.addr = config.dns2;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&dns));
        cJSON_AddStringToObject(response, "dns2", ip_str);
    } else {
        cJSON_AddStringToObject(response, "dns2", "");
    }
    
    // Also include current running IP configuration (useful when switching from DHCP to Static)
    struct netif *netif = netif_default;
    if (netif != NULL && netif_is_up(netif)) {
        const ip4_addr_t *current_ip = netif_ip4_addr(netif);
        const ip4_addr_t *current_netmask = netif_ip4_netmask(netif);
        const ip4_addr_t *current_gw = netif_ip4_gw(netif);
        
        if (current_ip != NULL && current_ip->addr != 0) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(current_ip));
            cJSON_AddStringToObject(response, "current_ip_address", ip_str);
        }
        
        if (current_netmask != NULL && current_netmask->addr != 0) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(current_netmask));
            cJSON_AddStringToObject(response, "current_netmask", ip_str);
        }
        
        if (current_gw != NULL && current_gw->addr != 0) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(current_gw));
            cJSON_AddStringToObject(response, "current_gateway", ip_str);
        }
    }
    
    return send_json_response(req, response, ESP_OK);
}

// POST /api/network/config
static esp_err_t api_network_config_set_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/network/config");
    
    size_t content_len = req->content_len;
    if (content_len == 0 || content_len > 1024) {
        ESP_LOGE(TAG, "Invalid request body size: %zu", content_len);
        return send_json_response(req, cJSON_CreateString("Invalid request body size"), HTTPD_400_BAD_REQUEST);
    }
    
    char *content = malloc(content_len + 1);
    if (content == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for request body");
        return send_json_response(req, cJSON_CreateString("Out of memory"), HTTPD_500_INTERNAL_SERVER_ERROR);
    }
    
    int total_received = 0;
    while (total_received < content_len) {
        int ret = httpd_req_recv(req, content + total_received, content_len - total_received);
        if (ret <= 0) {
            ESP_LOGE(TAG, "Failed to receive request body: %d", ret);
            free(content);
            return send_json_response(req, cJSON_CreateString("Invalid request body"), HTTPD_400_BAD_REQUEST);
        }
        total_received += ret;
    }
    content[content_len] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    free(content);
    
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return send_json_response(req, cJSON_CreateString("Invalid JSON"), HTTPD_400_BAD_REQUEST);
    }
    
    system_ip_config_t config;
    memset(&config, 0, sizeof(config));
    
    cJSON *use_dhcp_item = cJSON_GetObjectItem(json, "use_dhcp");
    if (use_dhcp_item != NULL && cJSON_IsBool(use_dhcp_item)) {
        config.use_dhcp = cJSON_IsTrue(use_dhcp_item);
    } else {
        config.use_dhcp = true;  // Default to DHCP
    }
    
    if (!config.use_dhcp) {
        // Parse static IP configuration
        cJSON *ip_item = cJSON_GetObjectItem(json, "ip_address");
        cJSON *netmask_item = cJSON_GetObjectItem(json, "netmask");
        cJSON *gateway_item = cJSON_GetObjectItem(json, "gateway");
        cJSON *dns1_item = cJSON_GetObjectItem(json, "dns1");
        cJSON *dns2_item = cJSON_GetObjectItem(json, "dns2");
        
        ip4_addr_t ip_addr;
        if (ip_item != NULL && cJSON_IsString(ip_item)) {
            if (inet_aton(ip_item->valuestring, &ip_addr)) {
                config.ip_address = ip_addr.addr;
            } else {
                cJSON_Delete(json);
                return send_json_response(req, cJSON_CreateString("Invalid IP address"), HTTPD_400_BAD_REQUEST);
            }
        }
        
        if (netmask_item != NULL && cJSON_IsString(netmask_item)) {
            if (inet_aton(netmask_item->valuestring, &ip_addr)) {
                config.netmask = ip_addr.addr;
            }
        }
        
        if (gateway_item != NULL && cJSON_IsString(gateway_item)) {
            if (inet_aton(gateway_item->valuestring, &ip_addr)) {
                config.gateway = ip_addr.addr;
            }
        }
        
        if (dns1_item != NULL && cJSON_IsString(dns1_item)) {
            if (inet_aton(dns1_item->valuestring, &ip_addr)) {
                config.dns1 = ip_addr.addr;
            }
        }
        
        if (dns2_item != NULL && cJSON_IsString(dns2_item)) {
            if (inet_aton(dns2_item->valuestring, &ip_addr)) {
                config.dns2 = ip_addr.addr;
            }
        }
    }
    
    cJSON_Delete(json);
    
    // Save to NVS
    if (system_ip_config_save(&config)) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "Configuration saved successfully. Please restart the device for changes to take effect.");
        return send_json_response(req, response, ESP_OK);
    } else {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", "Failed to save configuration");
        return send_json_response(req, response, HTTPD_500_INTERNAL_SERVER_ERROR);
    }
}

esp_err_t webui_api_register(httpd_handle_t server)
{
    httpd_uri_t scanner_scan_uri = {
        .uri = "/api/scanner/scan",
        .method = HTTP_GET,
        .handler = api_scanner_scan_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_scan_uri);
    
    httpd_uri_t scanner_read_assembly_uri = {
        .uri = "/api/scanner/read-assembly",
        .method = HTTP_POST,
        .handler = api_scanner_read_assembly_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_read_assembly_uri);
    
    httpd_uri_t scanner_write_assembly_uri = {
        .uri = "/api/scanner/write-assembly",
        .method = HTTP_POST,
        .handler = api_scanner_write_assembly_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_write_assembly_uri);
    
    httpd_uri_t scanner_check_writable_uri = {
        .uri = "/api/scanner/check-writable",
        .method = HTTP_POST,
        .handler = api_scanner_check_writable_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_check_writable_uri);
    
    httpd_uri_t scanner_discover_assemblies_uri = {
        .uri = "/api/scanner/discover-assemblies",
        .method = HTTP_POST,
        .handler = api_scanner_discover_assemblies_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_discover_assemblies_uri);
    
    // Session Management API endpoints
    httpd_uri_t scanner_register_session_uri = {
        .uri = "/api/scanner/register-session",
        .method = HTTP_POST,
        .handler = api_scanner_register_session_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_register_session_uri);
    
    httpd_uri_t scanner_unregister_session_uri = {
        .uri = "/api/scanner/unregister-session",
        .method = HTTP_POST,
        .handler = api_scanner_unregister_session_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_unregister_session_uri);
    
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &status_uri);
    
    // Network configuration API endpoints
    httpd_uri_t network_config_get_uri = {
        .uri = "/api/network/config",
        .method = HTTP_GET,
        .handler = api_network_config_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &network_config_get_uri);
    
    httpd_uri_t network_config_set_uri = {
        .uri = "/api/network/config",
        .method = HTTP_POST,
        .handler = api_network_config_set_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &network_config_set_uri);
    
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
    httpd_uri_t scanner_read_tag_uri = {
        .uri = "/api/scanner/read-tag",
        .method = HTTP_POST,
        .handler = api_scanner_read_tag_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_read_tag_uri);
    
    httpd_uri_t scanner_write_tag_uri = {
        .uri = "/api/scanner/write-tag",
        .method = HTTP_POST,
        .handler = api_scanner_write_tag_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_write_tag_uri);
#endif
    
    ESP_LOGI(TAG, "Web UI API endpoints registered");
    return ESP_OK;
}

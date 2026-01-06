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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
static esp_err_t api_scanner_implicit_open_handler(httpd_req_t *req);
static esp_err_t api_scanner_implicit_close_handler(httpd_req_t *req);
static esp_err_t api_scanner_implicit_write_data_handler(httpd_req_t *req);
static esp_err_t api_scanner_implicit_status_handler(httpd_req_t *req);
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
static esp_err_t api_scanner_motoman_read_position_variable_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_alarm_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_status_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_job_info_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_axis_config_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_position_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_position_deviation_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_torque_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_io_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_register_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_variable_b_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_variable_i_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_variable_d_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_variable_r_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_read_variable_s_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_get_rs022_handler(httpd_req_t *req);
static esp_err_t api_scanner_motoman_set_rs022_handler(httpd_req_t *req);
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

#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT

// Global connection status storage (simplified - in production, use proper connection tracking)
static struct {
    bool is_open;
    ip4_addr_t ip_address;
    uint16_t assembly_instance_consumed;
    uint16_t assembly_instance_produced;
    uint32_t rpi_ms;
    uint16_t assembly_data_size_consumed;
    uint16_t assembly_data_size_produced;
    bool exclusive_owner;  // true = PTP (exclusive owner), false = non-PTP (non-exclusive owner)
    uint16_t last_received_length;  // Length of last received T->O packet (for status only)
    uint32_t last_packet_time;       // Timestamp of last received T->O packet
} implicit_connection_status = {0};

// Callback for receiving T-to-O data
// Note: We don't store the data here since it's read-only from the device
// The web UI can read it directly from the device when needed
static void implicit_data_callback(const ip4_addr_t *ip_address,
                                   uint16_t assembly_instance,
                                   const uint8_t *data,
                                   uint16_t data_length,
                                   void *user_data)
{
    // Just update the timestamp to indicate we're receiving data
    implicit_connection_status.last_packet_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    implicit_connection_status.last_received_length = data_length;
}

// POST /api/scanner/implicit/open
static esp_err_t api_scanner_implicit_open_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/implicit/open");
    
    char content[512];
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
    cJSON *consumed_item = cJSON_GetObjectItem(json, "assembly_instance_consumed");
    cJSON *produced_item = cJSON_GetObjectItem(json, "assembly_instance_produced");
    cJSON *consumed_size_item = cJSON_GetObjectItem(json, "assembly_data_size_consumed");
    cJSON *produced_size_item = cJSON_GetObjectItem(json, "assembly_data_size_produced");
    cJSON *rpi_item = cJSON_GetObjectItem(json, "rpi_ms");
    
    if (ip_item == NULL || consumed_item == NULL || produced_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(consumed_item) || !cJSON_IsNumber(produced_item)) {
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
    
    uint16_t assembly_consumed = (uint16_t)consumed_item->valueint;
    uint16_t assembly_produced = (uint16_t)produced_item->valueint;
    
    // Default assembly data sizes (0 = autodetect)
    uint16_t assembly_data_size_consumed = 0;  // 0 = autodetect
    uint16_t assembly_data_size_produced = 0;   // 0 = autodetect
    
    if (consumed_size_item != NULL && cJSON_IsNumber(consumed_size_item)) {
        assembly_data_size_consumed = (uint16_t)consumed_size_item->valueint;
        if (assembly_data_size_consumed > 500) {
            cJSON_Delete(json);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid assembly_data_size_consumed (0-500, 0=autodetect)");
            return ESP_FAIL;
        }
    }
    
    if (produced_size_item != NULL && cJSON_IsNumber(produced_size_item)) {
        assembly_data_size_produced = (uint16_t)produced_size_item->valueint;
        if (assembly_data_size_produced > 500) {
            cJSON_Delete(json);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid assembly_data_size_produced (0-500, 0=autodetect)");
            return ESP_FAIL;
        }
    }
    
    uint32_t rpi_ms = 200;  // Default RPI
    if (rpi_item != NULL && cJSON_IsNumber(rpi_item)) {
        rpi_ms = (uint32_t)rpi_item->valueint;
        if (rpi_ms < 10) rpi_ms = 10;
        if (rpi_ms > 10000) rpi_ms = 10000;
    }
    
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    // Parse exclusive_owner (default: true for PTP/exclusive owner)
    bool exclusive_owner = true;  // Default to PTP (exclusive owner)
    cJSON *exclusive_owner_item = cJSON_GetObjectItem(json, "exclusive_owner");
    if (exclusive_owner_item != NULL && cJSON_IsBool(exclusive_owner_item)) {
        exclusive_owner = cJSON_IsTrue(exclusive_owner_item);
    }
    
    cJSON_Delete(json);
    
    // Close existing connection if open
    if (implicit_connection_status.is_open) {
        ESP_LOGI(TAG, "Closing existing connection before opening new one");
        esp_err_t close_ret = enip_scanner_implicit_close(&implicit_connection_status.ip_address, timeout_ms);
        
        // Wait for device to fully release resources before opening new connection
        // Based on Wireshark analysis: Forward Close response is fast (~654us), but device
        // needs ~1.4 seconds to fully clean up before a new Forward Open can succeed.
        // If Forward Close didn't succeed, wait even longer (2 seconds) to ensure resources are released.
        if (close_ret == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1500));  // Wait 1.5 seconds if Forward Close succeeded
        } else {
            ESP_LOGW(TAG, "Forward Close may have failed - waiting longer before retry");
            vTaskDelay(pdMS_TO_TICKS(2500));  // Wait 2.5 seconds if Forward Close failed
        }
    }
    
    // Open new connection
    esp_err_t err = enip_scanner_implicit_open(&ip_addr, assembly_consumed, assembly_produced,
                                               assembly_data_size_consumed, assembly_data_size_produced,
                                               rpi_ms, implicit_data_callback, NULL, timeout_ms,
                                               exclusive_owner);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        // After successful open, we need to get the actual detected sizes
        // Since autodetection happens inside enip_scanner_implicit_open, we need to
        // read the assembly sizes again to get the actual values (if autodetection was used)
        uint16_t actual_consumed_size = assembly_data_size_consumed;
        uint16_t actual_produced_size = assembly_data_size_produced;
        
        // If autodetection was used (size = 0), we need to read the actual sizes
        // For now, we'll use the values from the status endpoint which should have the correct values
        // The connection structure stores the detected sizes, but we don't have direct access
        // So we'll update the status after the connection is established
        
        implicit_connection_status.is_open = true;
        implicit_connection_status.ip_address = ip_addr;
        implicit_connection_status.assembly_instance_consumed = assembly_consumed;
        implicit_connection_status.assembly_instance_produced = assembly_produced;
        // Store the provided sizes - if autodetection was used, these will be 0
        // We'll need to read them from the device or connection structure
        implicit_connection_status.assembly_data_size_consumed = actual_consumed_size;
        implicit_connection_status.assembly_data_size_produced = actual_produced_size;
        implicit_connection_status.rpi_ms = rpi_ms;
        implicit_connection_status.exclusive_owner = exclusive_owner;
        implicit_connection_status.last_received_length = 0;
        implicit_connection_status.last_packet_time = 0;
        
        // If autodetection was used (size = 0), read the actual detected sizes using read_assembly
        // The connection structure stores them internally, but we need to query them
        if (assembly_data_size_consumed == 0) {
            enip_scanner_assembly_result_t result = {0};
            if (enip_scanner_read_assembly(&ip_addr, assembly_consumed, &result, timeout_ms) == ESP_OK) {
                implicit_connection_status.assembly_data_size_consumed = result.data_length;
                enip_scanner_free_assembly_result(&result);
            }
        }
        if (assembly_data_size_produced == 0) {
            enip_scanner_assembly_result_t result = {0};
            if (enip_scanner_read_assembly(&ip_addr, assembly_produced, &result, timeout_ms) == ESP_OK) {
                implicit_connection_status.assembly_data_size_produced = result.data_length;
                enip_scanner_free_assembly_result(&result);
            }
        }
        
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddNumberToObject(response, "assembly_instance_consumed", assembly_consumed);
        cJSON_AddNumberToObject(response, "assembly_instance_produced", assembly_produced);
        cJSON_AddNumberToObject(response, "assembly_data_size_consumed", implicit_connection_status.assembly_data_size_consumed);
        cJSON_AddNumberToObject(response, "assembly_data_size_produced", implicit_connection_status.assembly_data_size_produced);
        cJSON_AddNumberToObject(response, "rpi_ms", rpi_ms);
        cJSON_AddBoolToObject(response, "exclusive_owner", exclusive_owner);
        
        // Read current O->T data from memory and include it in the response
        // This ensures the form is populated with the stored values when connection opens
        // Note: The initial O->T data is read from the device inside enip_scanner_implicit_open()
        // and stored in memory, so it should be available immediately after open succeeds
        uint8_t o_to_t_data[500];
        uint16_t o_to_t_length = 0;
        esp_err_t read_ret = enip_scanner_implicit_read_o_to_t_data(&ip_addr, o_to_t_data, &o_to_t_length, sizeof(o_to_t_data));
        if (read_ret == ESP_OK && o_to_t_length > 0) {
            cJSON *o_to_t_array = cJSON_CreateArray();
            for (uint16_t i = 0; i < o_to_t_length; i++) {
                cJSON_AddItemToArray(o_to_t_array, cJSON_CreateNumber(o_to_t_data[i]));
            }
            cJSON_AddItemToObject(response, "last_sent_data", o_to_t_array);
            cJSON_AddNumberToObject(response, "last_sent_length", o_to_t_length);
        } else {
            // If read failed or returned no data, try reading directly from the device as fallback
            // This can happen if the initial read inside enip_scanner_implicit_open() failed
            enip_scanner_assembly_result_t assembly_result = {0};
            if (enip_scanner_read_assembly(&ip_addr, assembly_consumed, &assembly_result, timeout_ms) == ESP_OK && 
                assembly_result.data_length > 0) {
                // Read from device succeeded - use this data
                cJSON *o_to_t_array = cJSON_CreateArray();
                for (uint16_t i = 0; i < assembly_result.data_length; i++) {
                    cJSON_AddItemToArray(o_to_t_array, cJSON_CreateNumber(assembly_result.data[i]));
                }
                // Zero-pad if needed
                for (uint16_t i = assembly_result.data_length; i < implicit_connection_status.assembly_data_size_consumed; i++) {
                    cJSON_AddItemToArray(o_to_t_array, cJSON_CreateNumber(0));
                }
                cJSON_AddItemToObject(response, "last_sent_data", o_to_t_array);
                cJSON_AddNumberToObject(response, "last_sent_length", implicit_connection_status.assembly_data_size_consumed);
                enip_scanner_free_assembly_result(&assembly_result);
            } else {
                // Fallback: return zeros if both memory read and device read failed
                cJSON *o_to_t_array = cJSON_CreateArray();
                for (uint16_t i = 0; i < implicit_connection_status.assembly_data_size_consumed; i++) {
                    cJSON_AddItemToArray(o_to_t_array, cJSON_CreateNumber(0));
                }
                cJSON_AddItemToObject(response, "last_sent_data", o_to_t_array);
                cJSON_AddNumberToObject(response, "last_sent_length", implicit_connection_status.assembly_data_size_consumed);
            }
        }
        
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Implicit connection opened successfully");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", esp_err_to_name(err));
        
        return send_json_response(req, response, ESP_FAIL);
    }
}

// POST /api/scanner/implicit/close
static esp_err_t api_scanner_implicit_close_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/implicit/close");
    
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
    
    ip4_addr_t ip_addr;
    if (ip_item != NULL && cJSON_IsString(ip_item)) {
        if (!inet_aton(ip_item->valuestring, &ip_addr)) {
            cJSON_Delete(json);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid IP address");
            return ESP_FAIL;
        }
    } else if (implicit_connection_status.is_open) {
        ip_addr = implicit_connection_status.ip_address;
    } else {
        // Connection already closed - return success
        cJSON_Delete(json);
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Connection already closed");
        return send_json_response(req, response, ESP_OK);
    }
    
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    esp_err_t err = enip_scanner_implicit_close(&ip_addr, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        implicit_connection_status.is_open = false;
        implicit_connection_status.last_received_length = 0;
        
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Implicit connection closed successfully");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        // Even if close fails, mark as closed in our status to prevent retries
        implicit_connection_status.is_open = false;
        implicit_connection_status.last_received_length = 0;
        
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", esp_err_to_name(err));
        cJSON_AddStringToObject(response, "message", "Close attempt completed (connection may have been already closed)");
        
        // Return 200 OK even on error to prevent client retries
        // The error is still communicated in the JSON response
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/implicit/write-data
static esp_err_t api_scanner_implicit_write_data_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/implicit/write-data");
    
    char content[1024];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", "Invalid request body");
        return send_json_response(req, response, ESP_FAIL);
    }
    content[ret] = '\0';
    
    cJSON *json = cJSON_Parse(content);
    if (json == NULL) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", "Invalid JSON");
        return send_json_response(req, response, ESP_FAIL);
    }
    
    cJSON *ip_item = cJSON_GetObjectItem(json, "ip_address");
    cJSON *data_item = cJSON_GetObjectItem(json, "data");
    
    ip4_addr_t ip_addr;
    if (ip_item != NULL && cJSON_IsString(ip_item)) {
        if (!inet_aton(ip_item->valuestring, &ip_addr)) {
            cJSON_Delete(json);
            cJSON *response = cJSON_CreateObject();
            cJSON_AddBoolToObject(response, "success", false);
            cJSON_AddStringToObject(response, "status", "error");
            cJSON_AddStringToObject(response, "error", "Invalid IP address");
            return send_json_response(req, response, ESP_FAIL);
        }
    } else if (implicit_connection_status.is_open) {
        ip_addr = implicit_connection_status.ip_address;
    } else {
        cJSON_Delete(json);
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", "No connection open");
        return send_json_response(req, response, ESP_FAIL);
    }
    
    if (data_item == NULL || !cJSON_IsArray(data_item)) {
        cJSON_Delete(json);
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", "Missing or invalid data array");
        return send_json_response(req, response, ESP_FAIL);
    }
    
    int data_length = cJSON_GetArraySize(data_item);
    if (data_length == 0 || data_length > 500) {
        cJSON_Delete(json);
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", "Data length must be 1-500 bytes");
        return send_json_response(req, response, ESP_FAIL);
    }
    
    // Validate data length matches connection size (only if size is known and non-zero)
    if (implicit_connection_status.is_open && 
        implicit_connection_status.assembly_data_size_consumed > 0 &&
        data_length != implicit_connection_status.assembly_data_size_consumed) {
        cJSON_Delete(json);
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "status", "error");
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "Data length (%d) must match assembly_data_size_consumed (%u)", 
                 data_length, implicit_connection_status.assembly_data_size_consumed);
        cJSON_AddStringToObject(response, "error", error_msg);
        return send_json_response(req, response, ESP_FAIL);
    }
    
    uint8_t *data = malloc(data_length);
    if (data == NULL) {
        cJSON_Delete(json);
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", "Out of memory");
        return send_json_response(req, response, ESP_FAIL);
    }
    
    for (int i = 0; i < data_length; i++) {
        cJSON *item = cJSON_GetArrayItem(data_item, i);
        if (item == NULL || !cJSON_IsNumber(item)) {
            free(data);
            cJSON_Delete(json);
            cJSON *response = cJSON_CreateObject();
            cJSON_AddBoolToObject(response, "success", false);
            cJSON_AddStringToObject(response, "status", "error");
            cJSON_AddStringToObject(response, "error", "Invalid data array element");
            return send_json_response(req, response, ESP_FAIL);
        }
        int value = item->valueint;
        if (value < 0 || value > 255) {
            free(data);
            cJSON_Delete(json);
            cJSON *response = cJSON_CreateObject();
            cJSON_AddBoolToObject(response, "success", false);
            cJSON_AddStringToObject(response, "status", "error");
            cJSON_AddStringToObject(response, "error", "Data values must be 0-255");
            return send_json_response(req, response, ESP_FAIL);
        }
        data[i] = (uint8_t)value;
    }
    
    cJSON_Delete(json);
    
    esp_err_t err = enip_scanner_implicit_write_data(&ip_addr, data, data_length);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddStringToObject(response, "message", "Data written successfully");
        cJSON_AddNumberToObject(response, "data_length", data_length);
        
        esp_err_t ret = send_json_response(req, response, ESP_OK);
        free(data);
        return ret;
    } else {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", esp_err_to_name(err));
        
        esp_err_t ret = send_json_response(req, response, ESP_FAIL);
        free(data);
        return ret;
    }
}

// GET /api/scanner/implicit/status
static esp_err_t api_scanner_implicit_status_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /api/scanner/implicit/status");
    
    cJSON *response = cJSON_CreateObject();
    
    cJSON_AddBoolToObject(response, "is_open", implicit_connection_status.is_open);
    
    if (implicit_connection_status.is_open) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&implicit_connection_status.ip_address));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddNumberToObject(response, "assembly_instance_consumed", 
                                implicit_connection_status.assembly_instance_consumed);
        cJSON_AddNumberToObject(response, "assembly_instance_produced", 
                                implicit_connection_status.assembly_instance_produced);
        cJSON_AddNumberToObject(response, "assembly_data_size_consumed", 
                                implicit_connection_status.assembly_data_size_consumed);
        cJSON_AddNumberToObject(response, "assembly_data_size_produced", 
                                implicit_connection_status.assembly_data_size_produced);
        cJSON_AddNumberToObject(response, "rpi_ms", implicit_connection_status.rpi_ms);
        cJSON_AddBoolToObject(response, "exclusive_owner", implicit_connection_status.exclusive_owner);
        cJSON_AddNumberToObject(response, "last_received_length", 
                                implicit_connection_status.last_received_length);
        cJSON_AddNumberToObject(response, "last_packet_time_ms", 
                                implicit_connection_status.last_packet_time);
        
        // Read current O-to-T data from memory (stored in connection buffer)
        uint8_t o_to_t_data[500];
        uint16_t o_to_t_length = 0;
        esp_err_t read_ret = enip_scanner_implicit_read_o_to_t_data(&implicit_connection_status.ip_address,
                                                                     o_to_t_data, &o_to_t_length,
                                                                     sizeof(o_to_t_data));
        if (read_ret == ESP_OK && o_to_t_length > 0) {
            cJSON *o_to_t_array = cJSON_CreateArray();
            for (uint16_t i = 0; i < o_to_t_length; i++) {
                cJSON_AddItemToArray(o_to_t_array, cJSON_CreateNumber(o_to_t_data[i]));
            }
            cJSON_AddItemToObject(response, "last_sent_data", o_to_t_array);
            cJSON_AddNumberToObject(response, "last_sent_length", o_to_t_length);
        } else {
            // If no data in memory yet, return empty array so grid can be initialized
            cJSON *o_to_t_array = cJSON_CreateArray();
            for (uint16_t i = 0; i < implicit_connection_status.assembly_data_size_consumed; i++) {
                cJSON_AddItemToArray(o_to_t_array, cJSON_CreateNumber(0));
            }
            cJSON_AddItemToObject(response, "last_sent_data", o_to_t_array);
            cJSON_AddNumberToObject(response, "last_sent_length", implicit_connection_status.assembly_data_size_consumed);
        }
        
        // Read current T-to-O data from the device (read-only, can't change it)
        if (implicit_connection_status.last_received_length > 0) {
            enip_scanner_assembly_result_t assembly_result = {0};
            if (enip_scanner_read_assembly(&implicit_connection_status.ip_address,
                                          implicit_connection_status.assembly_instance_produced,
                                          &assembly_result, 5000) == ESP_OK &&
                assembly_result.data_length > 0) {
                cJSON *data_array = cJSON_CreateArray();
                for (uint16_t i = 0; i < assembly_result.data_length; i++) {
                    cJSON_AddItemToArray(data_array, cJSON_CreateNumber(assembly_result.data[i]));
                }
                cJSON_AddItemToObject(response, "last_received_data", data_array);
                enip_scanner_free_assembly_result(&assembly_result);
            }
        }
    }
    
    cJSON_AddStringToObject(response, "status", "ok");
    
    return send_json_response(req, response, ESP_OK);
}

#endif // CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT

#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT

// POST /api/scanner/motoman/read-position-variable
static esp_err_t api_scanner_motoman_read_position_variable_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-position-variable");
    
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
    cJSON *variable_item = cJSON_GetObjectItem(json, "variable_number");
    
    if (ip_item == NULL || variable_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(variable_item)) {
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
    
    uint16_t variable_number = (uint16_t)variable_item->valueint;
    if (variable_number > 9) {  // P1-P10 = 0-9
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Variable number must be 0-9 (P1-P10)");
        return ESP_FAIL;
    }
    
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    enip_scanner_motoman_position_t position;
    memset(&position, 0, sizeof(position));
    esp_err_t err = enip_scanner_motoman_read_variable_p(&ip_addr, variable_number, &position, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK && position.success) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&position.ip_address));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddNumberToObject(response, "variable_number", variable_number);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "data_type", position.data_type);
        cJSON_AddNumberToObject(response, "configuration", position.configuration);
        cJSON_AddNumberToObject(response, "tool_number", position.tool_number);
        cJSON_AddNumberToObject(response, "user_coordinate_number", position.reservation);  // reservation = user coordinate number for Class 0x7F
        cJSON_AddNumberToObject(response, "extended_configuration", position.extended_configuration);
        
        // Add axis data array
        cJSON *axis_array = cJSON_CreateArray();
        for (int i = 0; i < 8; i++) {
            cJSON_AddItemToArray(axis_array, cJSON_CreateNumber(position.axis_data[i]));
        }
        cJSON_AddItemToObject(response, "axis_data", axis_array);
        
        cJSON_AddStringToObject(response, "status", "ok");
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddNumberToObject(response, "variable_number", variable_number);
        cJSON_AddBoolToObject(response, "success", false);
        const char *error_msg = (position.error_message[0] != '\0') ? position.error_message : "Unknown error";
        cJSON_AddStringToObject(response, "error", error_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);  // Return 200 with error JSON
    }
}

// POST /api/scanner/motoman/read-alarm
static esp_err_t api_scanner_motoman_read_alarm_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-alarm");
    
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
    cJSON *type_item = cJSON_GetObjectItem(json, "alarm_type");
    cJSON *instance_item = cJSON_GetObjectItem(json, "alarm_instance");
    
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
    
    // Extract alarm_type BEFORE deleting JSON (cJSON strings are freed when JSON is deleted!)
    char alarm_type_buffer[16] = "current";  // Default to "current"
    if (type_item && cJSON_IsString(type_item) && type_item->valuestring) {
        strncpy(alarm_type_buffer, type_item->valuestring, sizeof(alarm_type_buffer) - 1);
        alarm_type_buffer[sizeof(alarm_type_buffer) - 1] = '\0';  // Ensure null termination
    }
    const char *alarm_type = alarm_type_buffer;
    
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    uint16_t alarm_instance = (uint16_t)instance_item->valueint;
    
    // Delete JSON now - we've copied all the values we need
    cJSON_Delete(json);
    
    enip_scanner_motoman_alarm_t alarm;
    memset(&alarm, 0, sizeof(alarm));
    
    esp_err_t err;
    // Compare alarm_type - use strcmp since we know it's "history" or "current"
    bool is_history = (alarm_type && strcmp(alarm_type, "history") == 0);
    
    if (is_history) {
        err = enip_scanner_motoman_read_alarm_history(&ip_addr, alarm_instance, &alarm, timeout_ms);
    } else {
        if (alarm_instance < 1 || alarm_instance > 4) {
            err = ESP_ERR_INVALID_ARG;
            snprintf(alarm.error_message, sizeof(alarm.error_message), "Invalid alarm instance (must be 1-4)");
        } else {
            err = enip_scanner_motoman_read_alarm(&ip_addr, (uint8_t)alarm_instance, &alarm, timeout_ms);
        }
        alarm_type = "current";
    }
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK && alarm.success) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&alarm.ip_address));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddStringToObject(response, "alarm_type", alarm_type);
        cJSON_AddNumberToObject(response, "alarm_instance", alarm_instance);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "alarm_code", alarm.alarm_code);
        cJSON_AddNumberToObject(response, "alarm_data", alarm.alarm_data);
        cJSON_AddNumberToObject(response, "alarm_data_type", alarm.alarm_data_type);
        cJSON_AddStringToObject(response, "alarm_date_time", alarm.alarm_date_time);
        cJSON_AddStringToObject(response, "alarm_string", alarm.alarm_string);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddStringToObject(response, "alarm_type", alarm_type);
        cJSON_AddNumberToObject(response, "alarm_instance", alarm_instance);
        cJSON_AddBoolToObject(response, "success", false);
        const char *error_msg = (alarm.error_message[0] != '\0') ? alarm.error_message : "Unknown error";
        cJSON_AddStringToObject(response, "error", error_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-job-info
static esp_err_t api_scanner_motoman_read_job_info_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-job-info");
    
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
    
    cJSON_Delete(json);
    
    enip_scanner_motoman_job_info_t job_info;
    memset(&job_info, 0, sizeof(job_info));
    esp_err_t err = enip_scanner_motoman_read_job_info(&ip_addr, &job_info, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK && job_info.success) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&job_info.ip_address));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "job_name", job_info.job_name);
        cJSON_AddNumberToObject(response, "line_number", job_info.line_number);
        cJSON_AddNumberToObject(response, "step_number", job_info.step_number);
        cJSON_AddNumberToObject(response, "speed_override", job_info.speed_override);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *error_msg = (job_info.error_message[0] != '\0') ? job_info.error_message : "Unknown error";
        cJSON_AddStringToObject(response, "error", error_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-axis-config
static esp_err_t api_scanner_motoman_read_axis_config_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-axis-config");
    
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
    cJSON *group_item = cJSON_GetObjectItem(json, "control_group");
    if (ip_item == NULL || group_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(group_item)) {
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
    
    uint16_t control_group = (uint16_t)group_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    enip_scanner_motoman_axis_config_t config;
    memset(&config, 0, sizeof(config));
    esp_err_t err = enip_scanner_motoman_read_axis_config(&ip_addr, control_group, &config, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK && config.success) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&config.ip_address));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "control_group", control_group);
        
        cJSON *axis_array = cJSON_CreateArray();
        for (int i = 0; i < 8; i++) {
            cJSON_AddItemToArray(axis_array, cJSON_CreateString(config.axis_names[i]));
        }
        cJSON_AddItemToObject(response, "axis_names", axis_array);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *error_msg = (config.error_message[0] != '\0') ? config.error_message : "Unknown error";
        cJSON_AddStringToObject(response, "error", error_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-position
static esp_err_t api_scanner_motoman_read_position_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-position");
    
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
    cJSON *group_item = cJSON_GetObjectItem(json, "control_group");
    if (ip_item == NULL || group_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(group_item)) {
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
    
    uint16_t control_group = (uint16_t)group_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    enip_scanner_motoman_position_t position;
    memset(&position, 0, sizeof(position));
    esp_err_t err = enip_scanner_motoman_read_position(&ip_addr, control_group, &position, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK && position.success) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&position.ip_address));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "control_group", control_group);
        cJSON_AddNumberToObject(response, "data_type", position.data_type);
        cJSON_AddNumberToObject(response, "configuration", position.configuration);
        cJSON_AddNumberToObject(response, "tool_number", position.tool_number);
        cJSON_AddNumberToObject(response, "reservation", position.reservation);
        cJSON_AddNumberToObject(response, "extended_configuration", position.extended_configuration);
        
        cJSON *axis_array = cJSON_CreateArray();
        for (int i = 0; i < 8; i++) {
            cJSON_AddItemToArray(axis_array, cJSON_CreateNumber(position.axis_data[i]));
        }
        cJSON_AddItemToObject(response, "axis_data", axis_array);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *error_msg = (position.error_message[0] != '\0') ? position.error_message : "Unknown error";
        cJSON_AddStringToObject(response, "error", error_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-position-deviation
static esp_err_t api_scanner_motoman_read_position_deviation_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-position-deviation");
    
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
    cJSON *group_item = cJSON_GetObjectItem(json, "control_group");
    if (ip_item == NULL || group_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(group_item)) {
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
    
    uint16_t control_group = (uint16_t)group_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    enip_scanner_motoman_position_deviation_t deviation;
    memset(&deviation, 0, sizeof(deviation));
    esp_err_t err = enip_scanner_motoman_read_position_deviation(&ip_addr, control_group, &deviation, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK && deviation.success) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&deviation.ip_address));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "control_group", control_group);
        
        cJSON *axis_array = cJSON_CreateArray();
        for (int i = 0; i < 8; i++) {
            cJSON_AddItemToArray(axis_array, cJSON_CreateNumber(deviation.axis_deviation[i]));
        }
        cJSON_AddItemToObject(response, "axis_deviation", axis_array);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *error_msg = (deviation.error_message[0] != '\0') ? deviation.error_message : "Unknown error";
        cJSON_AddStringToObject(response, "error", error_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-torque
static esp_err_t api_scanner_motoman_read_torque_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-torque");
    
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
    cJSON *group_item = cJSON_GetObjectItem(json, "control_group");
    if (ip_item == NULL || group_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(group_item)) {
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
    
    uint16_t control_group = (uint16_t)group_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    enip_scanner_motoman_torque_t torque;
    memset(&torque, 0, sizeof(torque));
    esp_err_t err = enip_scanner_motoman_read_torque(&ip_addr, control_group, &torque, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK && torque.success) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "control_group", control_group);
        
        cJSON *axis_array = cJSON_CreateArray();
        for (int i = 0; i < 8; i++) {
            cJSON_AddItemToArray(axis_array, cJSON_CreateNumber(torque.axis_torque[i]));
        }
        cJSON_AddItemToObject(response, "axis_torque", axis_array);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *error_msg = (torque.error_message[0] != '\0') ? torque.error_message : "Unknown error";
        cJSON_AddStringToObject(response, "error", error_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-io
static esp_err_t api_scanner_motoman_read_io_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-io");
    
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
    cJSON *signal_item = cJSON_GetObjectItem(json, "signal_number");
    if (ip_item == NULL || signal_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(signal_item)) {
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
    
    uint16_t signal_number = (uint16_t)signal_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    uint8_t value = 0;
    char error_msg[128] = {0};
    esp_err_t err = enip_scanner_motoman_read_io(&ip_addr, signal_number, &value, timeout_ms, error_msg);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "signal_number", signal_number);
        cJSON_AddNumberToObject(response, "value", value);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *err_msg = (error_msg[0] != '\0') ? error_msg : "Unknown error";
        cJSON_AddStringToObject(response, "error", err_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-register
static esp_err_t api_scanner_motoman_read_register_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-register");
    
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
    cJSON *reg_item = cJSON_GetObjectItem(json, "register_number");
    if (ip_item == NULL || reg_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(reg_item)) {
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
    
    uint16_t register_number = (uint16_t)reg_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    uint16_t value = 0;
    char error_msg[128] = {0};
    esp_err_t err = enip_scanner_motoman_read_register(&ip_addr, register_number, &value, timeout_ms, error_msg);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "register_number", register_number);
        cJSON_AddNumberToObject(response, "value", value);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *err_msg = (error_msg[0] != '\0') ? error_msg : "Unknown error";
        cJSON_AddStringToObject(response, "error", err_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-variable-b
static esp_err_t api_scanner_motoman_read_variable_b_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-variable-b");
    
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
    cJSON *var_item = cJSON_GetObjectItem(json, "variable_number");
    if (ip_item == NULL || var_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(var_item)) {
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
    
    uint16_t variable_number = (uint16_t)var_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    uint8_t value = 0;
    char error_msg[128] = {0};
    esp_err_t err = enip_scanner_motoman_read_variable_b(&ip_addr, variable_number, &value, timeout_ms, error_msg);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "variable_number", variable_number);
        cJSON_AddNumberToObject(response, "value", value);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *err_msg = (error_msg[0] != '\0') ? error_msg : "Unknown error";
        cJSON_AddStringToObject(response, "error", err_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-variable-i
static esp_err_t api_scanner_motoman_read_variable_i_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-variable-i");
    
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
    cJSON *var_item = cJSON_GetObjectItem(json, "variable_number");
    if (ip_item == NULL || var_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(var_item)) {
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
    
    uint16_t variable_number = (uint16_t)var_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    int16_t value = 0;
    char error_msg[128] = {0};
    esp_err_t err = enip_scanner_motoman_read_variable_i(&ip_addr, variable_number, &value, timeout_ms, error_msg);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "variable_number", variable_number);
        cJSON_AddNumberToObject(response, "value", value);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *err_msg = (error_msg[0] != '\0') ? error_msg : "Unknown error";
        cJSON_AddStringToObject(response, "error", err_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-variable-d
static esp_err_t api_scanner_motoman_read_variable_d_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-variable-d");
    
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
    cJSON *var_item = cJSON_GetObjectItem(json, "variable_number");
    if (ip_item == NULL || var_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(var_item)) {
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
    
    uint16_t variable_number = (uint16_t)var_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    int32_t value = 0;
    char error_msg[128] = {0};
    esp_err_t err = enip_scanner_motoman_read_variable_d(&ip_addr, variable_number, &value, timeout_ms, error_msg);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "variable_number", variable_number);
        cJSON_AddNumberToObject(response, "value", value);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *err_msg = (error_msg[0] != '\0') ? error_msg : "Unknown error";
        cJSON_AddStringToObject(response, "error", err_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-variable-r
static esp_err_t api_scanner_motoman_read_variable_r_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-variable-r");
    
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
    cJSON *var_item = cJSON_GetObjectItem(json, "variable_number");
    if (ip_item == NULL || var_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(var_item)) {
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
    
    uint16_t variable_number = (uint16_t)var_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    float value = 0.0f;
    char error_msg[128] = {0};
    esp_err_t err = enip_scanner_motoman_read_variable_r(&ip_addr, variable_number, &value, timeout_ms, error_msg);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "variable_number", variable_number);
        cJSON_AddNumberToObject(response, "value", value);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *err_msg = (error_msg[0] != '\0') ? error_msg : "Unknown error";
        cJSON_AddStringToObject(response, "error", err_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// POST /api/scanner/motoman/read-variable-s
static esp_err_t api_scanner_motoman_read_variable_s_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-variable-s");
    
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
    cJSON *var_item = cJSON_GetObjectItem(json, "variable_number");
    if (ip_item == NULL || var_item == NULL ||
        !cJSON_IsString(ip_item) || !cJSON_IsNumber(var_item)) {
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
    
    uint16_t variable_number = (uint16_t)var_item->valueint;
    uint32_t timeout_ms = 5000;
    cJSON *timeout_item = cJSON_GetObjectItem(json, "timeout_ms");
    if (timeout_item != NULL && cJSON_IsNumber(timeout_item)) {
        timeout_ms = (uint32_t)timeout_item->valueint;
    }
    
    cJSON_Delete(json);
    
    char value[33] = {0};
    char error_msg[128] = {0};
    esp_err_t err = enip_scanner_motoman_read_variable_s(&ip_addr, variable_number, value, sizeof(value), timeout_ms, error_msg);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "variable_number", variable_number);
        cJSON_AddStringToObject(response, "value", value);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *err_msg = (error_msg[0] != '\0') ? error_msg : "Unknown error";
        cJSON_AddStringToObject(response, "error", err_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

// GET /api/scanner/motoman/rs022
static esp_err_t api_scanner_motoman_get_rs022_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /api/scanner/motoman/rs022");
    
    bool instance_direct = false;
    system_motoman_rs022_load(&instance_direct);
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddBoolToObject(response, "instance_direct", instance_direct);
    cJSON_AddStringToObject(response, "status", "ok");
    
    return send_json_response(req, response, ESP_OK);
}

// POST /api/scanner/motoman/rs022
static esp_err_t api_scanner_motoman_set_rs022_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/rs022");
    
    char content[128];
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
    
    cJSON *val_item = cJSON_GetObjectItem(json, "instance_direct");
    if (val_item == NULL || !cJSON_IsBool(val_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid parameters");
        return ESP_FAIL;
    }
    
    bool instance_direct = cJSON_IsTrue(val_item);
    cJSON_Delete(json);
    
    bool saved = system_motoman_rs022_save(instance_direct);
    if (saved) {
        enip_scanner_motoman_set_rs022_instance_direct(instance_direct);
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", saved);
    cJSON_AddBoolToObject(response, "instance_direct", instance_direct);
    cJSON_AddStringToObject(response, "status", saved ? "ok" : "error");
    
    if (!saved) {
        cJSON_AddStringToObject(response, "error", "Failed to save RS022 setting");
    }
    
    return send_json_response(req, response, ESP_OK);
}

// POST /api/scanner/motoman/read-status
static esp_err_t api_scanner_motoman_read_status_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/scanner/motoman/read-status");
    
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
    
    cJSON_Delete(json);
    
    enip_scanner_motoman_status_t status;
    memset(&status, 0, sizeof(status));
    esp_err_t err = enip_scanner_motoman_read_status(&ip_addr, &status, timeout_ms);
    
    cJSON *response = cJSON_CreateObject();
    
    if (err == ESP_OK && status.success) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&status.ip_address));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddNumberToObject(response, "data1", status.data1);
        cJSON_AddNumberToObject(response, "data2", status.data2);
        cJSON_AddBoolToObject(response, "hold_pendant", (status.data2 & (1U << 1)) != 0);
        cJSON_AddBoolToObject(response, "hold_external", (status.data2 & (1U << 2)) != 0);
        cJSON_AddBoolToObject(response, "hold_command", (status.data2 & (1U << 3)) != 0);
        cJSON_AddBoolToObject(response, "alarm", (status.data2 & (1U << 4)) != 0);
        cJSON_AddBoolToObject(response, "error", (status.data2 & (1U << 5)) != 0);
        cJSON_AddBoolToObject(response, "servo_on", (status.data2 & (1U << 6)) != 0);
        cJSON_AddStringToObject(response, "status", "ok");
        
        return send_json_response(req, response, ESP_OK);
    } else {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_addr));
        
        cJSON_AddStringToObject(response, "ip_address", ip_str);
        cJSON_AddBoolToObject(response, "success", false);
        const char *error_msg = (status.error_message[0] != '\0') ? status.error_message : "Unknown error";
        cJSON_AddStringToObject(response, "error", error_msg);
        cJSON_AddStringToObject(response, "status", "error");
        
        return send_json_response(req, response, ESP_OK);
    }
}

#endif // CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT

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

#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
    httpd_uri_t scanner_implicit_open_uri = {
        .uri = "/api/scanner/implicit/open",
        .method = HTTP_POST,
        .handler = api_scanner_implicit_open_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_implicit_open_uri);
    
    httpd_uri_t scanner_implicit_close_uri = {
        .uri = "/api/scanner/implicit/close",
        .method = HTTP_POST,
        .handler = api_scanner_implicit_close_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_implicit_close_uri);
    
    httpd_uri_t scanner_implicit_write_data_uri = {
        .uri = "/api/scanner/implicit/write-data",
        .method = HTTP_POST,
        .handler = api_scanner_implicit_write_data_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_implicit_write_data_uri);
    
    httpd_uri_t scanner_implicit_status_uri = {
        .uri = "/api/scanner/implicit/status",
        .method = HTTP_GET,
        .handler = api_scanner_implicit_status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_implicit_status_uri);
    
    ESP_LOGI(TAG, "Implicit messaging API endpoints registered");
#endif

#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
    httpd_uri_t scanner_motoman_read_position_variable_uri = {
        .uri = "/api/scanner/motoman/read-position-variable",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_position_variable_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_position_variable_uri);
    ESP_LOGI(TAG, "Motoman position variable API endpoint registered");

    httpd_uri_t scanner_motoman_read_alarm_uri = {
        .uri = "/api/scanner/motoman/read-alarm",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_alarm_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_alarm_uri);
    ESP_LOGI(TAG, "Motoman alarm API endpoint registered");

    httpd_uri_t scanner_motoman_read_status_uri = {
        .uri = "/api/scanner/motoman/read-status",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_status_uri);
    ESP_LOGI(TAG, "Motoman status API endpoint registered");

    httpd_uri_t scanner_motoman_read_job_info_uri = {
        .uri = "/api/scanner/motoman/read-job-info",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_job_info_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_job_info_uri);
    ESP_LOGI(TAG, "Motoman job info API endpoint registered");

    httpd_uri_t scanner_motoman_read_axis_config_uri = {
        .uri = "/api/scanner/motoman/read-axis-config",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_axis_config_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_axis_config_uri);
    ESP_LOGI(TAG, "Motoman axis config API endpoint registered");

    httpd_uri_t scanner_motoman_read_position_uri = {
        .uri = "/api/scanner/motoman/read-position",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_position_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_position_uri);
    ESP_LOGI(TAG, "Motoman position API endpoint registered");

    httpd_uri_t scanner_motoman_read_position_dev_uri = {
        .uri = "/api/scanner/motoman/read-position-deviation",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_position_deviation_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_position_dev_uri);
    ESP_LOGI(TAG, "Motoman position deviation API endpoint registered");

    httpd_uri_t scanner_motoman_read_torque_uri = {
        .uri = "/api/scanner/motoman/read-torque",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_torque_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_torque_uri);
    ESP_LOGI(TAG, "Motoman torque API endpoint registered");

    httpd_uri_t scanner_motoman_read_io_uri = {
        .uri = "/api/scanner/motoman/read-io",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_io_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_io_uri);
    ESP_LOGI(TAG, "Motoman IO API endpoint registered");

    httpd_uri_t scanner_motoman_read_register_uri = {
        .uri = "/api/scanner/motoman/read-register",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_register_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_register_uri);
    ESP_LOGI(TAG, "Motoman register API endpoint registered");

    httpd_uri_t scanner_motoman_read_var_b_uri = {
        .uri = "/api/scanner/motoman/read-variable-b",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_variable_b_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_var_b_uri);
    ESP_LOGI(TAG, "Motoman variable B API endpoint registered");

    httpd_uri_t scanner_motoman_read_var_i_uri = {
        .uri = "/api/scanner/motoman/read-variable-i",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_variable_i_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_var_i_uri);
    ESP_LOGI(TAG, "Motoman variable I API endpoint registered");

    httpd_uri_t scanner_motoman_read_var_d_uri = {
        .uri = "/api/scanner/motoman/read-variable-d",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_variable_d_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_var_d_uri);
    ESP_LOGI(TAG, "Motoman variable D API endpoint registered");

    httpd_uri_t scanner_motoman_read_var_r_uri = {
        .uri = "/api/scanner/motoman/read-variable-r",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_variable_r_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_var_r_uri);
    ESP_LOGI(TAG, "Motoman variable R API endpoint registered");

    httpd_uri_t scanner_motoman_read_var_s_uri = {
        .uri = "/api/scanner/motoman/read-variable-s",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_read_variable_s_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_read_var_s_uri);
    ESP_LOGI(TAG, "Motoman variable S API endpoint registered");

    httpd_uri_t scanner_motoman_get_rs022_uri = {
        .uri = "/api/scanner/motoman/rs022",
        .method = HTTP_GET,
        .handler = api_scanner_motoman_get_rs022_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_get_rs022_uri);
    ESP_LOGI(TAG, "Motoman RS022 GET endpoint registered");

    httpd_uri_t scanner_motoman_set_rs022_uri = {
        .uri = "/api/scanner/motoman/rs022",
        .method = HTTP_POST,
        .handler = api_scanner_motoman_set_rs022_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scanner_motoman_set_rs022_uri);
    ESP_LOGI(TAG, "Motoman RS022 POST endpoint registered");
#endif
    
    ESP_LOGI(TAG, "Web UI API endpoints registered");
    return ESP_OK;
}

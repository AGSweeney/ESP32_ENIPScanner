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

#include "enip_scanner_tag_internal.h"
#include "enip_scanner.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT

// Forward declaration for data type handlers
extern esp_err_t tag_data_encode_write(uint16_t cip_data_type,
                                       const uint8_t *input_data, uint16_t input_length,
                                       uint8_t *output_buffer, size_t output_size,
                                       uint16_t *output_length, char *error_msg);
extern uint16_t tag_data_get_encoded_size(uint16_t cip_data_type, uint16_t input_length);

static const char *TAG = "enip_scanner_tag";

// ============================================================================
// Tag Path Encoding
// ============================================================================

static esp_err_t encode_tag_path(const char *tag_name, uint8_t *path_buffer, 
                                 size_t buffer_size, uint8_t *path_length_words)
{
    if (tag_name == NULL || path_buffer == NULL || path_length_words == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t tag_len = strlen(tag_name);
    if (tag_len == 0 || tag_len > 255) {
        ESP_LOGE(TAG, "Invalid tag name length: %zu (must be 1-255)", tag_len);
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t offset = 0;
    
    // Split tag name on "." only - per pylogix implementation
    // For "Program:Main.Countup", this creates ["Program:Main", "Countup"]
    // pylogix doesn't split on ":" - it keeps "Program:Main" as one segment
    const char *segment_start = tag_name;
    const char *segment_end;
    
    while (segment_start < tag_name + tag_len) {
        // Find next "." or end of string (don't split on ":")
        segment_end = strchr(segment_start, '.');
        if (segment_end == NULL) {
            segment_end = tag_name + tag_len;
        }
        
        size_t segment_len = segment_end - segment_start;
        if (segment_len == 0) {
            // Skip empty segments
            segment_start = segment_end;
            if (*segment_start == '.') {
                segment_start++;
            }
            continue;
        }
        
        // Skip numeric segments (bit access like ".1" - handled elsewhere)
        // Check if segment is purely numeric
        bool is_numeric = true;
        for (size_t i = 0; i < segment_len; i++) {
            if (segment_start[i] < '0' || segment_start[i] > '9') {
                is_numeric = false;
                break;
            }
        }
        if (is_numeric && segment_len <= 2) {
            // Skip small numeric segments (likely bit access)
            segment_start = segment_end;
            if (*segment_start == '.') {
                segment_start++;
            }
            continue;
        }
        
        if (segment_len > 255) {
            ESP_LOGE(TAG, "Segment too long: %zu bytes", segment_len);
            return ESP_ERR_INVALID_SIZE;
        }
        
        // Check buffer space: segment type (1) + length (1) + data + padding
        size_t segment_bytes = 1 + 1 + segment_len;
        if (segment_bytes % 2 != 0) {
            segment_bytes++;  // Will need padding
        }
        
        if (offset + segment_bytes > buffer_size) {
            ESP_LOGE(TAG, "Tag path too long for buffer");
            return ESP_ERR_INVALID_SIZE;
        }
        
        // Encode segment: [0x91] [length] [segment bytes] [padding if needed]
        path_buffer[offset++] = 0x91;  // Symbolic segment type
        path_buffer[offset++] = (uint8_t)segment_len;
        
        // Copy segment bytes
        memcpy(path_buffer + offset, segment_start, segment_len);
        offset += segment_len;
        
        // Pad to even bytes if needed
        if (offset % 2 != 0) {
            path_buffer[offset++] = 0x00;
        }
        
        // Move to next segment (skip the ".")
        segment_start = segment_end;
        if (*segment_start == '.') {
            segment_start++;
        }
    }
    
    *path_length_words = (uint8_t)(offset / 2);
    return ESP_OK;
}

// ============================================================================
// Tag Read Operation
// ============================================================================

esp_err_t enip_scanner_read_tag(const ip4_addr_t *ip_address,
                                const char *tag_path,
                                enip_scanner_tag_result_t *result,
                                uint32_t timeout_ms)
{
    if (ip_address == NULL || tag_path == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(result, 0, sizeof(enip_scanner_tag_result_t));
    result->ip_address = *ip_address;
    strncpy(result->tag_path, tag_path, sizeof(result->tag_path) - 1);
    result->tag_path[sizeof(result->tag_path) - 1] = '\0';
    result->success = false;
    
    // Thread-safe check of initialization state
    if (s_scanner_mutex == NULL) {
        snprintf(result->error_message, sizeof(result->error_message), "Scanner not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_scanner_mutex, portMAX_DELAY) != pdTRUE) {
        snprintf(result->error_message, sizeof(result->error_message), "Failed to acquire mutex");
        return ESP_FAIL;
    }
    
    bool initialized = s_scanner_initialized;
    xSemaphoreGive(s_scanner_mutex);
    
    if (!initialized) {
        snprintf(result->error_message, sizeof(result->error_message), "Scanner not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t start_time = xTaskGetTickCount();
    
    // Create TCP socket
    int sock = create_tcp_socket(ip_address, timeout_ms);
    if (sock < 0) {
        snprintf(result->error_message, sizeof(result->error_message), "Failed to connect to device");
        return ESP_FAIL;
    }
    
    // Register session
    uint32_t session_handle = 0;
    esp_err_t ret = register_session(sock, &session_handle);
    if (ret != ESP_OK) {
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Failed to register session: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Encode tag path as CIP symbolic segment
    uint8_t cip_path[256];
    uint8_t path_size_words = 0;
    ret = encode_tag_path(tag_path, cip_path, sizeof(cip_path), &path_size_words);
    if (ret != ESP_OK) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Failed to encode tag path");
        return ret;
    }
    
    // CIP Service: Read Tag (0x4C)
    uint8_t cip_service = CIP_SERVICE_READ;
    uint16_t element_count = 1;
    
    // CIP message length: Service + Path Size + Path + Element Count
    uint16_t cip_message_length = 1 + 1 + (path_size_words * 2) + 2;
    
    // SendRRData format
    uint16_t enip_data_length = 4 + 2 + 2 + 4 + 4 + cip_message_length;
    
    // Build complete packet
    uint8_t packet[512];
    size_t offset = 0;
    
    // Build ENIP header
    uint16_t cmd = ENIP_SEND_RR_DATA;
    memcpy(packet + offset, &cmd, 2);
    offset += 2;
    memcpy(packet + offset, &enip_data_length, 2);
    offset += 2;
    memcpy(packet + offset, &session_handle, 4);
    offset += 4;
    uint32_t status = 0;
    memcpy(packet + offset, &status, 4);
    offset += 4;
    uint64_t sender_context = 0;
    memcpy(packet + offset, &sender_context, 8);
    offset += 8;
    uint32_t options = 0;
    memcpy(packet + offset, &options, 4);
    offset += 4;
    
    // Interface Handle
    uint32_t interface_handle = 0;
    memcpy(packet + offset, &interface_handle, 4);
    offset += 4;
    
    // Timeout
    uint8_t cip_timeout = 0x0A;
    memcpy(packet + offset, &cip_timeout, 1);
    offset += 1;
    packet[offset++] = 0x00;
    
    // Item Count
    uint16_t item_count = 2;
    memcpy(packet + offset, &item_count, 2);
    offset += 2;
    
    // Item 1: Null Address Item
    uint16_t null_item_type = 0x0000;
    uint16_t null_item_length = 0x0000;
    memcpy(packet + offset, &null_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &null_item_length, 2);
    offset += 2;
    
    // Item 2: Unconnected Data Item
    uint16_t data_item_type = 0x00B2;
    memcpy(packet + offset, &data_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &cip_message_length, 2);
    offset += 2;
    
    // CIP Message: Service + Path Size + Path + Element Count
    packet[offset++] = cip_service;  // 0x4C (Read Tag)
    packet[offset++] = path_size_words;
    memcpy(packet + offset, cip_path, path_size_words * 2);
    offset += path_size_words * 2;
    memcpy(packet + offset, &element_count, 2);
    offset += 2;
    
    // Send request
    ret = send_data(sock, packet, offset);
    if (ret != ESP_OK) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Failed to send request");
        return ret;
    }
    
    // Receive response - first get header to determine length
    uint8_t response_buffer[512];
    size_t bytes_received = 0;
    
    // Receive at least the ENIP header (24 bytes)
    ssize_t recv_ret = recv(sock, response_buffer, sizeof(response_buffer), 0);
    if (recv_ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "Receive timeout waiting for response");
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Timeout waiting for response");
            return ESP_ERR_TIMEOUT;
        }
        ESP_LOGE(TAG, "Failed to receive response: %d", errno);
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Failed to receive response");
        return ESP_FAIL;
    }
    if (recv_ret == 0) {
        ESP_LOGE(TAG, "Connection closed by peer");
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Connection closed by peer");
        return ESP_FAIL;
    }
    
    bytes_received = recv_ret;
    
    // Try to read more if we got a partial response
    if (bytes_received < 40) {
        recv_ret = recv(sock, response_buffer + bytes_received, sizeof(response_buffer) - bytes_received, 0);
        if (recv_ret > 0) {
            bytes_received += recv_ret;
        }
    }
    
    if (bytes_received < sizeof(enip_header_t) + 4) {
        ESP_LOGE(TAG, "Response too short: got %zu bytes", bytes_received);
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Response too short");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Find SendRRData command (may have run/idle header)
    int header_offset = 0;
    for (int i = 0; i < 8 && i < (int)bytes_received - 1; i += 2) {
        uint16_t cmd = *(uint16_t *)(response_buffer + i);
        if (cmd == ENIP_SEND_RR_DATA) {
            header_offset = i;
            break;
        }
    }
    
    if (header_offset + sizeof(enip_header_t) > bytes_received) {
        ESP_LOGE(TAG, "Response too short for header");
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Response too short");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse response header
    enip_header_t response_header;
    memcpy(&response_header, response_buffer + header_offset, sizeof(response_header));
    
    if (response_header.command != ENIP_SEND_RR_DATA) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Unexpected response command: 0x%04X", response_header.command);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    if (response_header.status != 0) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Response error status: 0x%08lX", (unsigned long)response_header.status);
        return ESP_FAIL;
    }
    
    uint16_t response_length = response_header.length;
    size_t bytes_already_read = header_offset + sizeof(response_header);
    size_t remaining_in_buffer = (bytes_received > bytes_already_read) ? (bytes_received - bytes_already_read) : 0;
    
    // Read remaining data if needed
    if (bytes_received < header_offset + sizeof(response_header) + response_length) {
        size_t remaining = (header_offset + sizeof(response_header) + response_length) - bytes_received;
        if (remaining > sizeof(response_buffer) - bytes_received) {
            remaining = sizeof(response_buffer) - bytes_received;
        }
        if (remaining > 0) {
            size_t additional_received = 0;
            ret = recv_data(sock, response_buffer + bytes_received, remaining, timeout_ms, &additional_received);
            if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "Failed to receive remaining response data");
                unregister_session(sock, session_handle);
                close(sock);
                snprintf(result->error_message, sizeof(result->error_message), "Failed to receive remaining response data");
                return ret;
            }
            bytes_received += additional_received;
            remaining_in_buffer += additional_received;
        }
    }
    
    // Skip Interface Handle (4), Timeout (2), Item Count (2), Address Item (4), Data Item header (4) = 16 bytes
    if (remaining_in_buffer >= 16) {
        bytes_already_read += 16;
        remaining_in_buffer -= 16;
    } else {
        uint8_t skip_buffer[16];
        ret = recv_data(sock, skip_buffer, 16, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive response structure");
            return ret;
        }
    }
    
    // Read CIP response: Service + Reserved + Status + Additional Status Size
    uint8_t cip_status, additional_status_size;
    if (remaining_in_buffer >= 4) {
        cip_status = response_buffer[bytes_already_read + 2];
        additional_status_size = response_buffer[bytes_already_read + 3];
        bytes_already_read += 4;
        remaining_in_buffer -= 4;
    } else {
        uint8_t cip_header[4];
        ret = recv_data(sock, cip_header, 4, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive CIP header");
            return ret;
        }
        cip_status = cip_header[2];
        additional_status_size = cip_header[3];
    }
    
    if (cip_status != 0x00) {
        const char* status_msg = (cip_status == 0x01) ? "Connection failure" :
                                 (cip_status == 0x02) ? "Resource unavailable" :
                                 (cip_status == 0x03) ? "Invalid parameter value" :
                                 (cip_status == 0x04) ? "Path segment error" :
                                 (cip_status == 0x05) ? "Path destination unknown" :
                                 (cip_status == 0x06) ? "Partial transfer" :
                                 (cip_status == 0x07) ? "Connection lost" :
                                 (cip_status == 0x08) ? "Service not supported" :
                                 (cip_status == 0x09) ? "Invalid attribute value" :
                                 (cip_status == 0x0A) ? "Attribute list error" :
                                 (cip_status == 0x0B) ? "Already in requested mode" :
                                 (cip_status == 0x0C) ? "Object state conflict" :
                                 (cip_status == 0x0D) ? "Object already exists" :
                                 (cip_status == 0x0E) ? "Attribute not settable" :
                                 (cip_status == 0x0F) ? "Privilege violation" :
                                 (cip_status == 0x10) ? "Device state conflict" :
                                 (cip_status == 0x11) ? "Reply data too large" :
                                 (cip_status == 0x12) ? "Fragmentation of primitive value" :
                                 (cip_status == 0x13) ? "Not enough data" :
                                 (cip_status == 0x14) ? "Attribute not supported" :
                                 (cip_status == 0x15) ? "Too much data" :
                                 (cip_status == 0x16) ? "Object does not exist" :
                                 (cip_status == 0x1A) ? "Invalid data type" :
                                 (cip_status == 0x1B) ? "Invalid data type for service" :
                                 (cip_status == 0x1C) ? "Data type mismatch" :
                                 (cip_status == 0x1D) ? "Data size mismatch" :
                                 "Unknown error";
        
        bool is_program_tag = (strstr(result->tag_path, "Program:") != NULL);
        if (cip_status == 0x05 && is_program_tag) {
            ESP_LOGE(TAG, "CIP error status 0x%02X for tag '%s': %s (Micro800 does not support program-scoped tags externally)", 
                     cip_status, result->tag_path, status_msg);
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), 
                     "0x%02X (%s). Use global tags", cip_status, status_msg);
        } else {
            ESP_LOGE(TAG, "CIP error status 0x%02X for tag '%s': %s", cip_status, result->tag_path, status_msg);
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "CIP error status: 0x%02X (%s)", cip_status, status_msg);
        }
        return ESP_FAIL;
    }
    
    // Skip additional status if present
    if (additional_status_size > 0) {
        size_t safe_status_size = additional_status_size;
        if (remaining_in_buffer >= safe_status_size) {
            bytes_already_read += safe_status_size;
            remaining_in_buffer -= safe_status_size;
        } else {
            uint8_t *skip_buf = malloc(safe_status_size);
            if (skip_buf) {
                esp_err_t skip_ret = recv_data(sock, skip_buf, safe_status_size, timeout_ms, NULL);
                free(skip_buf);
                if (skip_ret != ESP_OK && skip_ret != ESP_ERR_TIMEOUT) {
                }
            }
        }
    }
    
    // Read data type (2 bytes) and data
    uint16_t data_type;
    if (remaining_in_buffer >= 2) {
        memcpy(&data_type, response_buffer + bytes_already_read, 2);
        bytes_already_read += 2;
        remaining_in_buffer -= 2;
    } else {
        ret = recv_data(sock, &data_type, 2, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive data type");
            return ret;
        }
    }
    
    result->cip_data_type = data_type;
    
    // Calculate remaining data length
    size_t cip_header_bytes = 1 + 1 + 1 + 1 + additional_status_size + 2;
    size_t enip_overhead = 4 + 2 + 2 + 4 + 4;  // Interface Handle + Timeout + Item Count + Items
    size_t cip_response_data_length = 0;
    
    // Prevent integer underflow
    if (response_length > enip_overhead + cip_header_bytes) {
        cip_response_data_length = response_length - enip_overhead - cip_header_bytes;
    }
    
    if (cip_response_data_length == 0) {
        result->data_length = 0;
        result->data = NULL;
        result->success = true;
        result->response_time_ms = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;
        unregister_session(sock, session_handle);
        close(sock);
        return ESP_OK;
    }
    
    // Allocate buffer for data
    uint8_t *data_buffer = malloc(cip_response_data_length);
    if (data_buffer == NULL) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Failed to allocate memory");
        return ESP_ERR_NO_MEM;
    }
    
    // Read data
    if (remaining_in_buffer >= cip_response_data_length) {
        memcpy(data_buffer, response_buffer + bytes_already_read, cip_response_data_length);
    } else {
        if (remaining_in_buffer > 0) {
            memcpy(data_buffer, response_buffer + bytes_already_read, remaining_in_buffer);
            size_t bytes_from_buffer = remaining_in_buffer;
            size_t bytes_needed = cip_response_data_length - bytes_from_buffer;
            ret = recv_data(sock, data_buffer + bytes_from_buffer, bytes_needed, timeout_ms, NULL);
            if (ret != ESP_OK) {
                free(data_buffer);
                unregister_session(sock, session_handle);
                close(sock);
                snprintf(result->error_message, sizeof(result->error_message), "Failed to receive data");
                return ret;
            }
        } else {
            ret = recv_data(sock, data_buffer, cip_response_data_length, timeout_ms, NULL);
            if (ret != ESP_OK) {
                free(data_buffer);
                unregister_session(sock, session_handle);
                close(sock);
                snprintf(result->error_message, sizeof(result->error_message), "Failed to receive data");
                return ret;
            }
        }
    }
    
    result->data = data_buffer;
    result->data_length = cip_response_data_length;
    result->success = true;
    result->response_time_ms = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;
    
    
    unregister_session(sock, session_handle);
    close(sock);
    
    return ESP_OK;
}

// ============================================================================
// Tag Write Operation
// ============================================================================

esp_err_t enip_scanner_write_tag(const ip4_addr_t *ip_address,
                                 const char *tag_path,
                                 const uint8_t *data,
                                 uint16_t data_length,
                                 uint16_t cip_data_type,
                                 uint32_t timeout_ms,
                                 char *error_message)
{
    if (ip_address == NULL || tag_path == NULL || data == NULL || data_length == 0) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid parameters");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    if (error_message) {
        error_message[0] = '\0';
    }
    
    // Thread-safe check of initialization state
    if (s_scanner_mutex == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Scanner not initialized");
        }
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_scanner_mutex, portMAX_DELAY) != pdTRUE) {
        if (error_message) {
            snprintf(error_message, 128, "Failed to acquire mutex");
        }
        return ESP_FAIL;
    }
    
    bool initialized = s_scanner_initialized;
    xSemaphoreGive(s_scanner_mutex);
    
    if (!initialized) {
        if (error_message) {
            snprintf(error_message, 128, "Scanner not initialized");
        }
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t start_time = xTaskGetTickCount();
    
    // Create TCP socket
    int sock = create_tcp_socket(ip_address, timeout_ms);
    if (sock < 0) {
        if (error_message) {
            snprintf(error_message, 128, "Failed to connect to device");
        }
        return ESP_FAIL;
    }
    
    // Register session
    uint32_t session_handle = 0;
    esp_err_t ret = register_session(sock, &session_handle);
    if (ret != ESP_OK) {
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Failed to register session");
        }
        return ret;
    }
    
    // Encode tag path
    uint8_t cip_path[256];
    uint8_t path_size_words = 0;
    ret = encode_tag_path(tag_path, cip_path, sizeof(cip_path), &path_size_words);
    if (ret != ESP_OK) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Failed to encode tag path");
        }
        return ret;
    }
    
    // Encode data using data type handler
    uint8_t encoded_data_buffer[512];  // Large enough for STRING with length prefix
    uint16_t actual_encoded_length = 0;
    
    ret = tag_data_encode_write(cip_data_type, data, data_length,
                                encoded_data_buffer, sizeof(encoded_data_buffer),
                                &actual_encoded_length, error_message);
    if (ret != ESP_OK) {
        unregister_session(sock, session_handle);
        close(sock);
        return ret;
    }
    
    // CIP Service: Write Tag (0x4D)
    uint8_t cip_service = CIP_SERVICE_WRITE;
    uint16_t element_count = 1;
    
    // CIP message length: Service + Path Size + Path + DataType (2 bytes) + Element Count (2 bytes) + Data
    uint16_t cip_message_length = 1 + 1 + (path_size_words * 2) + 2 + 2 + actual_encoded_length;
    
    // SendRRData format
    uint16_t enip_data_length = 4 + 2 + 2 + 4 + 4 + cip_message_length;
    const size_t enip_header_size = 24;
    
    // Build complete packet
    size_t total_packet_size = enip_header_size + enip_data_length;
    uint8_t *packet = malloc(total_packet_size);
    if (packet == NULL) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Failed to allocate memory");
        }
        return ESP_ERR_NO_MEM;
    }
    
    size_t offset = 0;
    
    // Build ENIP header
    uint16_t cmd = ENIP_SEND_RR_DATA;
    memcpy(packet + offset, &cmd, 2);
    offset += 2;
    memcpy(packet + offset, &enip_data_length, 2);
    offset += 2;
    memcpy(packet + offset, &session_handle, 4);
    offset += 4;
    uint32_t status = 0;
    memcpy(packet + offset, &status, 4);
    offset += 4;
    uint64_t sender_context = 0;
    memcpy(packet + offset, &sender_context, 8);
    offset += 8;
    uint32_t options = 0;
    memcpy(packet + offset, &options, 4);
    offset += 4;
    
    // Interface Handle
    uint32_t interface_handle = 0;
    memcpy(packet + offset, &interface_handle, 4);
    offset += 4;
    
    // Timeout
    uint8_t cip_timeout = 0x0A;
    memcpy(packet + offset, &cip_timeout, 1);
    offset += 1;
    packet[offset++] = 0x00;
    
    // Item Count
    uint16_t item_count = 2;
    memcpy(packet + offset, &item_count, 2);
    offset += 2;
    
    // Item 1: Null Address Item
    uint16_t null_item_type = 0x0000;
    uint16_t null_item_length = 0x0000;
    memcpy(packet + offset, &null_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &null_item_length, 2);
    offset += 2;
    
    // Item 2: Unconnected Data Item
    uint16_t data_item_type = 0x00B2;
    memcpy(packet + offset, &data_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &cip_message_length, 2);
    offset += 2;
    
    // CIP Message: Service + Path Size + Path + DataType (2 bytes) + Element Count + Data
    packet[offset++] = cip_service;  // 0x4D (Write Tag)
    packet[offset++] = path_size_words;
    memcpy(packet + offset, cip_path, path_size_words * 2);
    offset += path_size_words * 2;
    memcpy(packet + offset, &cip_data_type, 2);
    offset += 2;
    memcpy(packet + offset, &element_count, 2);
    offset += 2;
    
    // Copy encoded data (with bounds check)
    if (offset + actual_encoded_length > total_packet_size) {
        free(packet);
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Packet too large");
        }
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(packet + offset, encoded_data_buffer, actual_encoded_length);
    offset += actual_encoded_length;
    
    // Send request
    ret = send_data(sock, packet, offset);
    free(packet);
    if (ret != ESP_OK) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Failed to send request");
        }
        return ret;
    }
    
    // Receive response - first get header to determine length
    uint8_t response_buffer[512];
    memset(response_buffer, 0, sizeof(response_buffer));
    size_t bytes_received = 0;
    
    // Receive at least the ENIP header (24 bytes)
    ssize_t recv_ret = recv(sock, response_buffer, sizeof(response_buffer), 0);
    if (recv_ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Timeout waiting for response");
            }
            return ESP_ERR_TIMEOUT;
        }
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Failed to receive response: %d", errno);
        }
        return ESP_FAIL;
    }
    
    if (recv_ret == 0) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Connection closed by peer");
        }
        return ESP_FAIL;
    }
    
    bytes_received = recv_ret;
    
    // Try to read more if we got a partial response
    if (bytes_received < 40) {
        recv_ret = recv(sock, response_buffer + bytes_received, sizeof(response_buffer) - bytes_received, 0);
        if (recv_ret > 0) {
            bytes_received += recv_ret;
        }
    }
    
    
    if (bytes_received < sizeof(enip_header_t)) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Response too short: %zu bytes", bytes_received);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Find SendRRData command
    int header_offset = 0;
    for (int i = 0; i < 8 && i < (int)bytes_received - 1; i += 2) {
        uint16_t cmd = *(uint16_t *)(response_buffer + i);
        if (cmd == ENIP_SEND_RR_DATA) {
            header_offset = i;
            break;
        }
    }
    
    if (header_offset + sizeof(enip_header_t) > bytes_received) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Response too short");
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse response header
    enip_header_t response_header;
    memcpy(&response_header, response_buffer + header_offset, sizeof(response_header));
    
    if (response_header.command != ENIP_SEND_RR_DATA) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Unexpected response command: 0x%04X", response_header.command);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    if (response_header.status != 0) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Response error status: 0x%08lX", (unsigned long)response_header.status);
        }
        return ESP_FAIL;
    }
    
    // Skip to CIP response
    size_t bytes_already_read = header_offset + sizeof(response_header);
    size_t remaining_in_buffer = (bytes_received > bytes_already_read) ? (bytes_received - bytes_already_read) : 0;
    
    // Skip Interface Handle (4), Timeout (2), Item Count (2), Address Item (4), Data Item header (4)
    if (remaining_in_buffer >= 16) {
        bytes_already_read += 16;
        remaining_in_buffer -= 16;
    } else {
        uint8_t skip_buffer[16];
        ret = recv_data(sock, skip_buffer, 16, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Failed to receive response structure");
            }
            return ret;
        }
    }
    
    // Read CIP response: Service + Reserved + Status + Additional Status Size
    uint8_t cip_status, additional_status_size;
    if (remaining_in_buffer >= 4) {
        cip_status = response_buffer[bytes_already_read + 2];
        additional_status_size = response_buffer[bytes_already_read + 3];
        
        // Handle extended status (0xFF)
        if (cip_status == 0xFF && additional_status_size > 0 && remaining_in_buffer >= (4 + additional_status_size)) {
            uint8_t extended_status_code = response_buffer[bytes_already_read + 4];
            cip_status = extended_status_code;
        }
        
        bytes_already_read += 4;
        remaining_in_buffer -= 4;
    } else {
        uint8_t cip_header[4];
        ret = recv_data(sock, cip_header, 4, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Failed to receive CIP header");
            }
            return ret;
        }
        cip_status = cip_header[2];
        additional_status_size = cip_header[3];
    }
    
    if (cip_status != 0x00) {
        const char* status_msg = (cip_status == 0x05) ? "Object does not exist" :
                                 (cip_status == 0x06) ? "Attribute does not exist" :
                                 (cip_status == 0x0A) ? "Attribute not settable" :
                                 (cip_status == 0x14) ? "Attribute not supported" : "Unknown error";
        ESP_LOGE(TAG, "CIP error status 0x%02X for tag '%s': %s", cip_status, tag_path ? tag_path : "(null)", status_msg);
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "CIP error status: 0x%02X (%s)", cip_status, status_msg);
        }
        return ESP_FAIL;
    }
    
    // Skip additional status if present
    if (additional_status_size > 0) {
        size_t safe_status_size = additional_status_size;
        if (remaining_in_buffer >= safe_status_size) {
            bytes_already_read += safe_status_size;
            remaining_in_buffer -= safe_status_size;
        } else {
            uint8_t *skip_buf = malloc(safe_status_size);
            if (skip_buf) {
                esp_err_t skip_ret = recv_data(sock, skip_buf, safe_status_size, timeout_ms, NULL);
                free(skip_buf);
                if (skip_ret != ESP_OK && skip_ret != ESP_ERR_TIMEOUT) {
                }
            }
        }
    }
    
    unregister_session(sock, session_handle);
    close(sock);
    
    return ESP_OK;
}

// ============================================================================
// Tag Result Management
// ============================================================================

void enip_scanner_free_tag_result(enip_scanner_tag_result_t *result)
{
    if (result == NULL) {
        return;
    }
    
    if (result->data != NULL) {
        free(result->data);
        result->data = NULL;
        result->data_length = 0;
    }
}

// ============================================================================
// Data Type Name Helper
// ============================================================================

const char *enip_scanner_get_data_type_name(uint16_t cip_data_type)
{
    switch (cip_data_type) {
        case CIP_DATA_TYPE_BOOL:    return "BOOL";
        case CIP_DATA_TYPE_SINT:    return "SINT";
        case CIP_DATA_TYPE_INT:     return "INT";
        case CIP_DATA_TYPE_DINT:    return "DINT";
        case CIP_DATA_TYPE_LINT:    return "LINT";
        case CIP_DATA_TYPE_USINT:   return "USINT";
        case CIP_DATA_TYPE_UINT:    return "UINT";
        case CIP_DATA_TYPE_UDINT:   return "UDINT";
        case CIP_DATA_TYPE_ULINT:   return "ULINT";
        case CIP_DATA_TYPE_REAL:    return "REAL";
        case CIP_DATA_TYPE_LREAL:   return "LREAL";
        case CIP_DATA_TYPE_STIME:   return "TIME";  // Called "TIME" on Micro800, "STIME" in CIP spec
        case CIP_DATA_TYPE_DATE:    return "DATE";
        case CIP_DATA_TYPE_TIME_OF_DAY: return "TIME_OF_DAY";
        case CIP_DATA_TYPE_DATE_AND_TIME: return "DATE_AND_TIME";
        case CIP_DATA_TYPE_STRING:  return "STRING";
        case CIP_DATA_TYPE_BYTE:    return "BYTE";
        case CIP_DATA_TYPE_WORD:    return "WORD";
        case CIP_DATA_TYPE_DWORD:   return "DWORD";
        case CIP_DATA_TYPE_LWORD:   return "LWORD";
        default:                    return "Unknown";
    }
}

#endif // CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT

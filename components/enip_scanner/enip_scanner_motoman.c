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

#include "enip_scanner_motoman_internal.h"
#include "enip_scanner.h"
#include "esp_err.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT

// ============================================================================
// Motoman CIP Class Constants
// ============================================================================

// Motoman vendor-specific CIP classes (from Manual 165838-1CD)
#define MOTOMAN_CLASS_ALARM_CURRENT 0x70
#define MOTOMAN_CLASS_ALARM_HISTORY 0x71
#define MOTOMAN_CLASS_STATUS 0x72
#define MOTOMAN_CLASS_JOB_INFO 0x73
#define MOTOMAN_CLASS_AXIS_CONFIG 0x74
#define MOTOMAN_CLASS_POSITION 0x75
#define MOTOMAN_CLASS_POSITION_DEVIATION 0x76
#define MOTOMAN_CLASS_TORQUE 0x77
#define MOTOMAN_CLASS_IO_DATA 0x78
#define MOTOMAN_CLASS_REGISTER 0x79
#define MOTOMAN_CLASS_VARIABLE_B 0x7A
#define MOTOMAN_CLASS_VARIABLE_I 0x7B
#define MOTOMAN_CLASS_VARIABLE_D 0x7C
#define MOTOMAN_CLASS_VARIABLE_R 0x7D
#define MOTOMAN_CLASS_VARIABLE_S 0x8C
#define MOTOMAN_CLASS_VARIABLE_P 0x7F
#define MOTOMAN_CLASS_VARIABLE_BP 0x80
#define MOTOMAN_CLASS_VARIABLE_EX 0x81

// CIP Services
#define CIP_SERVICE_GET_ATTRIBUTE_ALL 0x01
#define CIP_SERVICE_GET_ATTRIBUTE_SINGLE 0x0E
#define CIP_SERVICE_SET_ATTRIBUTE_SINGLE 0x10
#define CIP_SERVICE_SET_ATTRIBUTE_ALL 0x02

// ============================================================================
// Internal Helper Functions
// ============================================================================

/**
 * @brief Build CIP path for Motoman vendor-specific class
 */
static esp_err_t build_motoman_cip_path(uint16_t cip_class, uint16_t instance, uint8_t attribute,
                                        uint8_t *path_buffer, size_t buffer_size, uint8_t *path_length_words) {
    if (path_buffer == NULL || path_length_words == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (buffer_size < 10) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t offset = 0;
    
    // Class segment (16-bit class for vendor-specific classes)
    path_buffer[offset++] = 0x21;  // 16-bit class segment (format 0x21)
    path_buffer[offset++] = cip_class & 0xFF;
    path_buffer[offset++] = (cip_class >> 8) & 0xFF;
    
    // Instance segment (16-bit)
    path_buffer[offset++] = 0x25;  // 16-bit instance segment (format 0x25)
    path_buffer[offset++] = instance & 0xFF;
    path_buffer[offset++] = (instance >> 8) & 0xFF;
    
    // Attribute segment (8-bit)
    path_buffer[offset++] = 0x30;  // 8-bit attribute segment (format 0x30)
    path_buffer[offset++] = attribute;
    
    // Path must be padded to even number of bytes (word-aligned)
    uint8_t path_padded_length = offset;
    if (offset % 2 != 0) {
        path_buffer[offset++] = 0x00;  // Pad byte
        path_padded_length = offset;
    }
    
    // Path size in words (16-bit words)
    *path_length_words = path_padded_length / 2;
    
    return ESP_OK;
}

/**
 * @brief Send CIP message to Motoman robot
 */
static esp_err_t send_motoman_cip_message(const ip4_addr_t *ip_address, uint16_t cip_class,
                                          uint16_t instance, uint8_t attribute, uint8_t service,
                                          const uint8_t *data, uint16_t data_length,
                                          uint8_t *response_buffer, size_t response_buffer_size,
                                          size_t *response_length, uint32_t timeout_ms,
                                          char *error_message) {
    if (ip_address == NULL || response_buffer == NULL || response_length == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
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
            snprintf(error_message, 128, "Failed to register session: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    
    // Build CIP path
    uint8_t cip_path[8];
    uint8_t path_size_words = 0;
    ret = build_motoman_cip_path(cip_class, instance, attribute, cip_path, sizeof(cip_path), &path_size_words);
    if (ret != ESP_OK) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Failed to build CIP path");
        }
        return ret;
    }
    
    uint8_t path_padded_length = path_size_words * 2;
    
    // Calculate CIP message length: Service (1) + Path Size (1) + Path (padded) + Data
    uint16_t cip_message_length = 1 + 1 + path_padded_length + data_length;
    
    // SendRRData format
    uint16_t enip_data_length = 4 + 2 + 2 + 4 + 4 + cip_message_length;
    
    // Build packet
    size_t total_packet_size = 24 + enip_data_length;  // ENIP header + data
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
    uint8_t cip_timeout = (timeout_ms / 1000) > 255 ? 255 : (timeout_ms / 1000);
    if (cip_timeout == 0) cip_timeout = 1;
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
    uint16_t request_data_item_type = 0x00B2;
    memcpy(packet + offset, &request_data_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &cip_message_length, 2);
    offset += 2;
    
    // CIP Message: Service + Path Size + Path + Data
    // Format: [Service] [Path Size] [Path] [Data]
    packet[offset++] = service;
    packet[offset++] = path_size_words;
    memcpy(packet + offset, cip_path, path_padded_length);
    offset += path_padded_length;
    
    // Add data if writing
    if (data != NULL && data_length > 0) {
        memcpy(packet + offset, data, data_length);
        offset += data_length;
    }
    
    // Send packet
    ret = send_data(sock, packet, offset);
    free(packet);
    
    if (ret != ESP_OK) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Failed to send CIP message");
        }
        return ret;
    }
    
    // Receive response
    uint8_t response[512];
    ssize_t recv_ret = recv(sock, response, sizeof(response), 0);
    if (recv_ret < 0) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Failed to receive response: %d", errno);
        }
        return ESP_FAIL;
    }
    
    size_t bytes_received = (size_t)recv_ret;
    
    // Try to read more if needed
    if (bytes_received < 40) {
        recv_ret = recv(sock, response + bytes_received, sizeof(response) - bytes_received, 0);
        if (recv_ret > 0) {
            bytes_received += (size_t)recv_ret;
        }
    }
    
    // Find SendRRData command in response
    int header_offset = 0;
    for (int i = 0; i < 8 && i < (int)bytes_received - 1; i += 2) {
        uint16_t cmd_check = *(uint16_t *)(response + i);
        if (cmd_check == ENIP_SEND_RR_DATA) {
            header_offset = i;
            break;
        }
    }
    
    if (header_offset + 24 > bytes_received) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Response too short");
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse ENIP header
    enip_header_t response_header;
    memcpy(&response_header, response + header_offset, sizeof(response_header));
    
    if (response_header.status != 0) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "ENIP error status: 0x%08lX", (unsigned long)response_header.status);
        }
        return ESP_FAIL;
    }
    
    // Parse SendRRData response structure
    // After ENIP header (24 bytes):
    // - Interface Handle (4 bytes)
    // - Timeout (2 bytes)
    // - Item Count (2 bytes) = 0x0002
    // - Item 1: Null Address Item (4 bytes)
    // - Item 2: Unconnected Data Item header (4 bytes)
    // - CIP response data
    
    size_t enip_data_offset = header_offset + 24;
    if (enip_data_offset + 16 > bytes_received) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "ENIP data too short");
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Skip Interface Handle (4), Timeout (2), Item Count (2) = 8 bytes
    size_t item_offset = enip_data_offset + 8;
    
    // Skip Item 1: Null Address Item (4 bytes)
    item_offset += 4;
    
    // Read Item 2: Unconnected Data Item
    if (item_offset + 4 > bytes_received) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Data item header too short");
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    uint16_t response_data_item_type = *(uint16_t *)(response + item_offset);
    uint16_t data_item_length = *(uint16_t *)(response + item_offset + 2);
    item_offset += 4;
    
    if (response_data_item_type != 0x00B2) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Unexpected data item type: 0x%04X", response_data_item_type);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Check CIP general status (first byte of CIP response)
    if (item_offset + 1 > bytes_received) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "CIP response too short");
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    uint8_t cip_general_status = response[item_offset];
    if (cip_general_status != 0) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "CIP error status: 0x%02X", cip_general_status);
        }
        return ESP_FAIL;
    }
    
    // Extract CIP data (skip general status byte)
    size_t data_offset = item_offset + 1;
    size_t data_available = bytes_received - data_offset;
    
    // Limit to actual CIP data length
    if (data_available > data_item_length - 1) {
        data_available = data_item_length - 1;
    }
    
    if (data_available > response_buffer_size) {
        data_available = response_buffer_size;
    }
    
    memcpy(response_buffer, response + data_offset, data_available);
    *response_length = data_available;
    
    unregister_session(sock, session_handle);
    close(sock);
    
    return ESP_OK;
}

// ============================================================================
// Public API Functions
// ============================================================================

esp_err_t enip_scanner_motoman_read_status(const ip4_addr_t *ip_address,
                                           enip_scanner_motoman_status_t *status,
                                           uint32_t timeout_ms) {
    if (ip_address == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(status, 0, sizeof(enip_scanner_motoman_status_t));
    status->ip_address = *ip_address;
    
    // Read Class 0x72, Instance 1, Get_Attribute_All (reads both Data 1 and Data 2)
    uint8_t response[16];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_STATUS, 1, 0,
                                             CIP_SERVICE_GET_ATTRIBUTE_ALL, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        snprintf(status->error_message, sizeof(status->error_message), "%s", error_msg);
        return ret;
    }
    
    if (response_length < 8) {
        snprintf(status->error_message, sizeof(status->error_message), "Response too short: %zu bytes", response_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse Data 1 (4 bytes)
    status->data1 = (uint32_t)(response[0] | (response[1] << 8) | (response[2] << 16) | (response[3] << 24));
    
    // Parse Data 2 (4 bytes)
    status->data2 = (uint32_t)(response[4] | (response[5] << 8) | (response[6] << 16) | (response[7] << 24));
    
    status->success = true;
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_read_io(const ip4_addr_t *ip_address, uint16_t signal_number,
                                       uint8_t *value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL || value == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    // Instance = signal_number / 10 (per Motoman manual)
    uint16_t instance = signal_number / 10;
    
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_IO_DATA, instance, 1,
                                             CIP_SERVICE_GET_ATTRIBUTE_SINGLE, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    if (response_length < 1) {
        if (error_message) {
            snprintf(error_message, 128, "Response too short: %zu bytes", response_length);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    *value = response[0];
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_write_io(const ip4_addr_t *ip_address, uint16_t signal_number,
                                        uint8_t value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    // Instance = signal_number / 10 (per Motoman manual)
    uint16_t instance = signal_number / 10;
    
    uint8_t data[1] = {value};
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_IO_DATA, instance, 1,
                                             CIP_SERVICE_SET_ATTRIBUTE_SINGLE, data, 1,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_read_variable_b(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               uint8_t *value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL || value == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    // Instance = variable_number (or variable_number + 1 depending on RS022 parameter)
    // Default: instance = variable_number + 1 (RS022=0)
    uint16_t instance = variable_number + 1;
    
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_B, instance, 1,
                                             CIP_SERVICE_GET_ATTRIBUTE_SINGLE, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    if (response_length < 1) {
        if (error_message) {
            snprintf(error_message, 128, "Response too short: %zu bytes", response_length);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    *value = response[0];
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_write_variable_b(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 uint8_t value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = variable_number + 1;
    uint8_t data[1] = {value};
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_B, instance, 1,
                                             CIP_SERVICE_SET_ATTRIBUTE_SINGLE, data, 1,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_read_variable_i(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               int16_t *value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL || value == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = variable_number + 1;
    
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_I, instance, 1,
                                             CIP_SERVICE_GET_ATTRIBUTE_SINGLE, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    if (response_length < 2) {
        if (error_message) {
            snprintf(error_message, 128, "Response too short: %zu bytes", response_length);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    *value = (int16_t)(response[0] | (response[1] << 8));
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_write_variable_i(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 int16_t value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = variable_number + 1;
    uint8_t data[2];
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_I, instance, 1,
                                             CIP_SERVICE_SET_ATTRIBUTE_SINGLE, data, 2,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_read_variable_d(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               int32_t *value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL || value == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = variable_number + 1;
    
    uint8_t response[8];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_D, instance, 1,
                                             CIP_SERVICE_GET_ATTRIBUTE_SINGLE, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    if (response_length < 4) {
        if (error_message) {
            snprintf(error_message, 128, "Response too short: %zu bytes", response_length);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    *value = (int32_t)(response[0] | (response[1] << 8) | (response[2] << 16) | (response[3] << 24));
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_write_variable_d(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 int32_t value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = variable_number + 1;
    uint8_t data[4];
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    data[2] = (value >> 16) & 0xFF;
    data[3] = (value >> 24) & 0xFF;
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_D, instance, 1,
                                             CIP_SERVICE_SET_ATTRIBUTE_SINGLE, data, 4,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_read_variable_r(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               float *value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL || value == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = variable_number + 1;
    
    uint8_t response[8];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_R, instance, 1,
                                             CIP_SERVICE_GET_ATTRIBUTE_SINGLE, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    if (response_length < 4) {
        if (error_message) {
            snprintf(error_message, 128, "Response too short: %zu bytes", response_length);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    union { uint32_t u32; float f; } converter;
    converter.u32 = (uint32_t)(response[0] | (response[1] << 8) | (response[2] << 16) | (response[3] << 24));
    *value = converter.f;
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_write_variable_r(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 float value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = variable_number + 1;
    union { uint32_t u32; float f; } converter;
    converter.f = value;
    uint8_t data[4];
    data[0] = converter.u32 & 0xFF;
    data[1] = (converter.u32 >> 8) & 0xFF;
    data[2] = (converter.u32 >> 16) & 0xFF;
    data[3] = (converter.u32 >> 24) & 0xFF;
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_R, instance, 1,
                                             CIP_SERVICE_SET_ATTRIBUTE_SINGLE, data, 4,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_read_register(const ip4_addr_t *ip_address, uint16_t register_number,
                                             uint16_t *value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL || value == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    // Instance = register_number + 1 (per Motoman manual, RS022=0)
    uint16_t instance = register_number + 1;
    
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_REGISTER, instance, 1,
                                             CIP_SERVICE_GET_ATTRIBUTE_SINGLE, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    if (response_length < 2) {
        if (error_message) {
            snprintf(error_message, 128, "Response too short: %zu bytes", response_length);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    *value = (uint16_t)(response[0] | (response[1] << 8));
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_write_register(const ip4_addr_t *ip_address, uint16_t register_number,
                                              uint16_t value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = register_number + 1;
    uint8_t data[2];
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_REGISTER, instance, 1,
                                             CIP_SERVICE_SET_ATTRIBUTE_SINGLE, data, 2,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "%s", error_msg);
        }
        return ret;
    }
    
    return ESP_OK;
}

#endif // CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT


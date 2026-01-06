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
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT

static const char *TAG = "enip_scanner_motoman";

static bool s_motoman_rs022_instance_direct = false;

void enip_scanner_motoman_set_rs022_instance_direct(bool instance_direct)
{
    s_motoman_rs022_instance_direct = instance_direct;
}

bool enip_scanner_motoman_get_rs022_instance_direct(void)
{
    return s_motoman_rs022_instance_direct;
}

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
                                        bool include_attribute,
                                        uint8_t *path_buffer, size_t buffer_size, uint8_t *path_length_words) {
    if (path_buffer == NULL || path_length_words == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (buffer_size < 10) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t offset = 0;
    
    // Class segment (use 8-bit when possible)
    if (cip_class <= 0xFF) {
        path_buffer[offset++] = 0x20;  // 8-bit class segment
        path_buffer[offset++] = (uint8_t)cip_class;
    } else {
        path_buffer[offset++] = 0x21;  // 16-bit class segment
        path_buffer[offset++] = cip_class & 0xFF;
        path_buffer[offset++] = (cip_class >> 8) & 0xFF;
    }
    
    // Instance segment (use 8-bit when possible)
    if (instance <= 0xFF) {
        path_buffer[offset++] = 0x24;  // 8-bit instance segment
        path_buffer[offset++] = (uint8_t)instance;
    } else {
        path_buffer[offset++] = 0x25;  // 16-bit instance segment
        path_buffer[offset++] = instance & 0xFF;
        path_buffer[offset++] = (instance >> 8) & 0xFF;
    }
    
    if (include_attribute) {
        // Attribute segment (8-bit)
        path_buffer[offset++] = 0x30;  // 8-bit attribute segment
        path_buffer[offset++] = attribute;
    }
    
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

static uint16_t motoman_variable_instance(uint16_t variable_number)
{
    return variable_number + (s_motoman_rs022_instance_direct ? 0 : 1);
}

static uint16_t motoman_register_instance(uint16_t register_number)
{
    return register_number + (s_motoman_rs022_instance_direct ? 0 : 1);
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
    
    // Build CIP path (need at least 10 bytes: 3 class + 3 instance + 2 attribute + 2 padding = 10)
    uint8_t cip_path[10];
    uint8_t path_size_words = 0;
    bool include_attribute = (service == CIP_SERVICE_GET_ATTRIBUTE_SINGLE ||
                              service == CIP_SERVICE_SET_ATTRIBUTE_SINGLE);
    ret = build_motoman_cip_path(cip_class, instance, attribute, include_attribute,
                                 cip_path, sizeof(cip_path), &path_size_words);
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
    
    // Try to read more if we got very little data (TCP may deliver in multiple packets)
    // But only try once with a short timeout to avoid hanging
    if (bytes_received < 40 && bytes_received < sizeof(response)) {
        // Set a short timeout for the second read attempt (100ms)
        struct timeval short_timeout = { .tv_sec = 0, .tv_usec = 100000 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &short_timeout, sizeof(short_timeout));
        
        recv_ret = recv(sock, response + bytes_received, sizeof(response) - bytes_received, 0);
        if (recv_ret > 0) {
            bytes_received += (size_t)recv_ret;
        }
        // Note: We're closing the socket soon anyway, so no need to restore timeout
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
    
    // CIP response: [service|0x80][reserved][general status][additional status size][additional status...][data]
    if (item_offset + 4 > bytes_received) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "CIP response too short");
        }
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t cip_general_status = response[item_offset + 2];
    uint8_t cip_additional_status_size = response[item_offset + 3]; // size in 16-bit words
    if (cip_general_status != 0) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            const char* status_msg = (cip_general_status == 0x01) ? "Connection failure" :
                                     (cip_general_status == 0x02) ? "Resource unavailable" :
                                     (cip_general_status == 0x03) ? "Invalid parameter value" :
                                     (cip_general_status == 0x04) ? "Path segment error" :
                                     (cip_general_status == 0x05) ? "Path destination unknown (Object does not exist)" :
                                     (cip_general_status == 0x06) ? "Partial transfer" :
                                     (cip_general_status == 0x07) ? "Connection lost" :
                                     (cip_general_status == 0x08) ? "Service not supported" :
                                     (cip_general_status == 0x09) ? "Invalid attribute value" :
                                     (cip_general_status == 0x0A) ? "Attribute list error" :
                                     (cip_general_status == 0x0B) ? "Already in requested mode" :
                                     (cip_general_status == 0x0C) ? "Object state conflict" :
                                     (cip_general_status == 0x0D) ? "Object already exists" :
                                     (cip_general_status == 0x0E) ? "Attribute not settable" :
                                     (cip_general_status == 0x0F) ? "Privilege violation" :
                                     (cip_general_status == 0x10) ? "Device state conflict" :
                                     (cip_general_status == 0x11) ? "Reply data too large" :
                                     (cip_general_status == 0x12) ? "Fragmentation of a primitive value" :
                                     (cip_general_status == 0x13) ? "Not enough data" :
                                     (cip_general_status == 0x14) ? "Attribute not supported" :
                                     (cip_general_status == 0x15) ? "Too much data" :
                                     (cip_general_status == 0x16) ? "Object does not exist" :
                                     (cip_general_status == 0x17) ? "Service fragmentation sequence not in progress" :
                                     (cip_general_status == 0x18) ? "No stored attribute data" :
                                     (cip_general_status == 0x19) ? "Store operation failure" :
                                     (cip_general_status == 0x1A) ? "Routing failure - request packet too large" :
                                     (cip_general_status == 0x1B) ? "Routing failure - response packet too large" :
                                     (cip_general_status == 0x1C) ? "Missing attribute list entry data" :
                                     (cip_general_status == 0x1D) ? "Invalid attribute value list" :
                                     (cip_general_status == 0x1E) ? "Embedded service error" :
                                     (cip_general_status == 0x1F) ? "Vendor specific error" :
                                     (cip_general_status == 0x20) ? "Invalid parameter" :
                                     (cip_general_status == 0x21) ? "Write-once value or medium already written" :
                                     (cip_general_status == 0x22) ? "Invalid reply received" :
                                     (cip_general_status == 0x23) ? "Buffer overflow" :
                                     (cip_general_status == 0x24) ? "Message format error" :
                                     (cip_general_status == 0x25) ? "Key failure in path" :
                                     (cip_general_status == 0x26) ? "Path size invalid" :
                                     (cip_general_status == 0x27) ? "Unexpected attribute in list" :
                                     (cip_general_status == 0x28) ? "Invalid member ID" :
                                     (cip_general_status == 0x29) ? "Member not settable" :
                                     (cip_general_status == 0x2A) ? "Group 2 only server general failure" :
                                     (cip_general_status == 0x2B) ? "Unknown Modbus error" :
                                     (cip_general_status == 0x81) ? "Vendor-specific: Invalid instance or attribute (Motoman)" :
                                     "Vendor-specific or extended error";
            snprintf(error_message, 128, "CIP error status: 0x%02X (%s)", cip_general_status, status_msg);
        }
        return ESP_FAIL;
    }
    
    // Initial data offset: skip CIP header (4 bytes) + additional status
    size_t data_offset = item_offset + 4 + (cip_additional_status_size * 2);
    size_t data_available = bytes_received - data_offset;
    
    // Calculate expected data length: data_item_length is the total CIP message length
    // CIP response structure: [service|0x80][reserved][general status][additional status size][additional status...][data]
    // So: data_item_length = 4 (CIP header) + (cip_additional_status_size * 2) + data_length
    // Therefore: data_length = data_item_length - 4 - (cip_additional_status_size * 2)
    size_t expected_data_length = 0;
    if (data_item_length > 4) {
        expected_data_length = data_item_length - 4;  // Subtract CIP header (4 bytes)
    }
    if (expected_data_length > (cip_additional_status_size * 2)) {
        expected_data_length -= (cip_additional_status_size * 2);  // Subtract additional status
    }
    
    // For Get_Attribute_All, the data should start immediately after the CIP header
    // Do NOT skip bytes for Get_Attribute_All - the data is already at the correct offset
    // Only for Get_Attribute_Single might there be path info in the response
    if (service == CIP_SERVICE_GET_ATTRIBUTE_ALL) {
        // Data should be at data_offset already, don't skip anything
        ESP_LOGD(TAG, "Get_Attribute_All: data starts at offset %zu, length %zu", data_offset, data_available);
    } else if (service == CIP_SERVICE_GET_ATTRIBUTE_SINGLE && data_available >= 8) {
        // For Get_Attribute_Single, check if there's path info (8 bytes) before the data
        // Path info typically has segment type bytes (0x20-0x3F) followed by segment data
        // If first byte is a segment type and it's followed by non-zero data, it's likely path info
        uint8_t b0 = response[data_offset];
        bool might_be_path = (b0 >= 0x20 && b0 <= 0x3F);
        
        // Check if bytes 1-7 are also consistent with path structure
        if (might_be_path) {
            // Path segments are usually 2-4 bytes each, so 8 bytes could be 2-4 segments
            // If we see multiple segment type bytes, it's likely path info
            bool has_multiple_segments = false;
            for (size_t i = 2; i < 8 && i < data_available; i += 2) {
                if (response[data_offset + i] >= 0x20 && response[data_offset + i] <= 0x3F) {
                    has_multiple_segments = true;
                    break;
                }
            }
            
            if (has_multiple_segments && (data_offset + 8) < bytes_received) {
                ESP_LOGI(TAG, "Detected path bytes in Get_Attribute_Single response, adjusting data_offset by 8 bytes");
                data_offset += 8;
                data_available = bytes_received - data_offset;
                // Recalculate expected length to account for the path bytes
                if (expected_data_length > 0 && expected_data_length >= 8) {
                    expected_data_length -= 8;
                }
            }
        }
    }
    
    // Determine copy length: use what we actually received, but don't exceed buffer size
    size_t copy_length = data_available;
    
    // For Get_Attribute_All, we want all available data, not limited by expected_data_length
    // (expected_data_length might be calculated incorrectly due to path info)
    // Only limit by expected_data_length for single attribute reads
    if (service != CIP_SERVICE_GET_ATTRIBUTE_ALL && expected_data_length > 0 && copy_length > expected_data_length) {
        copy_length = expected_data_length;
    }
    
    // Limit to buffer size
    if (copy_length > response_buffer_size) {
        copy_length = response_buffer_size;
    }
    
    // Copy the data (even if copy_length is 0, this is safe)
    if (copy_length > 0) {
        memcpy(response_buffer, response + data_offset, copy_length);
    }
    *response_length = copy_length;
    
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
    
    // Instance = variable_number (+1 when RS022=0)
    uint16_t instance = motoman_variable_instance(variable_number);
    
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
    
    uint16_t instance = motoman_variable_instance(variable_number);
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
    
    uint16_t instance = motoman_variable_instance(variable_number);
    
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
    
    uint16_t instance = motoman_variable_instance(variable_number);
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
    
    uint16_t instance = motoman_variable_instance(variable_number);
    
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
    
    uint16_t instance = motoman_variable_instance(variable_number);
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
    
    uint16_t instance = motoman_variable_instance(variable_number);
    
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
    
    uint16_t instance = motoman_variable_instance(variable_number);
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
    
    // Instance = register_number (+1 when RS022=0)
    uint16_t instance = motoman_register_instance(register_number);
    
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
    
    uint16_t instance = motoman_register_instance(register_number);
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

// ============================================================================
// Alarm Functions (Classes 0x70, 0x71)
// ============================================================================

static esp_err_t read_motoman_alarm_attributes(const ip4_addr_t *ip_address, uint16_t cip_class,
                                               uint16_t instance, enip_scanner_motoman_alarm_t *alarm,
                                               uint32_t timeout_ms) {
    // Use Get_Attribute_All to read all 60 bytes at once (more efficient than 5 separate requests)
    // Response format: Alarm code (4) + Alarm data (4) + Alarm data type (4) + Date/time (16) + Alarm string (32) = 60 bytes
    uint8_t response[128];  // Large enough for 60 bytes + CIP headers
    size_t response_length = 0;
    char error_msg[128];
    
    // Use Get_Attribute_All (0x01) with attribute 0 (omitted for Get_Attribute_All)
    // This reads all 5 attributes in one request: 60 bytes total
    esp_err_t ret = send_motoman_cip_message(ip_address, cip_class, instance, 0,
                                             CIP_SERVICE_GET_ATTRIBUTE_ALL, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    if (ret != ESP_OK) {
        snprintf(alarm->error_message, sizeof(alarm->error_message), "%s", error_msg);
        return ret;
    }
    
    // We expect 60 bytes of data (4+4+4+16+32), but may get 52 bytes if path info is included
    // The actual alarm data is 60 bytes, but the response might have 8 bytes of path info before it
    // Check if we have enough data - need at least 52 bytes (60 - 8 for path), ideally 60
    if (response_length < 52) {
        snprintf(alarm->error_message, sizeof(alarm->error_message), 
                 "Alarm response too short: expected at least 52 bytes, got %zu bytes", response_length);
        ESP_LOGE(TAG, "Expected at least 52 bytes, got %zu bytes", response_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Determine data offset: if we got 52 bytes, the first 8 bytes might be path info
    // If we got 60 bytes, the data starts at offset 0
    size_t data_offset = 0;
    if (response_length == 52) {
        // 52 bytes = 8 bytes path + 52 bytes data, but we need 60 bytes of data
        // This suggests the path info (8 bytes) was already stripped, but we're missing 8 bytes
        // OR the data starts at offset 0 and we only got 52 bytes total
        // Check if first 8 bytes look like path info (usually zeros or small values)
        bool looks_like_path = true;
        for (size_t i = 0; i < 8 && i < response_length; i++) {
            if (response[i] != 0 && response[i] > 0x20) {
                looks_like_path = false;
                break;
            }
        }
        if (looks_like_path) {
            data_offset = 8;
            // But we only have 52 bytes total, so we can't get all 60 bytes
            // This means we're missing the last 8 bytes, which would be part of the alarm string
        }
    } else if (response_length >= 60) {
        // We have 60+ bytes, data should start at offset 0
        data_offset = 0;
    }
    
    // Parse the alarm data starting at data_offset
    size_t offset = data_offset;
    size_t available = response_length - data_offset;
    
    // Attribute 1: Alarm code (4 bytes)
    if (available < 4) {
        snprintf(alarm->error_message, sizeof(alarm->error_message), "Not enough data for alarm code");
        return ESP_ERR_INVALID_RESPONSE;
    }
    alarm->alarm_code = (uint32_t)(response[offset] | (response[offset+1] << 8) | 
                                   (response[offset+2] << 16) | (response[offset+3] << 24));
    offset += 4;
    available -= 4;
    
    // Attribute 2: Alarm data (4 bytes)
    if (available < 4) {
        snprintf(alarm->error_message, sizeof(alarm->error_message), "Not enough data for alarm data");
        return ESP_ERR_INVALID_RESPONSE;
    }
    alarm->alarm_data = (uint32_t)(response[offset] | (response[offset+1] << 8) | 
                                   (response[offset+2] << 16) | (response[offset+3] << 24));
    offset += 4;
    available -= 4;
    
    // Attribute 3: Alarm data type (4 bytes)
    if (available < 4) {
        snprintf(alarm->error_message, sizeof(alarm->error_message), "Not enough data for alarm data type");
        return ESP_ERR_INVALID_RESPONSE;
    }
    alarm->alarm_data_type = (uint32_t)(response[offset] | (response[offset+1] << 8) | 
                                        (response[offset+2] << 16) | (response[offset+3] << 24));
    offset += 4;
    available -= 4;
    
    // Attribute 4: Alarm occurrence date/time (16 bytes)
    // Format: "YYYY/MM/DD HH:MM" (16-byte fixed string)
    memset(alarm->alarm_date_time, 0, sizeof(alarm->alarm_date_time));
    size_t date_copy_len = (available < 16) ? available : 16;
    memcpy(alarm->alarm_date_time, response + offset, date_copy_len);
    alarm->alarm_date_time[16] = '\0';  // Ensure null termination
    
    // Find the actual string end (trim trailing spaces/nulls)
    size_t date_str_len = 0;
    for (size_t i = 0; i < date_copy_len; i++) {
        if (response[offset + i] >= ' ' && response[offset + i] <= '~') {
            date_str_len = i + 1;
        } else if (response[offset + i] == '\0') {
            break;
        }
    }
    if (date_str_len > 0 && date_str_len < 16) {
        alarm->alarm_date_time[date_str_len] = '\0';
    }
    offset += 16;
    available = (available > 16) ? (available - 16) : 0;
    
    // Attribute 5: Alarm string (32 bytes)
    // Format: Alarm name string (32-byte fixed string)
    memset(alarm->alarm_string, 0, sizeof(alarm->alarm_string));
    size_t str_copy_len = (available < 32) ? available : 32;
    memcpy(alarm->alarm_string, response + offset, str_copy_len);
    alarm->alarm_string[32] = '\0';  // Ensure null termination
    
    // Find the actual string end (trim trailing spaces/nulls)
    size_t str_len = 0;
    for (size_t i = 0; i < str_copy_len; i++) {
        if (response[offset + i] >= ' ' && response[offset + i] <= '~') {
            str_len = i + 1;
        } else if (response[offset + i] == '\0') {
            break;
        }
    }
    if (str_len > 0 && str_len < 32) {
        alarm->alarm_string[str_len] = '\0';
    }
    
    alarm->success = true;
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_read_alarm(const ip4_addr_t *ip_address, uint8_t alarm_instance,
                                         enip_scanner_motoman_alarm_t *alarm, uint32_t timeout_ms) {
    if (ip_address == NULL || alarm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(alarm, 0, sizeof(enip_scanner_motoman_alarm_t));
    alarm->ip_address = *ip_address;
    
    // Instance: 1=Latest alarm, 2=Alarm immediately before 1, 3=Alarm immediately before 2, 4=Alarm immediately before 3
    if (alarm_instance < 1 || alarm_instance > 4) {
        snprintf(alarm->error_message, sizeof(alarm->error_message), "Invalid alarm instance (must be 1-4)");
        return ESP_ERR_INVALID_ARG;
    }
    
    return read_motoman_alarm_attributes(ip_address, MOTOMAN_CLASS_ALARM_CURRENT, alarm_instance, alarm, timeout_ms);
}

esp_err_t enip_scanner_motoman_read_alarm_history(const ip4_addr_t *ip_address, uint16_t alarm_instance,
                                                 enip_scanner_motoman_alarm_t *alarm, uint32_t timeout_ms) {
    if (ip_address == NULL || alarm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(alarm, 0, sizeof(enip_scanner_motoman_alarm_t));
    alarm->ip_address = *ip_address;
    
    // Instance ranges:
    // 1-100: Major failure
    // 1001-1100: Minor failure
    // 2001-2100: User (System)
    // 3001-3100: User (User)
    // 4001-4100: Off-line
    if ((alarm_instance < 1 || alarm_instance > 100) &&
        (alarm_instance < 1001 || alarm_instance > 1100) &&
        (alarm_instance < 2001 || alarm_instance > 2100) &&
        (alarm_instance < 3001 || alarm_instance > 3100) &&
        (alarm_instance < 4001 || alarm_instance > 4100)) {
        snprintf(alarm->error_message, sizeof(alarm->error_message), "Invalid alarm history instance");
        return ESP_ERR_INVALID_ARG;
    }
    
    return read_motoman_alarm_attributes(ip_address, MOTOMAN_CLASS_ALARM_HISTORY, alarm_instance, alarm, timeout_ms);
}

// ============================================================================
// Job Information (Class 0x73)
// ============================================================================

esp_err_t enip_scanner_motoman_read_job_info(const ip4_addr_t *ip_address,
                                            enip_scanner_motoman_job_info_t *job_info,
                                            uint32_t timeout_ms) {
    if (ip_address == NULL || job_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(job_info, 0, sizeof(enip_scanner_motoman_job_info_t));
    job_info->ip_address = *ip_address;
    
    // Instance 1, Get_Attribute_All
    uint8_t response[44];  // Job name (32) + Line number (4) + Step number (4) + Speed override (4) = 44 bytes
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_JOB_INFO, 1, 0,
                                             CIP_SERVICE_GET_ATTRIBUTE_ALL, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        snprintf(job_info->error_message, sizeof(job_info->error_message), "%s", error_msg);
        return ret;
    }
    
    if (response_length < 44) {
        snprintf(job_info->error_message, sizeof(job_info->error_message), "Response too short: %zu bytes", response_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse job name (32 bytes)
    size_t job_name_len = response_length < 32 ? response_length : 32;
    memcpy(job_info->job_name, response, job_name_len);
    job_info->job_name[job_name_len] = '\0';
    
    // Parse line number (4 bytes)
    job_info->line_number = (uint32_t)(response[32] | (response[33] << 8) | (response[34] << 16) | (response[35] << 24));
    
    // Parse step number (4 bytes)
    job_info->step_number = (uint32_t)(response[36] | (response[37] << 8) | (response[38] << 16) | (response[39] << 24));
    
    // Parse speed override (4 bytes, unit: 0.01%)
    job_info->speed_override = (uint32_t)(response[40] | (response[41] << 8) | (response[42] << 16) | (response[43] << 24));
    
    job_info->success = true;
    return ESP_OK;
}

// ============================================================================
// Axis Configuration (Class 0x74)
// ============================================================================

esp_err_t enip_scanner_motoman_read_axis_config(const ip4_addr_t *ip_address, uint16_t control_group,
                                                enip_scanner_motoman_axis_config_t *config, uint32_t timeout_ms) {
    if (ip_address == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(enip_scanner_motoman_axis_config_t));
    config->ip_address = *ip_address;
    
    // Instance ranges: 1-8 (Robot pulse), 11-18 (Base pulse), 21-44 (Station pulse), 101-108 (Robot coordinate), 111-118 (Base linear)
    uint8_t response[32];  // 8 axis coordinate names (4 bytes each) = 32 bytes
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_AXIS_CONFIG, control_group, 0,
                                             CIP_SERVICE_GET_ATTRIBUTE_ALL, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        snprintf(config->error_message, sizeof(config->error_message), "%s", error_msg);
        return ret;
    }
    
    if (response_length < 32) {
        snprintf(config->error_message, sizeof(config->error_message), "Response too short: %zu bytes", response_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse 8 axis coordinate names (4 bytes each)
    for (int i = 0; i < 8; i++) {
        memcpy(config->axis_names[i], response + (i * 4), 4);
        config->axis_names[i][4] = '\0';
    }
    
    config->success = true;
    return ESP_OK;
}

// ============================================================================
// Robot Position (Class 0x75)
// ============================================================================

esp_err_t enip_scanner_motoman_read_position(const ip4_addr_t *ip_address, uint16_t control_group,
                                             enip_scanner_motoman_position_t *position, uint32_t timeout_ms) {
    if (ip_address == NULL || position == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(position, 0, sizeof(enip_scanner_motoman_position_t));
    position->ip_address = *ip_address;
    
    // Instance ranges: 1-8 (Robot Pulse), 11-18 (Base Pulse), 21-44 (Station Pulse), 101-108 (Robot Base)
    uint8_t response[52];  // Data type (4) + Configuration (4) + Tool number (4) + Reservation (4) + Extended config (4) + 8 axis (32) = 52 bytes
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_POSITION, control_group, 0,
                                             CIP_SERVICE_GET_ATTRIBUTE_ALL, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        snprintf(position->error_message, sizeof(position->error_message), "%s", error_msg);
        return ret;
    }
    
    if (response_length < 44) {
        snprintf(position->error_message, sizeof(position->error_message), "Response too short: %zu bytes", response_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse data type (4 bytes): 0=Pulse, 16=Base
    position->data_type = (uint32_t)(response[0] | (response[1] << 8) | (response[2] << 16) | (response[3] << 24));
    
    // Parse configuration (4 bytes)
    position->configuration = (uint32_t)(response[4] | (response[5] << 8) | (response[6] << 16) | (response[7] << 24));
    
    // Parse tool number (4 bytes)
    position->tool_number = (uint32_t)(response[8] | (response[9] << 8) | (response[10] << 16) | (response[11] << 24));
    
    // Parse reservation (4 bytes)
    position->reservation = (uint32_t)(response[12] | (response[13] << 8) | (response[14] << 16) | (response[15] << 24));
    
    // Parse extended configuration (4 bytes)
    position->extended_configuration = (uint32_t)(response[16] | (response[17] << 8) | (response[18] << 16) | (response[19] << 24));
    
    // Parse 8 axis data (4 bytes each)
    for (int i = 0; i < 8; i++) {
        position->axis_data[i] = (int32_t)(response[20 + (i * 4)] | 
                                           (response[21 + (i * 4)] << 8) | 
                                           (response[22 + (i * 4)] << 16) | 
                                           (response[23 + (i * 4)] << 24));
    }
    
    position->success = true;
    return ESP_OK;
}

// ============================================================================
// Position Deviation (Class 0x76)
// ============================================================================

esp_err_t enip_scanner_motoman_read_position_deviation(const ip4_addr_t *ip_address, uint16_t control_group,
                                                       enip_scanner_motoman_position_deviation_t *deviation,
                                                       uint32_t timeout_ms) {
    if (ip_address == NULL || deviation == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(deviation, 0, sizeof(enip_scanner_motoman_position_deviation_t));
    deviation->ip_address = *ip_address;
    
    // Instance ranges: 1-8 (Robot), 11-18 (Base), 21-44 (Station)
    uint8_t response[32];  // 8 axis data (4 bytes each) = 32 bytes
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_POSITION_DEVIATION, control_group, 0,
                                             CIP_SERVICE_GET_ATTRIBUTE_ALL, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        snprintf(deviation->error_message, sizeof(deviation->error_message), "%s", error_msg);
        return ret;
    }
    
    if (response_length < 4) {
        snprintf(deviation->error_message, sizeof(deviation->error_message), "Response too short: %zu bytes", response_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Some controllers return only populated axes (e.g., 6-axis = 24 bytes).
    size_t axis_count = response_length / 4;
    if (axis_count > 8) {
        axis_count = 8;
    }
    
    for (size_t i = 0; i < axis_count; i++) {
        deviation->axis_deviation[i] = (int32_t)(response[i * 4] | 
                                                 (response[i * 4 + 1] << 8) | 
                                                 (response[i * 4 + 2] << 16) | 
                                                 (response[i * 4 + 3] << 24));
    }
    
    // Zero-fill any missing axes.
    for (size_t i = axis_count; i < 8; i++) {
        deviation->axis_deviation[i] = 0;
    }
    
    deviation->success = true;
    return ESP_OK;
}

// ============================================================================
// Torque (Class 0x77)
// ============================================================================

esp_err_t enip_scanner_motoman_read_torque(const ip4_addr_t *ip_address, uint16_t control_group,
                                           enip_scanner_motoman_torque_t *torque, uint32_t timeout_ms) {
    if (ip_address == NULL || torque == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(torque, 0, sizeof(enip_scanner_motoman_torque_t));
    torque->ip_address = *ip_address;
    
    // Instance ranges: 1-8 (Robot), 11-18 (Base), 21-44 (Station)
    uint8_t response[32];  // 8 axis data (4 bytes each) = 32 bytes
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_TORQUE, control_group, 0,
                                             CIP_SERVICE_GET_ATTRIBUTE_ALL, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        snprintf(torque->error_message, sizeof(torque->error_message), "%s", error_msg);
        return ret;
    }
    
    if (response_length < 4) {
        snprintf(torque->error_message, sizeof(torque->error_message), "Response too short: %zu bytes", response_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Some controllers return only populated axes (e.g., 6-axis = 24 bytes).
    size_t axis_count = response_length / 4;
    if (axis_count > 8) {
        axis_count = 8;
    }
    
    for (size_t i = 0; i < axis_count; i++) {
        torque->axis_torque[i] = (int32_t)(response[i * 4] | 
                                           (response[i * 4 + 1] << 8) | 
                                           (response[i * 4 + 2] << 16) | 
                                           (response[i * 4 + 3] << 24));
    }
    
    // Zero-fill any missing axes.
    for (size_t i = axis_count; i < 8; i++) {
        torque->axis_torque[i] = 0;
    }
    
    torque->success = true;
    return ESP_OK;
}

// ============================================================================
// String Variables (Class 0x8C)
// ============================================================================

esp_err_t enip_scanner_motoman_read_variable_s(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               char *value, size_t value_size, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL || value == NULL || value_size == 0) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = motoman_variable_instance(variable_number);
    
    uint8_t response[32];  // String variable is 32 bytes
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_S, instance, 1,
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
    
    // Copy string (max 32 bytes, but respect user's buffer size)
    size_t copy_len = response_length < value_size ? response_length : (value_size - 1);
    copy_len = copy_len < 32 ? copy_len : 31;
    memcpy(value, response, copy_len);
    value[copy_len] = '\0';
    
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_write_variable_s(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 const char *value, uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL || value == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = motoman_variable_instance(variable_number);
    
    // String variable is 32 bytes max
    uint8_t data[32];
    memset(data, 0, sizeof(data));
    size_t str_len = strlen(value);
    if (str_len > 32) str_len = 32;
    memcpy(data, value, str_len);
    
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_S, instance, 1,
                                             CIP_SERVICE_SET_ATTRIBUTE_SINGLE, data, 32,
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

// ============================================================================
// Position Variables (Class 0x7F)
// ============================================================================

esp_err_t enip_scanner_motoman_read_variable_p(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               enip_scanner_motoman_position_t *position, uint32_t timeout_ms) {
    if (ip_address == NULL || position == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(position, 0, sizeof(enip_scanner_motoman_position_t));
    position->ip_address = *ip_address;
    
    // Instance = variable_number (+1 when RS022=0)
    uint16_t instance = motoman_variable_instance(variable_number);
    
    uint8_t response[52];  // Data type (4) + Configuration (4) + Tool number (4) + User coord (4) + Extended config (4) + 8 axis (32) = 52 bytes
    size_t response_length = 0;
    char error_msg[128];
    
    // Use Get_Attribute_All (Service 0x01) - this should read all attributes at once
    // Note: PLC config shows Service 14 (0x0E = Get_Attribute_Single), but that requires
    // individual attribute reads. Get_Attribute_All is more efficient for reading all data.
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_P, instance, 0,
                                             CIP_SERVICE_GET_ATTRIBUTE_ALL, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        // Copy error message, truncating if necessary
        strncpy(position->error_message, error_msg, sizeof(position->error_message) - 1);
        position->error_message[sizeof(position->error_message) - 1] = '\0';
        return ret;
    }
    
    if (response_length < 52) {
        snprintf(position->error_message, sizeof(position->error_message), "Response too short: %zu bytes", response_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse same structure as Class 0x75 but with user coordinate number instead of reservation
    position->data_type = (uint32_t)(response[0] | (response[1] << 8) | (response[2] << 16) | (response[3] << 24));
    position->configuration = (uint32_t)(response[4] | (response[5] << 8) | (response[6] << 16) | (response[7] << 24));
    position->tool_number = (uint32_t)(response[8] | (response[9] << 8) | (response[10] << 16) | (response[11] << 24));
    position->reservation = (uint32_t)(response[12] | (response[13] << 8) | (response[14] << 16) | (response[15] << 24));  // User coordinate number
    position->extended_configuration = (uint32_t)(response[16] | (response[17] << 8) | (response[18] << 16) | (response[19] << 24));
    
    for (int i = 0; i < 8; i++) {
        position->axis_data[i] = (int32_t)(response[20 + (i * 4)] | 
                                           (response[21 + (i * 4)] << 8) | 
                                           (response[22 + (i * 4)] << 16) | 
                                           (response[23 + (i * 4)] << 24));
    }
    
    position->success = true;
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_write_variable_p(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 const enip_scanner_motoman_position_t *position,
                                                 uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL || position == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = motoman_variable_instance(variable_number);
    
    // Build data: Data type (4) + Configuration (4) + Tool number (4) + User coord (4) + Extended config (4) + 8 axis (32) = 52 bytes
    uint8_t data[52];
    data[0] = position->data_type & 0xFF;
    data[1] = (position->data_type >> 8) & 0xFF;
    data[2] = (position->data_type >> 16) & 0xFF;
    data[3] = (position->data_type >> 24) & 0xFF;
    
    data[4] = position->configuration & 0xFF;
    data[5] = (position->configuration >> 8) & 0xFF;
    data[6] = (position->configuration >> 16) & 0xFF;
    data[7] = (position->configuration >> 24) & 0xFF;
    
    data[8] = position->tool_number & 0xFF;
    data[9] = (position->tool_number >> 8) & 0xFF;
    data[10] = (position->tool_number >> 16) & 0xFF;
    data[11] = (position->tool_number >> 24) & 0xFF;
    
    data[12] = position->reservation & 0xFF;  // User coordinate number
    data[13] = (position->reservation >> 8) & 0xFF;
    data[14] = (position->reservation >> 16) & 0xFF;
    data[15] = (position->reservation >> 24) & 0xFF;
    
    data[16] = position->extended_configuration & 0xFF;
    data[17] = (position->extended_configuration >> 8) & 0xFF;
    data[18] = (position->extended_configuration >> 16) & 0xFF;
    data[19] = (position->extended_configuration >> 24) & 0xFF;
    
    for (int i = 0; i < 8; i++) {
        data[20 + (i * 4)] = position->axis_data[i] & 0xFF;
        data[21 + (i * 4)] = (position->axis_data[i] >> 8) & 0xFF;
        data[22 + (i * 4)] = (position->axis_data[i] >> 16) & 0xFF;
        data[23 + (i * 4)] = (position->axis_data[i] >> 24) & 0xFF;
    }
    
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_P, instance, 0,
                                             CIP_SERVICE_SET_ATTRIBUTE_ALL, data, sizeof(data),
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

// ============================================================================
// Base Position Variables (Class 0x80)
// ============================================================================

esp_err_t enip_scanner_motoman_read_variable_bp(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 enip_scanner_motoman_base_position_t *position, uint32_t timeout_ms) {
    if (ip_address == NULL || position == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(position, 0, sizeof(enip_scanner_motoman_base_position_t));
    position->ip_address = *ip_address;
    
    uint16_t instance = motoman_variable_instance(variable_number);
    
    uint8_t response[36];  // Data type (4) + 8 axis (32) = 36 bytes
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_BP, instance, 0,
                                             CIP_SERVICE_GET_ATTRIBUTE_ALL, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        snprintf(position->error_message, sizeof(position->error_message), "%s", error_msg);
        return ret;
    }
    
    if (response_length < 36) {
        snprintf(position->error_message, sizeof(position->error_message), "Response too short: %zu bytes", response_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse data type (4 bytes): 0=Pulse, 16=Base
    position->data_type = (uint32_t)(response[0] | (response[1] << 8) | (response[2] << 16) | (response[3] << 24));
    
    // Parse 8 axis data (4 bytes each)
    for (int i = 0; i < 8; i++) {
        position->axis_data[i] = (int32_t)(response[4 + (i * 4)] | 
                                           (response[5 + (i * 4)] << 8) | 
                                           (response[6 + (i * 4)] << 16) | 
                                           (response[7 + (i * 4)] << 24));
    }
    
    position->success = true;
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_write_variable_bp(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                  const enip_scanner_motoman_base_position_t *position,
                                                  uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL || position == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = motoman_variable_instance(variable_number);
    
    // Build data: Data type (4) + 8 axis (32) = 36 bytes
    uint8_t data[36];
    data[0] = position->data_type & 0xFF;
    data[1] = (position->data_type >> 8) & 0xFF;
    data[2] = (position->data_type >> 16) & 0xFF;
    data[3] = (position->data_type >> 24) & 0xFF;
    
    for (int i = 0; i < 8; i++) {
        data[4 + (i * 4)] = position->axis_data[i] & 0xFF;
        data[5 + (i * 4)] = (position->axis_data[i] >> 8) & 0xFF;
        data[6 + (i * 4)] = (position->axis_data[i] >> 16) & 0xFF;
        data[7 + (i * 4)] = (position->axis_data[i] >> 24) & 0xFF;
    }
    
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_BP, instance, 0,
                                             CIP_SERVICE_SET_ATTRIBUTE_ALL, data, sizeof(data),
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

// ============================================================================
// External Axis Position Variables (Class 0x81)
// ============================================================================

esp_err_t enip_scanner_motoman_read_variable_ex(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 enip_scanner_motoman_external_position_t *position, uint32_t timeout_ms) {
    if (ip_address == NULL || position == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(position, 0, sizeof(enip_scanner_motoman_external_position_t));
    position->ip_address = *ip_address;
    
    uint16_t instance = motoman_variable_instance(variable_number);
    
    uint8_t response[36];  // Data type (4) + 8 axis (32) = 36 bytes
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_EX, instance, 0,
                                             CIP_SERVICE_GET_ATTRIBUTE_ALL, NULL, 0,
                                             response, sizeof(response), &response_length,
                                             timeout_ms, error_msg);
    
    if (ret != ESP_OK) {
        snprintf(position->error_message, sizeof(position->error_message), "%s", error_msg);
        return ret;
    }
    
    if (response_length < 36) {
        snprintf(position->error_message, sizeof(position->error_message), "Response too short: %zu bytes", response_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse data type (4 bytes): 0=Pulse
    position->data_type = (uint32_t)(response[0] | (response[1] << 8) | (response[2] << 16) | (response[3] << 24));
    
    // Parse 8 axis data (4 bytes each)
    for (int i = 0; i < 8; i++) {
        position->axis_data[i] = (int32_t)(response[4 + (i * 4)] | 
                                           (response[5 + (i * 4)] << 8) | 
                                           (response[6 + (i * 4)] << 16) | 
                                           (response[7 + (i * 4)] << 24));
    }
    
    position->success = true;
    return ESP_OK;
}

esp_err_t enip_scanner_motoman_write_variable_ex(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                  const enip_scanner_motoman_external_position_t *position,
                                                  uint32_t timeout_ms, char *error_message) {
    if (ip_address == NULL || position == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid arguments");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t instance = motoman_variable_instance(variable_number);
    
    // Build data: Data type (4) + 8 axis (32) = 36 bytes
    uint8_t data[36];
    data[0] = position->data_type & 0xFF;
    data[1] = (position->data_type >> 8) & 0xFF;
    data[2] = (position->data_type >> 16) & 0xFF;
    data[3] = (position->data_type >> 24) & 0xFF;
    
    for (int i = 0; i < 8; i++) {
        data[4 + (i * 4)] = position->axis_data[i] & 0xFF;
        data[5 + (i * 4)] = (position->axis_data[i] >> 8) & 0xFF;
        data[6 + (i * 4)] = (position->axis_data[i] >> 16) & 0xFF;
        data[7 + (i * 4)] = (position->axis_data[i] >> 24) & 0xFF;
    }
    
    uint8_t response[4];
    size_t response_length = 0;
    char error_msg[128];
    
    esp_err_t ret = send_motoman_cip_message(ip_address, MOTOMAN_CLASS_VARIABLE_EX, instance, 0,
                                             CIP_SERVICE_SET_ATTRIBUTE_ALL, data, sizeof(data),
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



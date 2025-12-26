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

#include "enip_scanner_implicit_internal.h"
#include "enip_scanner.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_random.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/FreeRTOSConfig.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "esp_netif_ip_addr.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT

static const char *TAG = "enip_scanner_implicit";

// Connection ID generation
static uint32_t connection_id_base = 0;
static uint16_t connection_counter = 0;
static SemaphoreHandle_t s_connection_id_mutex = NULL;

// Connection array protection
static SemaphoreHandle_t s_connections_mutex = NULL;

// Forward declarations
static void heartbeat_task(void *pvParameters);
static void receive_task(void *pvParameters);
static void watchdog_task(void *pvParameters);
static esp_err_t forward_open_with_size_calculation(enip_implicit_connection_t *conn, uint32_t timeout_ms, bool include_overhead, bool retry_attempted, bool use_fixed_length);

static uint32_t generate_connection_id(void)
{
    // Create mutex on first call if needed
    if (s_connection_id_mutex == NULL) {
        s_connection_id_mutex = xSemaphoreCreateMutex();
        if (s_connection_id_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create connection ID mutex");
            return 0;
        }
    }
    
    if (xSemaphoreTake(s_connection_id_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    
    if (connection_id_base == 0) {
        uint16_t random_upper = (uint16_t)esp_random();
        connection_id_base = ((uint32_t)random_upper << 16);
        connection_counter = 2;
        if (connection_id_base == 0) {
            connection_id_base = 0x087e0000;
        }
    }
    
    connection_counter += 2;
    uint32_t o_to_t_connection_id = connection_id_base | (connection_counter & 0xFFFF);
    if (o_to_t_connection_id == 0) {
        o_to_t_connection_id = 0x087e0002;
    }
    
    xSemaphoreGive(s_connection_id_mutex);
    return o_to_t_connection_id;
}

#define read_assembly_data_size enip_scanner_read_assembly_data_size

static esp_err_t forward_open(enip_implicit_connection_t *conn, uint32_t timeout_ms)
{
    return forward_open_with_size_calculation(conn, timeout_ms, true, false, false);
}

static esp_err_t forward_open_with_size_calculation(enip_implicit_connection_t *conn, uint32_t timeout_ms, bool include_overhead, bool retry_attempted, bool use_fixed_length)
{
    if (conn == NULL || conn->tcp_socket < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (conn->exclusive_owner) {
        conn->o_to_t_connection_id = generate_connection_id();
        conn->t_to_o_connection_id = conn->o_to_t_connection_id + 1;
    } else {
        conn->o_to_t_connection_id = 0xffff0016;
        conn->t_to_o_connection_id = 0xffff0017;
    }
    
    conn->connection_serial_number = (uint16_t)esp_random();
    conn->originator_serial_number = esp_random();
    conn->priority_time_tick = 0x2A;
    conn->timeout_ticks = 0x04;
    
    uint16_t o_to_t_size, t_to_o_size;
    if (include_overhead) {
        o_to_t_size = conn->assembly_data_size_consumed + 2 + 4;
        t_to_o_size = conn->assembly_data_size_produced + 2;
    } else {
        o_to_t_size = conn->assembly_data_size_consumed;
        t_to_o_size = conn->assembly_data_size_produced;
    }
    
    uint16_t o_to_t_params = 0x0000;
    uint16_t t_to_o_params = 0x0000;
    
    if (!use_fixed_length) {
        o_to_t_params = 0x0200;
        t_to_o_params = 0x0200;
    }
    
    o_to_t_params |= (2 << 9);
    t_to_o_params |= (2 << 9);
    
    o_to_t_params |= 0x4000;
    if (conn->exclusive_owner) {
        t_to_o_params |= 0x4000;
    } else {
        t_to_o_params |= 0x2000;
    }
    
    o_to_t_params |= 0x8000;
    t_to_o_params |= 0x8000;
    o_to_t_params += o_to_t_size;
    t_to_o_params += t_to_o_size;
    
    uint32_t rpi_us = conn->rpi_ms * 1000;
    uint8_t packet[128];
    size_t offset = 0;
    uint16_t cmd = ENIP_SEND_RR_DATA;
    memcpy(packet + offset, &cmd, 2);
    offset += 2;
    
    size_t length_offset = offset;
    offset += 2;
    
    memcpy(packet + offset, &conn->session_handle, 4);
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
    
    uint32_t interface_handle = 0;
    memcpy(packet + offset, &interface_handle, 4);
    offset += 4;
    
    uint16_t cip_timeout = 0x0A;
    memcpy(packet + offset, &cip_timeout, 2);
    offset += 2;
    
    uint16_t item_count_req = 2;
    memcpy(packet + offset, &item_count_req, 2);
    offset += 2;
    
    uint16_t null_item_type = CPF_ITEM_NULL_ADDRESS;
    uint16_t null_item_length = 0;
    memcpy(packet + offset, &null_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &null_item_length, 2);
    offset += 2;
    
    uint16_t data_item_type_req = CPF_ITEM_UNCONNECTED_DATA;
    memcpy(packet + offset, &data_item_type_req, 2);
    offset += 2;
    size_t data_item_length_offset = offset;
    offset += 2;
    
    size_t cip_start = offset;
    
    uint8_t service_code = CIP_SERVICE_FORWARD_OPEN;
    packet[offset++] = service_code;
    
    uint8_t path_size = 2;
    packet[offset++] = path_size;
    
    packet[offset++] = CIP_PATH_CLASS;
    packet[offset++] = CIP_CLASS_CONNECTION_MANAGER;
    packet[offset++] = CIP_PATH_INSTANCE;
    packet[offset++] = 0x01;
    packet[offset++] = 0x2A;
    packet[offset++] = 0x04;
    
    memcpy(packet + offset, &conn->o_to_t_connection_id, 4);
    offset += 4;
    memcpy(packet + offset, &conn->t_to_o_connection_id, 4);
    offset += 4;
    memcpy(packet + offset, &conn->connection_serial_number, 2);
    offset += 2;
    
    uint16_t vendor_id = 0xFADA;
    memcpy(packet + offset, &vendor_id, 2);
    offset += 2;
    memcpy(packet + offset, &conn->originator_serial_number, 4);
    offset += 4;
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    packet[offset++] = 0x00;
    
    memcpy(packet + offset, &rpi_us, 4);
    offset += 4;
    memcpy(packet + offset, &o_to_t_params, 2);
    offset += 2;
    memcpy(packet + offset, &rpi_us, 4);
    offset += 4;
    memcpy(packet + offset, &t_to_o_params, 2);
    offset += 2;
    packet[offset++] = 0x01;
    packet[offset++] = 3;
    
    packet[offset++] = CIP_PATH_CLASS;
    packet[offset++] = CIP_CLASS_ASSEMBLY;
    packet[offset++] = CIP_PATH_CONNECTION_POINT;
    packet[offset++] = (uint8_t)conn->assembly_instance_consumed;
    packet[offset++] = CIP_PATH_CONNECTION_POINT;
    packet[offset++] = (uint8_t)conn->assembly_instance_produced;
    
    size_t cip_length = offset - cip_start;
    uint16_t data_item_length = (uint16_t)cip_length;
    memcpy(packet + data_item_length_offset, &data_item_length, 2);
    uint16_t enip_data_length = (uint16_t)(offset - 24);
    memcpy(packet + length_offset, &enip_data_length, 2);
    
    esp_err_t ret = send_data(conn->tcp_socket, packet, offset);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send Forward Open request");
        return ret;
    }
    
    uint8_t response[512];
    size_t bytes_received = 0;
    ret = recv_data(conn->tcp_socket, response, 28, timeout_ms, &bytes_received);
    if (ret != ESP_OK || bytes_received < 24) {
        ESP_LOGE(TAG, "Failed to receive Forward Open ENIP header: got %zu bytes", bytes_received);
        return ESP_FAIL;
    }
    
    size_t response_offset = 0;
    enip_header_t *response_header = (enip_header_t *)response;
    
    if (response_header->command != ENIP_SEND_RR_DATA && bytes_received >= 28) {
        enip_header_t *header_offset = (enip_header_t *)(response + 4);
        if (header_offset->command == ENIP_SEND_RR_DATA) {
            response_header = header_offset;
            response_offset = 4;
            bytes_received -= 4;
        } else {
            ESP_LOGE(TAG, "Unexpected response command: 0x%04X", response_header->command);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    
    if (response_header->command != ENIP_SEND_RR_DATA) {
        ESP_LOGE(TAG, "Unexpected response command: 0x%04X", response_header->command);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    uint16_t response_length = response_header->length;
    
    if (response_header->status != 0) {
        ESP_LOGE(TAG, "Forward Open ENIP status error: 0x%08lX", (unsigned long)response_header->status);
        return ESP_FAIL;
    }
    
    size_t total_expected = response_offset + 24 + response_length;
    
    if (bytes_received + response_offset < total_expected) {
        size_t remaining = total_expected - (bytes_received + response_offset);
        if (remaining > sizeof(response) - (bytes_received + response_offset)) {
            remaining = sizeof(response) - (bytes_received + response_offset);
        }
        if (remaining > 0) {
            size_t additional_received = 0;
            ret = recv_data(conn->tcp_socket, response + bytes_received + response_offset, remaining, timeout_ms, &additional_received);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to receive Forward Open response data: got %zu bytes", additional_received);
                return ESP_FAIL;
            }
            bytes_received += additional_received;
        }
    }
    
    if (bytes_received + response_offset < 32) {
        ESP_LOGE(TAG, "Forward Open response too short: %zu bytes", bytes_received + response_offset);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    uint32_t interface_handle_resp;
    memcpy(&interface_handle_resp, response + response_offset + 24, 4);
    uint16_t timeout_resp;
    memcpy(&timeout_resp, response + response_offset + 28, 2);
    uint16_t item_count_resp;
    memcpy(&item_count_resp, response + response_offset + 30, 2);
    
    if (item_count_resp < 1 || item_count_resp > 4) {
        ESP_LOGE(TAG, "Unexpected item count: %d (expected 1-4)", item_count_resp);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    size_t cip_offset = response_offset + 24 + 4 + 2 + 2;
    size_t data_item_offset = 0;
    uint16_t data_item_type_resp = 0;
    uint16_t data_item_length_resp = 0;
    bool found_data_item = false;
    
    size_t current_offset = cip_offset;
    for (uint16_t i = 0; i < item_count_resp; i++) {
        if ((bytes_received + response_offset) < current_offset + 4) {
            ESP_LOGE(TAG, "Response too short for item %d header", i);
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        uint16_t item_type;
        uint16_t item_length;
        memcpy(&item_type, response + current_offset, 2);
        memcpy(&item_length, response + current_offset + 2, 2);
        
        if (item_type == CPF_ITEM_UNCONNECTED_DATA || item_type == CPF_ITEM_CONNECTED_DATA) {
            data_item_offset = current_offset;
            data_item_type_resp = item_type;
            data_item_length_resp = item_length;
            found_data_item = true;
            break;
        }
        
        if ((bytes_received + response_offset) < current_offset + 4 + item_length) {
            ESP_LOGE(TAG, "Response too short for item %d data", i);
            return ESP_ERR_INVALID_RESPONSE;
        }
        
        current_offset += 4 + item_length;
    }
    
    if (!found_data_item) {
        ESP_LOGE(TAG, "No data item found in %d items", item_count_resp);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    cip_offset = data_item_offset;
    size_t cip_response_offset = cip_offset + 4;
    
    if (data_item_type_resp == CPF_ITEM_SEQUENCED_ADDRESS) {
        if (data_item_length_resp >= 8) {
            cip_response_offset += 8;
        } else if (data_item_length_resp > 0) {
            cip_response_offset += data_item_length_resp;
        }
    }
    
    if (bytes_received + response_offset < cip_response_offset + 4) {
        ESP_LOGE(TAG, "Forward Open response too short");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    uint8_t service_response = response[cip_response_offset];
    
    if ((service_response & 0x80) == 0) {
        ESP_LOGE(TAG, "Forward Open response missing response bit");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    uint8_t general_status = response[cip_response_offset + 2];
    
    if (general_status != 0x00) {
        size_t remaining_bytes = (bytes_received + response_offset) - (cip_response_offset + 4);
        
        uint16_t extended_status = 0;
        if (remaining_bytes >= 2) {
            memcpy(&extended_status, response + cip_response_offset + 4, 2);
            ESP_LOGE(TAG, "Forward Open failed: Status=0x%02X, Extended=0x%04X", general_status, extended_status);
            
            if (extended_status == 0x0100) {
                ESP_LOGE(TAG, "Connection Failure (0x0100)");
            } else if (extended_status == 0x0106) {
                ESP_LOGE(TAG, "Ownership Conflict (0x0106)");
            } else if (extended_status == 0x0107) {
                ESP_LOGE(TAG, "Connection In Use (0x0107)");
            } else if (extended_status == 0x0315) {
                ESP_LOGE(TAG, "Invalid Connection Parameters (0x0315)");
                if (include_overhead && !retry_attempted) {
                    esp_err_t retry_result = forward_open_with_size_calculation(conn, timeout_ms, false, true, false);
                    if (retry_result == ESP_OK) {
                        return ESP_OK;
                    }
                    retry_result = forward_open_with_size_calculation(conn, timeout_ms, false, true, true);
                    if (retry_result == ESP_OK) {
                        return ESP_OK;
                    }
                }
            }
        }
        return ESP_FAIL;
    }
    
    if ((bytes_received + response_offset) < cip_response_offset + 12) {
        ESP_LOGE(TAG, "Forward Open response too short for connection IDs");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    uint32_t response_o_to_t_id, response_t_to_o_id;
    memcpy(&response_o_to_t_id, response + cip_response_offset + 4, 4);
    memcpy(&response_t_to_o_id, response + cip_response_offset + 8, 4);
    
    if (conn->exclusive_owner) {
        if (response_o_to_t_id != conn->o_to_t_connection_id || response_t_to_o_id != conn->t_to_o_connection_id) {
            conn->o_to_t_connection_id = response_o_to_t_id;
            conn->t_to_o_connection_id = response_t_to_o_id;
        }
    } else {
        conn->o_to_t_connection_id = response_o_to_t_id;
        conn->t_to_o_connection_id = response_t_to_o_id;
    }
    
    return ESP_OK;
}

static esp_err_t forward_close(enip_implicit_connection_t *conn, uint32_t timeout_ms)
{
    if (conn == NULL || conn->tcp_socket < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t packet[128];
    size_t offset = 0;
    uint16_t cmd = ENIP_SEND_RR_DATA;
    memcpy(packet + offset, &cmd, 2);
    offset += 2;
    
    size_t length_offset = offset;
    offset += 2;
    
    memcpy(packet + offset, &conn->session_handle, 4);
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
    
    uint32_t interface_handle = 0;
    memcpy(packet + offset, &interface_handle, 4);
    offset += 4;
    
    uint16_t cip_timeout = 0x0000;
    memcpy(packet + offset, &cip_timeout, 2);
    offset += 2;
    
    uint16_t item_count = 2;
    memcpy(packet + offset, &item_count, 2);
    offset += 2;
    
    uint16_t null_item_type = CPF_ITEM_NULL_ADDRESS;
    uint16_t null_item_length = 0;
    memcpy(packet + offset, &null_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &null_item_length, 2);
    offset += 2;
    
    uint16_t data_item_type_close = CPF_ITEM_UNCONNECTED_DATA;
    memcpy(packet + offset, &data_item_type_close, 2);
    offset += 2;
    size_t data_item_length_offset = offset;
    offset += 2;
    
    size_t cip_start = offset;
    
    uint8_t service_code = CIP_SERVICE_FORWARD_CLOSE;
    packet[offset++] = service_code;
    
    uint8_t path_size = 0x02;
    packet[offset++] = path_size;
    
    packet[offset++] = CIP_PATH_CLASS;
    packet[offset++] = CIP_CLASS_CONNECTION_MANAGER;
    packet[offset++] = CIP_PATH_INSTANCE;
    packet[offset++] = 0x01;
    
    packet[offset++] = conn->priority_time_tick;
    packet[offset++] = conn->timeout_ticks;
    
    memcpy(packet + offset, &conn->connection_serial_number, 2);
    offset += 2;
    
    uint16_t vendor_id = 0xFADA;
    memcpy(packet + offset, &vendor_id, 2);
    offset += 2;
    memcpy(packet + offset, &conn->originator_serial_number, 4);
    offset += 4;
    
    uint8_t conn_path_size = 3;
    packet[offset++] = conn_path_size;
    packet[offset++] = 0x00;
    
    packet[offset++] = CIP_PATH_CLASS;
    packet[offset++] = CIP_CLASS_ASSEMBLY;
    packet[offset++] = CIP_PATH_CONNECTION_POINT;
    packet[offset++] = (uint8_t)conn->assembly_instance_consumed;
    packet[offset++] = CIP_PATH_CONNECTION_POINT;
    packet[offset++] = (uint8_t)conn->assembly_instance_produced;
    
    size_t cip_length = offset - cip_start;
    uint16_t data_item_length = (uint16_t)cip_length;
    memcpy(packet + data_item_length_offset, &data_item_length, 2);
    
    uint16_t enip_data_length = (uint16_t)(offset - 24);
    memcpy(packet + length_offset, &enip_data_length, 2);
    
    esp_err_t ret = send_data(conn->tcp_socket, packet, offset);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send Forward Close request");
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    uint8_t response[128];
    size_t bytes_received = 0;
    
    fd_set read_fds, error_fds;
    struct timeval tv;
    FD_ZERO(&read_fds);
    FD_ZERO(&error_fds);
    FD_SET(conn->tcp_socket, &read_fds);
    FD_SET(conn->tcp_socket, &error_fds);
    // Add extra time for device processing (minimum 2 seconds)
    uint32_t select_timeout_ms = timeout_ms > 2000 ? timeout_ms : 2000;
    tv.tv_sec = select_timeout_ms / 1000;
    tv.tv_usec = (select_timeout_ms % 1000) * 1000;
    
    int select_result = select(conn->tcp_socket + 1, &read_fds, NULL, &error_fds, &tv);
    if (select_result < 0) {
        ESP_LOGW(TAG, "Forward Close: select() error: %d (errno=%d)", select_result, errno);
        return ESP_ERR_INVALID_STATE;
    } else if (FD_ISSET(conn->tcp_socket, &error_fds)) {
        ESP_LOGW(TAG, "Forward Close: Socket error detected (connection may be closed by peer)");
        // Check socket error
        int socket_error = 0;
        socklen_t error_len = sizeof(socket_error);
        if (getsockopt(conn->tcp_socket, SOL_SOCKET, SO_ERROR, &socket_error, &error_len) == 0) {
            ESP_LOGW(TAG, "Forward Close: Socket error code: %d", socket_error);
        }
        // Some devices close the connection as acknowledgment - treat as success
        ESP_LOGD(TAG, "Forward Close: Device closed connection - treating as acknowledgment");
        return ESP_OK;
    } else if (select_result == 0 || !FD_ISSET(conn->tcp_socket, &read_fds)) {
        ESP_LOGW(TAG, "Forward Close: Socket not readable (select returned %d, errno=%d)", select_result, errno);
        
        // Try to read anyway - sometimes data is available even if select says it's not
        ret = recv_data(conn->tcp_socket, response, 24, 100, &bytes_received);
        if (ret != ESP_OK || bytes_received == 0) {
            ESP_LOGW(TAG, "Forward Close: No data available");
            return ESP_ERR_TIMEOUT;
        }
    } else {
        ret = recv_data(conn->tcp_socket, response, 24, timeout_ms, &bytes_received);
    }
    
    if (bytes_received == 0 && ret != ESP_OK) {
        ret = recv_data(conn->tcp_socket, response, 24, timeout_ms, &bytes_received);
    }
    
    // Check if socket was closed by peer (some devices close connection as acknowledgment)
    if (bytes_received == 0) {
        // Check socket state
        int socket_error = 0;
        socklen_t error_len = sizeof(socket_error);
        if (getsockopt(conn->tcp_socket, SOL_SOCKET, SO_ERROR, &socket_error, &error_len) == 0) {
            if (socket_error == 0) {
                // Socket is still valid but no data received - device may be ignoring the request
                ESP_LOGW(TAG, "Forward Close: No response received, but socket is still valid");
                ESP_LOGW(TAG, "Forward Close: Device may be ignoring the request or requires different parameters");
            } else if (socket_error == ECONNRESET || socket_error == EPIPE) {
                ESP_LOGD(TAG, "Forward Close: Device closed connection - treating as acknowledgment");
                return ESP_OK;
            } else {
                ESP_LOGW(TAG, "Forward Close: Socket error: %d", socket_error);
            }
        } else {
            ESP_LOGW(TAG, "Forward Close: Failed to check socket error (getsockopt failed)");
        }
    }
    
    if (ret == ESP_OK && bytes_received >= 24) {
        // Parse ENIP header
        enip_header_t *response_header = (enip_header_t *)response;
        if (response_header->command == ENIP_SEND_RR_DATA && response_header->status == 0) {
            // Read remaining response data if needed
            uint16_t response_length = response_header->length;
            if (response_length > 0 && response_length <= sizeof(response) - 24) {
                size_t remaining = response_length;
                size_t additional_received = 0;
                recv_data(conn->tcp_socket, response + 24, remaining, timeout_ms, &additional_received);
                bytes_received += additional_received;
                
                ESP_LOG_BUFFER_HEXDUMP(TAG, response, bytes_received, ESP_LOG_DEBUG);
                
                // Parse CPF items to find the CIP response
                // Response format: ENIP Header (24) + Interface Handle (4) + Timeout (2) + Item Count (2) + CPF Items
                size_t current_offset = 24 + 4 + 2;  // Skip ENIP header, Interface Handle, Timeout
                if (bytes_received >= current_offset + 2) {
                    uint16_t item_count_resp;
                    memcpy(&item_count_resp, response + current_offset, 2);
                    current_offset += 2;
                    
                    bool cip_response_found = false;
                    for (int i = 0; i < item_count_resp && current_offset + 4 <= bytes_received; i++) {
                        uint16_t item_type;
                        uint16_t item_length;
                        memcpy(&item_type, response + current_offset, 2);
                        memcpy(&item_length, response + current_offset + 2, 2);
                        current_offset += 4;
                        
                        if (item_type == CPF_ITEM_UNCONNECTED_DATA) {
                            // This is the CIP response data item
                            // CIP Response structure (per user specification):
                            // Byte 0: Reply Service (0xCE = Forward Close Response)
                            // Byte 1: Reserved (0x00)
                            // Byte 2: General Status (0x00 = Success)
                            // Byte 3: Extended Status Length (0x00 = no extended status)
                            // Bytes 4-5: Connection Serial Number
                            // Bytes 6-7: Vendor ID
                            // Bytes 8-11: Originator Serial Number
                            // Byte 12: Application Data Size (0x00)
                            // Byte 13: Reserved (0x00)
                            if (current_offset + item_length <= bytes_received) {
                                uint8_t cip_service = response[current_offset];
                                uint8_t general_status = response[current_offset + 2];
                                uint8_t additional_status_size = response[current_offset + 3];
                                
                                if (cip_service == (CIP_SERVICE_FORWARD_CLOSE | 0x80)) {
                                    if (general_status == 0x00) {
                                        cip_response_found = true;
                                        return ESP_OK;
                                    } else {
                                        // Error response
                                        // For error case: General Status = 0x01, Extended Status Length = 0x01, Extended Status = 0x0107 (Connection Not Found)
                                        uint16_t extended_status = 0;
                                        if (additional_status_size >= 1 && item_length >= 6) {
                                            memcpy(&extended_status, response + current_offset + 4, 2);
                                        }
                                        ESP_LOGE(TAG, "Forward Close failed: General Status=0x%02X, Extended Status=0x%04X", 
                                                 general_status, extended_status);
                                        
                                        // Log specific error codes
                                        if (extended_status == 0x0107) {
                                            ESP_LOGE(TAG, "ERROR: Connection not found (0x0107) - The connection specified by the identifiers was not found");
                                        } else if (extended_status == 0xFFFF) {
                                            ESP_LOGE(TAG, "ERROR: Wrong closer (0xFFFF) - IP address mismatch or other closing error");
                                        }
                                        cip_response_found = true;
                                        return ESP_FAIL;
                                    }
                                } else {
                                    ESP_LOGW(TAG, "Unexpected Forward Close response service code: 0x%02X (expected 0xCE)", cip_service);
                                    cip_response_found = true;
                                    return ESP_ERR_INVALID_RESPONSE;
                                }
                            } else {
                                ESP_LOGW(TAG, "Forward Close response: Unconnected Data Item incomplete (need %zu bytes, got %zu)", 
                                         current_offset + item_length, bytes_received);
                                cip_response_found = true;
                                return ESP_ERR_INVALID_RESPONSE;
                            }
                        }
                        current_offset += item_length;
                    }
                    
                    if (!cip_response_found) {
                        ESP_LOGW(TAG, "Forward Close response: CIP response item not found in CPF items");
                        return ESP_ERR_INVALID_RESPONSE;
                    }
                    // If cip_response_found is true, we should have already returned above
                    // This return is here to satisfy the compiler
                    return ESP_ERR_INVALID_RESPONSE;
                } else {
                    ESP_LOGW(TAG, "Forward Close response: Incomplete (got %zu bytes, need at least %zu)", bytes_received, current_offset + 2);
                    return ESP_ERR_INVALID_RESPONSE;
                }
            } else {
                ESP_LOGW(TAG, "Forward Close response: ENIP length=%u exceeds buffer size", response_length);
                return ESP_ERR_INVALID_RESPONSE;
            }
        } else {
            ESP_LOGW(TAG, "Forward Close response error: command=0x%04X, status=0x%08lX", 
                     response_header->command, (unsigned long)response_header->status);
            return ESP_ERR_INVALID_RESPONSE;
        }
    } else {
        ESP_LOGW(TAG, "Forward Close response not received (connection may already be closed): got %zu bytes", bytes_received);
        // Return error if no response - caller should wait longer before retrying
        return ESP_ERR_TIMEOUT;
    }
}


static int create_udp_socket(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: %d", errno);
        return -1;
    }
    
    // Set SO_REUSEADDR to allow immediate reuse of the port after closing
    // This prevents EADDRINUSE errors when reconnecting quickly
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ESP_LOGW(TAG, "Failed to set SO_REUSEADDR on UDP socket: %d (continuing anyway)", errno);
    }
    
    // Bind to INADDR_ANY, port 2222
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(ENIP_IMPLICIT_PORT);
    
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind UDP socket: %d", errno);
        close(sock);
        return -1;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        ESP_LOGE(TAG, "Failed to get socket flags: %d", errno);
        close(sock);
        return -1;
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        ESP_LOGE(TAG, "Failed to set socket non-blocking: %d", errno);
        close(sock);
        return -1;
    }
    
    // Set receive timeout (100ms)
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    return sock;
}


static void heartbeat_task(void *pvParameters)
{
    enip_implicit_connection_t *conn = (enip_implicit_connection_t *)pvParameters;
    if (conn == NULL) {
        vTaskDelete(NULL);
        return;
    }
    
    uint32_t eip_sequence = 0;
    uint16_t cip_sequence = 0;
    
    // Wait 50ms after Forward Open before sending first heartbeat
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Calculate packet size: Item Count (2) + Address Item (12) + Data Item Header (4) + 
    //                        CIP Seq (2) + Run/Idle (4) + Assembly Data
    size_t packet_size = 2 + 12 + 4 + 2 + 4 + conn->assembly_data_size_consumed;
    uint8_t *packet = malloc(packet_size);
    if (packet == NULL) {
        ESP_LOGE(TAG, "Failed to allocate packet buffer");
        vTaskDelete(NULL);
        return;
    }
    
    while (conn->state == ENIP_CONN_STATE_OPEN && conn->valid) {
        size_t offset = 0;
        
        // Item Count
        uint16_t item_count = 2;
        memcpy(packet + offset, &item_count, 2);
        offset += 2;
        
        // Sequenced Address Item (0x8002, 8 bytes)
        uint16_t addr_item_type = CPF_ITEM_SEQUENCED_ADDRESS;
        uint16_t addr_item_length = 8;
        memcpy(packet + offset, &addr_item_type, 2);
        offset += 2;
        memcpy(packet + offset, &addr_item_length, 2);
        offset += 2;
        memcpy(packet + offset, &conn->o_to_t_connection_id, 4);
        offset += 4;
        memcpy(packet + offset, &eip_sequence, 4);
        offset += 4;
        eip_sequence++;
        
        // Connected Data Item - size = CIP seq (2) + Run/Idle (4) + Assembly data
        uint16_t data_item_length = 2 + 4 + conn->assembly_data_size_consumed;  // CIP seq + Run/Idle + assembly data
        uint16_t data_item_type = CPF_ITEM_CONNECTED_DATA;
        memcpy(packet + offset, &data_item_type, 2);
        offset += 2;
        memcpy(packet + offset, &data_item_length, 2);
        offset += 2;
        memcpy(packet + offset, &cip_sequence, 2);
        offset += 2;
        cip_sequence++;
        
        // Run/Idle Header (4 bytes) - 0x00000001 = Run state
        uint32_t run_idle = 0x00000001;
        memcpy(packet + offset, &run_idle, 4);
        offset += 4;
        
        // Assembly data (O-to-T, consumed) - use actual size
        uint16_t assembly_data_size = conn->assembly_data_size_consumed;
        typedef struct {
            enip_implicit_data_callback_t callback;
            void *user_data;
            uint8_t *o_to_t_data;  // Dynamic size
            uint16_t o_to_t_data_length;
            SemaphoreHandle_t data_mutex;  // Mutex to protect o_to_t_data access
        } callback_wrapper_t;
        
        // Safely access user_data (may be NULL during shutdown)
        // Check valid flag first (atomic read)
        bool conn_valid = conn->valid;
        callback_wrapper_t *wrapper = NULL;
        if (conn->user_data != NULL && conn_valid) {
            wrapper = (callback_wrapper_t *)conn->user_data;
        }
        
        // Access O-to-T data with mutex protection
        if (wrapper && wrapper->data_mutex != NULL) {
            if (xSemaphoreTake(wrapper->data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (wrapper->o_to_t_data && wrapper->o_to_t_data_length > 0) {
                    // The wrapper buffer is always allocated to exactly assembly_data_size_consumed bytes
                    // and is zero-padded if the user wrote less data
                    // Always copy the full assembly_data_size bytes from the buffer
                    memcpy(packet + offset, wrapper->o_to_t_data, assembly_data_size);
                } else {
                    // Default: zeros
                    memset(packet + offset, 0, assembly_data_size);
                    // Log if we expected data but didn't find it (only occasionally)
                    static uint32_t no_data_count = 0;
                    if ((no_data_count++ % 100) == 0) {
                        if (wrapper->o_to_t_data == NULL) {
                            ESP_LOGW(TAG, "Heartbeat: No O-to-T data buffer allocated, sending zeros");
                        } else if (wrapper->o_to_t_data_length == 0) {
                            ESP_LOGW(TAG, "Heartbeat: O-to-T data length is 0, sending zeros");
                        }
                    }
                }
                xSemaphoreGive(wrapper->data_mutex);
            } else {
                // Mutex timeout - use zeros
                memset(packet + offset, 0, assembly_data_size);
            }
        } else {
            // Default: zeros
            memset(packet + offset, 0, assembly_data_size);
            static uint32_t no_wrapper_count = 0;
            if ((no_wrapper_count++ % 100) == 0) {
                if (wrapper == NULL) {
                    ESP_LOGW(TAG, "Heartbeat: No wrapper found, sending zeros");
                }
            }
        }
        offset += assembly_data_size;
        
        // Send packet (packet_size bytes total)
        struct sockaddr_in target_addr;
        memset(&target_addr, 0, sizeof(target_addr));
        target_addr.sin_family = AF_INET;
        target_addr.sin_addr.s_addr = conn->ip_address.addr;
        target_addr.sin_port = htons(ENIP_IMPLICIT_PORT);
        
        ssize_t sent = sendto(conn->udp_socket, packet, packet_size, 0,
                             (struct sockaddr *)&target_addr, sizeof(target_addr));
        if (sent >= 0) {
            // Update last heartbeat time when we successfully send O->T
            conn->last_heartbeat_time = xTaskGetTickCount();
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Log errors at reduced frequency
            static uint32_t error_count = 0;
            if ((error_count++ % 100) == 0) {
                ESP_LOGW(TAG, "Heartbeat send error: %d", errno);
            }
        }
        
        // Explicit write to assembly instance (DISABLED - can be re-enabled by changing #if 0 to #if 1)
        // Some devices require the assembly instance to be updated explicitly, not just via implicit messaging packets
        // Note: WAGO devices typically reject explicit writes with privilege violation (0x0F) when using implicit messaging
        // Set to #if 1 to re-enable this feature
#if 0
        if (wrapper && wrapper->o_to_t_data && wrapper->o_to_t_data_length > 0) {
            char error_msg[128] = {0};
            esp_err_t write_ret = enip_scanner_write_assembly(&conn->ip_address,
                                                              conn->assembly_instance_consumed,
                                                              wrapper->o_to_t_data,
                                                              conn->assembly_data_size_consumed,
                                                              conn->rpi_ms + 100,  // Timeout slightly longer than RPI
                                                              error_msg);
            if (write_ret != ESP_OK) {
                // Log errors at reduced frequency to avoid spam
                static uint32_t write_error_count = 0;
                if ((write_error_count++ % 50) == 0) {
                    ESP_LOGW(TAG, "Explicit write to assembly %u failed: %s (error: %s)", 
                             conn->assembly_instance_consumed, error_msg, esp_err_to_name(write_ret));
                }
            }
        }
#endif
        
        // Delay by RPI, but cap at 1000ms maximum
        // This ensures we send O->T packets at least every 1000ms even if RPI is larger
        uint32_t delay_ms = conn->rpi_ms;
        if (delay_ms > 1000) {
            delay_ms = 1000;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    free(packet);
    vTaskDelete(NULL);
}


static void receive_task(void *pvParameters)
{
    enip_implicit_connection_t *conn = (enip_implicit_connection_t *)pvParameters;
    if (conn == NULL) {
        vTaskDelete(NULL);
        return;
    }
    
    uint8_t recv_buffer[256];
    
    // Receive task continues running even when state = CLOSING
    // It only exits when valid = false (after Forward Close completes)
    // This allows receiving T-to-O data until Forward Close response is received
    uint32_t no_packet_count = 0;
    while (conn->valid) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        ssize_t received = recvfrom(conn->udp_socket, recv_buffer, sizeof(recv_buffer), 0,
                                   (struct sockaddr *)&from_addr, &from_len);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available, small delay
                no_packet_count++;
                // Log every 10 seconds if no packets received (100 iterations * 100ms = 10s)
                if (no_packet_count == 100) {
                    ESP_LOGW(TAG, "No T->O packets received for 10 seconds - adapter may not be sending data");
                    ESP_LOGW(TAG, "  Expected T->O connection ID: 0x%08lX, RPI: %lu ms", 
                             (unsigned long)conn->t_to_o_connection_id, (unsigned long)conn->rpi_ms);
                    no_packet_count = 0;  // Reset counter
                }
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGW(TAG, "Receive error: %d", errno);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        // Reset counter when we receive a packet
        no_packet_count = 0;
        
        // Check source IP matches target device
        if (from_addr.sin_addr.s_addr != conn->ip_address.addr) {
            static uint32_t wrong_ip_count = 0;
            if ((wrong_ip_count++ % 100) == 0) {
                ESP_LOGW(TAG, "Received UDP packet from wrong IP (expected " IPSTR ", got " IPSTR ") - ignoring",
                         IP2STR(&conn->ip_address), IP2STR((ip4_addr_t *)&from_addr.sin_addr));
            }
            continue;  // Ignore packets from other devices
        }
        
        // Minimum packet size check
        if (received < 2) {
            continue;
        }
        
        // Parse Item Count
        uint16_t item_count;
        memcpy(&item_count, recv_buffer, 2);
        
        if (item_count < 2 || received < 14) {
            continue;
        }
        
        // Parse Address Item
        uint16_t addr_item_type;
        uint16_t addr_item_length;
        memcpy(&addr_item_type, recv_buffer + 2, 2);
        memcpy(&addr_item_length, recv_buffer + 4, 2);
        
        uint32_t connection_id = 0;
        size_t data_item_offset = 6;
        
        if (addr_item_type == CPF_ITEM_SEQUENCED_ADDRESS) {
            // Sequenced Address Item (8 bytes)
            if (addr_item_length != 8 || received < 14) {
                continue;
            }
            memcpy(&connection_id, recv_buffer + 6, 4);
            data_item_offset = 14;  // Skip sequence number
        } else if (addr_item_type == CPF_ITEM_CONNECTION_ADDRESS) {
            // Connection Address Item (4 bytes)
            if (addr_item_length != 4 || received < 10) {
                continue;
            }
            memcpy(&connection_id, recv_buffer + 6, 4);
            data_item_offset = 10;
        } else {
            static uint32_t unknown_addr_count = 0;
            if ((unknown_addr_count++ % 100) == 0) {
                ESP_LOGW(TAG, "Received packet with unknown address item type: 0x%04X", addr_item_type);
            }
            continue;
        }
        
        // Verify Connection ID matches T-to-O Connection ID
        if (connection_id != conn->t_to_o_connection_id) {
            static uint32_t wrong_conn_id_count = 0;
            if ((wrong_conn_id_count++ % 100) == 0) {
                ESP_LOGW(TAG, "Received packet with wrong connection ID: 0x%08lX (expected 0x%08lX) - ignoring",
                         (unsigned long)connection_id, (unsigned long)conn->t_to_o_connection_id);
                ESP_LOGW(TAG, "  T->O Instance: %u, Expected data size: %u bytes", 
                         conn->assembly_instance_produced, conn->assembly_data_size_produced);
                ESP_LOGW(TAG, "  This may indicate the adapter is using a different connection ID than expected");
                ESP_LOGW(TAG, "  Check Forward Open response - device may have assigned different IDs");
            }
            continue;  // Different connection, ignore
        }
        
        // Parse Data Item
        if (received < data_item_offset + 4) {
            continue;
        }
        
        uint16_t data_item_type;
        uint16_t data_item_length;
        memcpy(&data_item_type, recv_buffer + data_item_offset, 2);
        memcpy(&data_item_length, recv_buffer + data_item_offset + 2, 2);
        
        if (data_item_type != CPF_ITEM_CONNECTED_DATA) {
            continue;
        }
        
        // For Class 1, skip 2-byte CIP sequence count
        // Expected data_item_length = CIP seq (2) + Assembly data size
        size_t assembly_data_offset = data_item_offset + 4;  // Skip data item header
        uint16_t expected_data_length = 2 + conn->assembly_data_size_produced;  // CIP seq + assembly data
        
        if (data_item_length == expected_data_length) {
            // Class 1: Skip CIP sequence count (2 bytes)
            assembly_data_offset += 2;
        } else if (data_item_length == conn->assembly_data_size_produced) {
            // Class 0: No sequence count (unlikely for implicit messaging)
            // assembly_data_offset stays the same
        } else {
            ESP_LOGW(TAG, "Unexpected data item length: %u (expected %u or %u)", 
                     data_item_length, expected_data_length, conn->assembly_data_size_produced);
            continue;
        }
        
        uint16_t assembly_data_length = conn->assembly_data_size_produced;
        
        // Extract Assembly data
        if (received < assembly_data_offset + assembly_data_length) {
            continue;
        }
        
        // Update last packet time for watchdog
        conn->last_packet_time = xTaskGetTickCount();
        
        
        // Call user callback if provided (safely check conn->valid first)
        // Check valid flag first (atomic read)
        bool conn_valid = conn->valid;
        if (conn->user_data != NULL && conn_valid) {
            // Use the same structure definition as in enip_scanner_implicit_open
            typedef struct {
                enip_implicit_data_callback_t callback;
                void *user_data;
                uint8_t *o_to_t_data;  // Dynamic allocation for O-to-T data
                uint16_t o_to_t_data_length;
                SemaphoreHandle_t data_mutex;  // Mutex to protect o_to_t_data access
            } callback_wrapper_t;
            
            callback_wrapper_t *wrapper = (callback_wrapper_t *)conn->user_data;
            if (wrapper && wrapper->callback) {
                // Extract assembly data
                // Allocate buffer for assembly data (use actual size)
                uint8_t *assembly_data = malloc(assembly_data_length);
                if (assembly_data != NULL) {
                    memcpy(assembly_data, recv_buffer + assembly_data_offset, assembly_data_length);
                    wrapper->callback(&conn->ip_address, conn->assembly_instance_produced,
                                    assembly_data, assembly_data_length, wrapper->user_data);
                    free(assembly_data);
                } else {
                    ESP_LOGW(TAG, "Failed to allocate memory for assembly data callback");
                }
            } else {
                static uint32_t no_callback_count = 0;
                if ((no_callback_count++ % 100) == 0) {
                    ESP_LOGW(TAG, "No callback available for received data (wrapper=%p, callback=%p)",
                             wrapper, wrapper ? wrapper->callback : NULL);
                }
            }
        }
    }
    
    vTaskDelete(NULL);
}


static void watchdog_task(void *pvParameters)
{
    enip_implicit_connection_t *conn = (enip_implicit_connection_t *)pvParameters;
    if (conn == NULL) {
        vTaskDelete(NULL);
        return;
    }
    
    while (conn->state == ENIP_CONN_STATE_OPEN && conn->valid) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Check every 100ms
        
        // Check if we're still sending O->T heartbeats
        uint32_t time_since_last_heartbeat = 0;
        if (conn->last_heartbeat_time != 0) {
            time_since_last_heartbeat = xTaskGetTickCount() - conn->last_heartbeat_time;
        }
        
        uint32_t heartbeat_timeout_ticks = (conn->rpi_ms * 2) / portTICK_PERIOD_MS;
        if (time_since_last_heartbeat > heartbeat_timeout_ticks && conn->last_heartbeat_time != 0) {
            continue;
        }
        
        if (conn->last_packet_time != 0) {
            uint32_t timeout_ms = conn->rpi_ms * 20;
            if (timeout_ms < 5000) {
                timeout_ms = 5000;
            }
            uint32_t watchdog_timeout_ms = timeout_ms;
            if (timeout_ms < 10000) {
                watchdog_timeout_ms = 10000;
            }
            uint32_t timeout_ticks = watchdog_timeout_ms / portTICK_PERIOD_MS;
            uint32_t time_since_last_packet = xTaskGetTickCount() - conn->last_packet_time;
            
            if (time_since_last_packet > timeout_ticks) {
                uint32_t elapsed_ms = (unsigned long)(time_since_last_packet * portTICK_PERIOD_MS);
                ESP_LOGW(TAG, "Connection timeout detected (%lu ms) - No T->O packets received for %lu ms", 
                         elapsed_ms, elapsed_ms);
                ESP_LOGW(TAG, "  RPI: %lu ms, Timeout threshold: %lu ms (20x RPI, min 10s)", 
                         (unsigned long)conn->rpi_ms, (unsigned long)watchdog_timeout_ms);
                ESP_LOGW(TAG, "  We ARE sending O->T heartbeats, but adapter is NOT sending T->O data packets");
                ESP_LOGW(TAG, "  Possible causes: Adapter not configured for T->O, wrong connection ID, or network issue");
                conn->state = ENIP_CONN_STATE_CLOSING;
                conn->valid = false;
                break;
            }
        }
    }
    
    vTaskDelete(NULL);
}

#define MAX_IMPLICIT_CONNECTIONS 8
static enip_implicit_connection_t s_connections[MAX_IMPLICIT_CONNECTIONS];
static bool s_connections_initialized = false;

static enip_implicit_connection_t *find_or_create_connection(const ip4_addr_t *ip_address)
{
    // Create mutex on first call if needed
    if (s_connections_mutex == NULL) {
        s_connections_mutex = xSemaphoreCreateMutex();
        if (s_connections_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create connections mutex");
            return NULL;
        }
    }
    
    if (xSemaphoreTake(s_connections_mutex, portMAX_DELAY) != pdTRUE) {
        return NULL;
    }
    
    if (!s_connections_initialized) {
        memset(s_connections, 0, sizeof(s_connections));
        s_connections_initialized = true;
    }
    
    enip_implicit_connection_t *found_conn = NULL;
    
    for (int i = 0; i < MAX_IMPLICIT_CONNECTIONS; i++) {
        if (s_connections[i].valid && 
            s_connections[i].ip_address.addr == ip_address->addr) {
            found_conn = &s_connections[i];
            break;
        }
    }
    
    if (found_conn == NULL) {
        for (int i = 0; i < MAX_IMPLICIT_CONNECTIONS; i++) {
            if (!s_connections[i].valid) {
                memset(&s_connections[i], 0, sizeof(enip_implicit_connection_t));
                s_connections[i].ip_address = *ip_address;
                s_connections[i].state = ENIP_CONN_STATE_IDLE;
                found_conn = &s_connections[i];
                break;
            }
        }
    }
    
    xSemaphoreGive(s_connections_mutex);
    return found_conn;  // NULL if no free slots
}


esp_err_t enip_scanner_implicit_open(const ip4_addr_t *ip_address,
                                     uint16_t assembly_instance_consumed,
                                     uint16_t assembly_instance_produced,
                                     uint16_t assembly_data_size_consumed,
                                     uint16_t assembly_data_size_produced,
                                     uint32_t rpi_ms,
                                     enip_implicit_data_callback_t callback,
                                     void *user_data,
                                     uint32_t timeout_ms,
                                     bool exclusive_owner)
{
    if (ip_address == NULL || callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (rpi_ms < 10 || rpi_ms > 10000) {
        ESP_LOGE(TAG, "Invalid RPI: %lu ms (must be 10-10000)", (unsigned long)rpi_ms);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_scanner_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_scanner_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    bool initialized = s_scanner_initialized;
    xSemaphoreGive(s_scanner_mutex);
    
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    enip_implicit_connection_t *conn = find_or_create_connection(ip_address);
    if (conn == NULL) {
        ESP_LOGE(TAG, "No free connection slots available");
        return ESP_ERR_NO_MEM;
    }
    
    // Check if connection is already open (need to take mutex again)
    if (s_connections_mutex != NULL && xSemaphoreTake(s_connections_mutex, portMAX_DELAY) == pdTRUE) {
        if (conn->valid && conn->state == ENIP_CONN_STATE_OPEN) {
            xSemaphoreGive(s_connections_mutex);
            ESP_LOGW(TAG, "Connection already open for this IP");
            return ESP_ERR_INVALID_STATE;
        }
        xSemaphoreGive(s_connections_mutex);
    }
    conn->assembly_instance_consumed = assembly_instance_consumed;
    conn->assembly_instance_produced = assembly_instance_produced;
    conn->rpi_ms = rpi_ms;
    conn->exclusive_owner = exclusive_owner;
    conn->state = ENIP_CONN_STATE_OPENING;
    conn->user_data = user_data;
    conn->last_packet_time = 0;
    conn->last_heartbeat_time = 0;
    
    conn->tcp_socket = create_tcp_socket(ip_address, timeout_ms);
    if (conn->tcp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create TCP socket");
        conn->state = ENIP_CONN_STATE_IDLE;
        return ESP_FAIL;
    }
    
    esp_err_t ret = register_session(conn->tcp_socket, &conn->session_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register session: %s", esp_err_to_name(ret));
        close(conn->tcp_socket);
        conn->tcp_socket = -1;
        conn->state = ENIP_CONN_STATE_IDLE;
        return ret;
    }
    
    if (assembly_data_size_consumed == 0) {
        ESP_LOGD(TAG, "Autodetecting consumed assembly data size for instance %u", assembly_instance_consumed);
        ret = read_assembly_data_size(conn->tcp_socket, conn->session_handle, 
                                      assembly_instance_consumed, 
                                      &conn->assembly_data_size_consumed, 
                                      timeout_ms);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to autodetect consumed assembly data size: %s", esp_err_to_name(ret));
            ESP_LOGW(TAG, "You may need to specify assembly_data_size_consumed manually");
            unregister_session(conn->tcp_socket, conn->session_handle);
            close(conn->tcp_socket);
            conn->tcp_socket = -1;
            conn->state = ENIP_CONN_STATE_IDLE;
            return ESP_ERR_NOT_FOUND;
        }
    } else {
        conn->assembly_data_size_consumed = assembly_data_size_consumed;
    }
    
    if (assembly_data_size_produced == 0) {
        ESP_LOGD(TAG, "Autodetecting produced assembly data size for instance %u", assembly_instance_produced);
        ret = read_assembly_data_size(conn->tcp_socket, conn->session_handle, 
                                      assembly_instance_produced, 
                                      &conn->assembly_data_size_produced, 
                                      timeout_ms);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to autodetect produced assembly data size: %s", esp_err_to_name(ret));
            ESP_LOGW(TAG, "You may need to specify assembly_data_size_produced manually");
            unregister_session(conn->tcp_socket, conn->session_handle);
            close(conn->tcp_socket);
            conn->tcp_socket = -1;
            conn->state = ENIP_CONN_STATE_IDLE;
            return ESP_ERR_NOT_FOUND;
        }
    } else {
        conn->assembly_data_size_produced = assembly_data_size_produced;
    }
    
    ESP_LOGD(TAG, "Assembly sizes: Consumed=%u bytes, Produced=%u bytes", 
             conn->assembly_data_size_consumed, conn->assembly_data_size_produced);
    
    ret = forward_open(conn, timeout_ms);
    if (ret != ESP_OK) {
        unregister_session(conn->tcp_socket, conn->session_handle);
        close(conn->tcp_socket);
        conn->tcp_socket = -1;
        conn->state = ENIP_CONN_STATE_IDLE;
        return ret;
    }
    
    // Create UDP socket
    conn->udp_socket = create_udp_socket();
    if (conn->udp_socket < 0) {
        forward_close(conn, timeout_ms);
        unregister_session(conn->tcp_socket, conn->session_handle);
        close(conn->tcp_socket);
        conn->tcp_socket = -1;
        conn->state = ENIP_CONN_STATE_IDLE;
        return ESP_FAIL;
    }
    
    // Store callback (we'll need to wrap it)
    // For now, store callback pointer in user_data field
    // We'll need a wrapper structure to store both callback and user_data
    typedef struct {
        enip_implicit_data_callback_t callback;
        void *user_data;
        uint8_t *o_to_t_data;  // Dynamic allocation for O-to-T data
        uint16_t o_to_t_data_length;
        SemaphoreHandle_t data_mutex;  // Mutex to protect o_to_t_data access
    } callback_wrapper_t;
    
    callback_wrapper_t *wrapper = malloc(sizeof(callback_wrapper_t));
    if (wrapper == NULL) {
        close(conn->udp_socket);
        forward_close(conn, timeout_ms);
        unregister_session(conn->tcp_socket, conn->session_handle);
        close(conn->tcp_socket);
        conn->tcp_socket = -1;
        conn->state = ENIP_CONN_STATE_IDLE;
        return ESP_ERR_NO_MEM;
    }
    wrapper->callback = callback;
    wrapper->user_data = user_data;
    wrapper->o_to_t_data = NULL;
    wrapper->o_to_t_data_length = 0;
    wrapper->data_mutex = xSemaphoreCreateMutex();
    if (wrapper->data_mutex == NULL) {
        free(wrapper);
        close(conn->udp_socket);
        forward_close(conn, timeout_ms);
        unregister_session(conn->tcp_socket, conn->session_handle);
        close(conn->tcp_socket);
        conn->tcp_socket = -1;
        conn->state = ENIP_CONN_STATE_IDLE;
        return ESP_ERR_NO_MEM;
    }
    
    // Read initial O->T assembly data from the device
    // This ensures we start with the current state, not zeros
    enip_scanner_assembly_result_t assembly_result = {0};
    ret = enip_scanner_read_assembly(ip_address, assembly_instance_consumed, &assembly_result, timeout_ms);
    
    // Protect initial data write with mutex
    if (xSemaphoreTake(wrapper->data_mutex, portMAX_DELAY) == pdTRUE) {
        if (ret == ESP_OK && assembly_result.data_length > 0) {
            // Allocate buffer for O-to-T data
            wrapper->o_to_t_data = malloc(conn->assembly_data_size_consumed);
            if (wrapper->o_to_t_data != NULL) {
                // Copy the read data
                uint16_t copy_size = (assembly_result.data_length < conn->assembly_data_size_consumed) ?
                                     assembly_result.data_length : conn->assembly_data_size_consumed;
                memcpy(wrapper->o_to_t_data, assembly_result.data, copy_size);
                
                // Zero-pad if the read data is shorter than expected
                if (copy_size < conn->assembly_data_size_consumed) {
                    memset(wrapper->o_to_t_data + copy_size, 0, conn->assembly_data_size_consumed - copy_size);
                }
                
                wrapper->o_to_t_data_length = conn->assembly_data_size_consumed;
            } else {
                ESP_LOGW(TAG, "Failed to allocate buffer for initial O->T data");
            }
        } else {
            ESP_LOGW(TAG, "Failed to read initial O->T assembly data: %s (will start with zeros)", 
                     ret == ESP_OK ? "empty data" : esp_err_to_name(ret));
            // Allocate zero-filled buffer
            wrapper->o_to_t_data = malloc(conn->assembly_data_size_consumed);
            if (wrapper->o_to_t_data != NULL) {
                memset(wrapper->o_to_t_data, 0, conn->assembly_data_size_consumed);
                wrapper->o_to_t_data_length = conn->assembly_data_size_consumed;  // Always set to full buffer size
            }
        }
        xSemaphoreGive(wrapper->data_mutex);
    }
    
    // Free assembly result
    enip_scanner_free_assembly_result(&assembly_result);
    
    conn->user_data = wrapper;
    conn->state = ENIP_CONN_STATE_OPEN;
    conn->valid = true;
    conn->last_packet_time = xTaskGetTickCount();
    
    xTaskCreate(heartbeat_task, "enip_hb", 4096, conn, 4, &conn->heartbeat_task_handle);
    xTaskCreate(receive_task, "enip_recv", 4096, conn, 5, &conn->receive_task_handle);
    xTaskCreate(watchdog_task, "enip_wdog", 4096, conn, 1, &conn->watchdog_task_handle);
    
    ESP_LOGI(TAG, "Implicit connection opened: O-to-T=0x%08lX, T-to-O=0x%08lX",
             (unsigned long)conn->o_to_t_connection_id, (unsigned long)conn->t_to_o_connection_id);
    
    return ESP_OK;
}

esp_err_t enip_scanner_implicit_close(const ip4_addr_t *ip_address, uint32_t timeout_ms)
{
    if (ip_address == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_connections_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    enip_implicit_connection_t *conn = NULL;
    if (xSemaphoreTake(s_connections_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_FAIL;
    }
    
    for (int i = 0; i < MAX_IMPLICIT_CONNECTIONS; i++) {
        if (s_connections[i].valid && 
            s_connections[i].ip_address.addr == ip_address->addr) {
            conn = &s_connections[i];
            break;
        }
    }
    
    if (conn == NULL || !conn->valid) {
        xSemaphoreGive(s_connections_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Save state and socket before releasing mutex
    bool was_open = (conn->state == ENIP_CONN_STATE_OPEN);
    int saved_tcp_socket = conn->tcp_socket;
    xSemaphoreGive(s_connections_mutex);
    
    // Proper Forward Close Sequence:
    // Per device implementation: Device stops I/O immediately when it receives Forward Close
    // Per Wireshark analysis: Forward Close gets quick response when sent on active connection
    // Strategy: Send Forward Close while connection is active, then stop heartbeats after response
    bool forward_close_success = false;
    
    if (saved_tcp_socket >= 0 && was_open) {
        // Send Forward Close while connection is still fully active (heartbeats AND receive both running)
        // This ensures device receives it on an active connection and responds quickly
        
        uint32_t fc_timeout = timeout_ms > 5000 ? 5000 : timeout_ms;
        esp_err_t send_ret = forward_close(conn, fc_timeout);
        
        if (send_ret == ESP_OK) {
            forward_close_success = true;
        } else {
            ESP_LOGW(TAG, "Forward Close failed - device will timeout connection");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } else {
        ESP_LOGW(TAG, "Cannot send Forward Close: socket=%d, state was %s", saved_tcp_socket, was_open ? "OPEN" : "not OPEN");
    }
    
    // Mark connection as invalid to stop tasks
    // Tasks check conn->valid in their loops and will exit when it becomes false
    // They will call vTaskDelete(NULL) themselves - we must NOT try to delete them externally
    // Trying to delete a task that already deleted itself causes a crash (store access fault)
    if (xSemaphoreTake(s_connections_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        conn->valid = false;
        // Clear task handles immediately - tasks will delete themselves
        conn->heartbeat_task_handle = NULL;
        conn->receive_task_handle = NULL;
        conn->watchdog_task_handle = NULL;
        xSemaphoreGive(s_connections_mutex);
    }
    
    // Give tasks a short time to see conn->valid = false and exit gracefully
    // Tasks check conn->valid in their loops and will exit, then call vTaskDelete(NULL)
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // Access connection fields safely (after tasks have had time to exit)
    // We already have tcp_socket saved above, just need other fields
    int udp_socket = -1;
    uint32_t rpi_ms = 0;
    uint32_t session_handle = 0;
    
    if (xSemaphoreTake(s_connections_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        udp_socket = conn->udp_socket;
        rpi_ms = conn->rpi_ms;
        session_handle = conn->session_handle;
        xSemaphoreGive(s_connections_mutex);
    }
    
    if (udp_socket >= 0) {
        if (forward_close_success) {
            vTaskDelay(pdMS_TO_TICKS(200));
        } else {
            uint32_t watchdog_timeout_ms = (rpi_ms * 16) + 10000;
            if (watchdog_timeout_ms < 13000) {
                watchdog_timeout_ms = 13000;
            }
            ESP_LOGW(TAG, "Forward Close timed out - waiting %lu ms for device watchdog", 
                     (unsigned long)watchdog_timeout_ms);
            vTaskDelay(pdMS_TO_TICKS(watchdog_timeout_ms));
        }
        shutdown(udp_socket, SHUT_RDWR);
        close(udp_socket);
        
        if (xSemaphoreTake(s_connections_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            conn->udp_socket = -1;
            xSemaphoreGive(s_connections_mutex);
        }
    }
    
    if (saved_tcp_socket >= 0) {
        if (forward_close_success) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        unregister_session(saved_tcp_socket, session_handle);
        close(saved_tcp_socket);
        
        if (xSemaphoreTake(s_connections_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            conn->tcp_socket = -1;
            xSemaphoreGive(s_connections_mutex);
        }
    }
    
    // Free callback wrapper (must be done after tasks exit to avoid use-after-free)
    if (xSemaphoreTake(s_connections_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (conn->user_data != NULL) {
            // Free callback wrapper and its data buffer
            typedef struct {
                enip_implicit_data_callback_t callback;
                void *user_data;
                uint8_t *o_to_t_data;
                uint16_t o_to_t_data_length;
                SemaphoreHandle_t data_mutex;  // Mutex to protect o_to_t_data access
            } callback_wrapper_t;
            callback_wrapper_t *wrapper = (callback_wrapper_t *)conn->user_data;
            if (wrapper) {
                // Delete mutex if it exists
                if (wrapper->data_mutex != NULL) {
                    vSemaphoreDelete(wrapper->data_mutex);
                    wrapper->data_mutex = NULL;
                }
                if (wrapper->o_to_t_data) {
                    free(wrapper->o_to_t_data);
                }
                free(wrapper);
            }
            conn->user_data = NULL;
        }
        
        memset(conn, 0, sizeof(enip_implicit_connection_t));
        xSemaphoreGive(s_connections_mutex);
    }
    
    return ESP_OK;
}

esp_err_t enip_scanner_implicit_write_data(const ip4_addr_t *ip_address,
                                          const uint8_t *data,
                                          uint16_t data_length)
{
    if (ip_address == NULL || data == NULL || data_length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_connections_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    enip_implicit_connection_t *conn = NULL;
    if (xSemaphoreTake(s_connections_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    for (int i = 0; i < MAX_IMPLICIT_CONNECTIONS; i++) {
        if (s_connections[i].valid && 
            s_connections[i].ip_address.addr == ip_address->addr &&
            s_connections[i].state == ENIP_CONN_STATE_OPEN) {
            conn = &s_connections[i];
            break;
        }
    }
    
    if (conn == NULL || !conn->valid) {
        xSemaphoreGive(s_connections_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    xSemaphoreGive(s_connections_mutex);
    
    if (data_length > conn->assembly_data_size_consumed) {
        ESP_LOGE(TAG, "Data length too large: %u (max %u bytes)", data_length, conn->assembly_data_size_consumed);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Store data in connection structure for heartbeat task to use
    // Uses mutex-protected buffer to avoid race conditions
    typedef struct {
        enip_implicit_data_callback_t callback;
        void *user_data;
        uint8_t *o_to_t_data;  // Dynamic allocation
        uint16_t o_to_t_data_length;
        SemaphoreHandle_t data_mutex;  // Mutex to protect o_to_t_data access
    } callback_wrapper_t;
    
    callback_wrapper_t *wrapper = (callback_wrapper_t *)conn->user_data;
    if (wrapper == NULL || wrapper->data_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Access O-to-T data with mutex protection
    if (xSemaphoreTake(wrapper->data_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    // Allocate/reallocate buffer if needed
    if (wrapper->o_to_t_data == NULL || wrapper->o_to_t_data_length < conn->assembly_data_size_consumed) {
        if (wrapper->o_to_t_data != NULL) {
            free(wrapper->o_to_t_data);
        }
        wrapper->o_to_t_data = malloc(conn->assembly_data_size_consumed);
        if (wrapper->o_to_t_data == NULL) {
            xSemaphoreGive(wrapper->data_mutex);
            return ESP_ERR_NO_MEM;
        }
    }
    
    // Copy the data and ensure the buffer is exactly assembly_data_size_consumed bytes
    memcpy(wrapper->o_to_t_data, data, data_length);
    
    // Zero out remaining bytes to ensure full buffer is used
    if (data_length < conn->assembly_data_size_consumed) {
        memset(wrapper->o_to_t_data + data_length, 0, conn->assembly_data_size_consumed - data_length);
    }
    
    // Always set length to full buffer size (memory always contains full assembly size)
    wrapper->o_to_t_data_length = conn->assembly_data_size_consumed;
    
    xSemaphoreGive(wrapper->data_mutex);
    return ESP_OK;
}

esp_err_t enip_scanner_implicit_read_o_to_t_data(const ip4_addr_t *ip_address,
                                                  uint8_t *data,
                                                  uint16_t *data_length,
                                                  uint16_t max_length)
{
    if (ip_address == NULL || data == NULL || data_length == NULL || max_length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_connections_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    enip_implicit_connection_t *conn = NULL;
    if (xSemaphoreTake(s_connections_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    for (int i = 0; i < MAX_IMPLICIT_CONNECTIONS; i++) {
        if (s_connections[i].valid && 
            s_connections[i].ip_address.addr == ip_address->addr &&
            s_connections[i].state == ENIP_CONN_STATE_OPEN) {
            conn = &s_connections[i];
            break;
        }
    }
    
    if (conn == NULL || !conn->valid) {
        xSemaphoreGive(s_connections_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    xSemaphoreGive(s_connections_mutex);
    
    typedef struct {
        enip_implicit_data_callback_t callback;
        void *user_data;
        uint8_t *o_to_t_data;  // Dynamic allocation
        uint16_t o_to_t_data_length;
        SemaphoreHandle_t data_mutex;  // Mutex to protect o_to_t_data access
    } callback_wrapper_t;
    
    callback_wrapper_t *wrapper = (callback_wrapper_t *)conn->user_data;
    if (wrapper == NULL || wrapper->data_mutex == NULL) {
        // No data written yet, return zeros
        uint16_t copy_size = (max_length < conn->assembly_data_size_consumed) ? 
                             max_length : conn->assembly_data_size_consumed;
        memset(data, 0, copy_size);
        *data_length = copy_size;
        return ESP_OK;
    }
    
    // Copy the stored O-to-T data from memory with mutex protection
    // The buffer is always allocated to assembly_data_size_consumed bytes
    uint16_t copy_size = (max_length < conn->assembly_data_size_consumed) ? 
                         max_length : conn->assembly_data_size_consumed;
    
    // Access O-to-T data with mutex protection
    if (xSemaphoreTake(wrapper->data_mutex, portMAX_DELAY) != pdTRUE) {
        memset(data, 0, copy_size);
        *data_length = copy_size;
        return ESP_FAIL;
    }
    
    if (wrapper->o_to_t_data != NULL) {
        // Buffer exists in memory - copy it
        // Always copy the full buffer size (assembly_data_size_consumed)
        // even if o_to_t_data_length is less (it should always be full size)
        uint16_t buffer_size = (wrapper->o_to_t_data_length > 0) ? 
                              wrapper->o_to_t_data_length : conn->assembly_data_size_consumed;
        uint16_t actual_copy = (copy_size < buffer_size) ? copy_size : buffer_size;
        memcpy(data, wrapper->o_to_t_data, actual_copy);
        // Zero-pad if needed (shouldn't happen if buffer is properly sized)
        if (actual_copy < copy_size) {
            memset(data + actual_copy, 0, copy_size - actual_copy);
        }
        *data_length = copy_size;
    } else {
        // Buffer not allocated yet (shouldn't happen after connection open)
        // Return zeros for the full assembly size
        memset(data, 0, copy_size);
        *data_length = copy_size;
    }
    
    xSemaphoreGive(wrapper->data_mutex);
    return ESP_OK;
}

#endif // CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT


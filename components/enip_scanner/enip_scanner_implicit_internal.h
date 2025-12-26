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

#ifndef ENIP_SCANNER_IMPLICIT_INTERNAL_H
#define ENIP_SCANNER_IMPLICIT_INTERNAL_H

#include "lwip/ip4_addr.h"
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for shared functions from enip_scanner.c
int create_tcp_socket(const ip4_addr_t *ip_addr, uint32_t timeout_ms);
esp_err_t register_session(int sock, uint32_t *session_handle);
void unregister_session(int sock, uint32_t session_handle);
esp_err_t send_data(int sock, const void *data, size_t len);
esp_err_t recv_data(int sock, void *data, size_t len, uint32_t timeout_ms, size_t *bytes_received);
esp_err_t enip_scanner_read_assembly_data_size(int sock, uint32_t session_handle, uint16_t assembly_instance, uint16_t *data_size, uint32_t timeout_ms);
extern SemaphoreHandle_t s_scanner_mutex;
extern bool s_scanner_initialized;

// EtherNet/IP constants
#define ENIP_PORT 44818
#define ENIP_IMPLICIT_PORT 2222
#define ENIP_SEND_RR_DATA 0x006F
#define ENIP_SEND_UNIT_DATA 0x0070

// CIP Service Codes
#define CIP_SERVICE_FORWARD_OPEN 0x54
#define CIP_SERVICE_FORWARD_CLOSE 0x4E
#define CIP_SERVICE_GET_ATTRIBUTE_SINGLE 0x0E

// CIP Classes
#define CIP_CLASS_CONNECTION_MANAGER 0x06
#define CIP_CLASS_ASSEMBLY 0x04

// Path Encoding
#define CIP_PATH_CLASS 0x20
#define CIP_PATH_INSTANCE 0x24
#define CIP_PATH_ATTRIBUTE 0x30
#define CIP_PATH_CONNECTION_POINT 0x2C
#define CIP_PATH_ATTRIBUTE 0x30

// CPF Item Types
#define CPF_ITEM_NULL_ADDRESS 0x0000
#define CPF_ITEM_CONNECTION_ADDRESS 0x00A1
#define CPF_ITEM_SEQUENCED_ADDRESS 0x8002
#define CPF_ITEM_CONNECTED_DATA 0x00B1
#define CPF_ITEM_UNCONNECTED_DATA 0x00B2

// EtherNet/IP header structure
typedef struct __attribute__((packed)) {
    uint16_t command;
    uint16_t length;
    uint32_t session_handle;
    uint32_t status;
    uint64_t sender_context;
    uint32_t options;
} enip_header_t;

// Connection State
typedef enum {
    ENIP_CONN_STATE_IDLE = 0,
    ENIP_CONN_STATE_OPENING,
    ENIP_CONN_STATE_OPEN,
    ENIP_CONN_STATE_CLOSING,
} enip_connection_state_t;

// Connection structure (internal)
typedef struct {
    ip4_addr_t ip_address;
    uint32_t session_handle;
    int tcp_socket;
    int udp_socket;
    uint16_t assembly_instance_consumed;  // O-to-T (e.g., 150)
    uint16_t assembly_instance_produced;  // T-to-O (e.g., 100)
    uint16_t assembly_data_size_consumed; // O-to-T data size in bytes (e.g., 40)
    uint16_t assembly_data_size_produced; // T-to-O data size in bytes (e.g., 72)
    uint32_t rpi_ms;
    uint32_t o_to_t_connection_id;
    uint32_t t_to_o_connection_id;
    uint16_t connection_serial_number;
    uint32_t originator_serial_number;
    uint8_t priority_time_tick;  // Priority/Time Tick byte from Forward Open (must match in Forward Close)
    uint8_t timeout_ticks;  // Timeout Ticks from Forward Open (must match in Forward Close)
    bool exclusive_owner;  // true = PTP (Point-to-Point), false = non-PTP (Multicast T-to-O)
    enip_connection_state_t state;
    void *user_data;
    uint32_t last_packet_time;  // Time of last T->O packet received
    uint32_t last_heartbeat_time;  // Time of last O->T heartbeat sent
    bool valid;
    TaskHandle_t heartbeat_task_handle;
    TaskHandle_t receive_task_handle;
    TaskHandle_t watchdog_task_handle;
} enip_implicit_connection_t;

#ifdef __cplusplus
}
#endif

#endif // ENIP_SCANNER_IMPLICIT_INTERNAL_H


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

#ifndef ENIP_SCANNER_TAG_INTERNAL_H
#define ENIP_SCANNER_TAG_INTERNAL_H

#include "enip_scanner.h"
#include "lwip/ip4_addr.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for shared functions from enip_scanner.c
// These functions are made non-static to allow tag operations to use them
int create_tcp_socket(const ip4_addr_t *ip_addr, uint32_t timeout_ms);
esp_err_t register_session(int sock, uint32_t *session_handle);
void unregister_session(int sock, uint32_t session_handle);
esp_err_t send_data(int sock, const void *data, size_t len);
esp_err_t recv_data(int sock, void *data, size_t len, uint32_t timeout_ms, size_t *bytes_received);
extern SemaphoreHandle_t s_scanner_mutex;
extern bool s_scanner_initialized;

// EtherNet/IP constants (shared)
#define ENIP_PORT 44818
#define ENIP_SEND_RR_DATA 0x006F
#define CIP_SERVICE_READ 0x4C
#define CIP_SERVICE_WRITE 0x4D

// ENIP header structure
typedef struct __attribute__((packed)) {
    uint16_t command;
    uint16_t length;
    uint32_t session_handle;
    uint32_t status;
    uint64_t sender_context;
    uint32_t options;
} enip_header_t;

#ifdef __cplusplus
}
#endif

#endif // ENIP_SCANNER_TAG_INTERNAL_H


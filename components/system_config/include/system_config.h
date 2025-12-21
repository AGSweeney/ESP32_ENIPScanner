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

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include "lwip/ip4_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IP configuration structure
 */
typedef struct {
    bool use_dhcp;              // true for DHCP, false for static
    uint32_t ip_address;        // IP address in network byte order
    uint32_t netmask;           // Network mask in network byte order
    uint32_t gateway;           // Gateway in network byte order
    uint32_t dns1;              // Primary DNS in network byte order
    uint32_t dns2;              // Secondary DNS in network byte order
} system_ip_config_t;

/**
 * @brief Get default IP configuration (DHCP enabled)
 */
void system_ip_config_get_defaults(system_ip_config_t *config);

/**
 * @brief Load IP configuration from NVS
 * @param config Pointer to config structure to fill
 * @return true if loaded successfully, false if using defaults
 */
bool system_ip_config_load(system_ip_config_t *config);

/**
 * @brief Save IP configuration to NVS
 * @param config Pointer to config structure to save
 * @return true on success, false on error
 */
bool system_ip_config_save(const system_ip_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_CONFIG_H

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

#ifndef ENIP_SCANNER_H
#define ENIP_SCANNER_H

#include <stdint.h>
#include <stdbool.h>
#include "lwip/ip4_addr.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief EtherNet/IP scanner result structure
 */
typedef struct {
    ip4_addr_t ip_address;      // Device IP address
    uint16_t vendor_id;         // Vendor ID
    uint16_t device_type;       // Device Type
    uint16_t product_code;      // Product Code
    uint8_t major_revision;      // Major Revision
    uint8_t minor_revision;      // Minor Revision
    uint16_t status;            // Status word
    uint32_t serial_number;     // Serial Number
    char product_name[33];      // Product Name (null-terminated, max 32 chars)
    bool online;                // Device is online and responding
    uint32_t response_time_ms;  // Response time in milliseconds
} enip_scanner_device_info_t;

/**
 * @brief Assembly scan result structure
 */
typedef struct {
    ip4_addr_t ip_address;      // Device IP address
    uint16_t assembly_instance; // Assembly instance number (e.g., 100, 150)
    bool success;               // Scan was successful
    uint8_t *data;              // Assembly data (allocated, caller must free)
    uint16_t data_length;       // Length of assembly data
    uint32_t response_time_ms;  // Response time in milliseconds
    char error_message[128];   // Error message if scan failed
} enip_scanner_assembly_result_t;

/**
 * @brief Initialize the EtherNet/IP scanner
 * @return ESP_OK on success
 */
esp_err_t enip_scanner_init(void);

/**
 * @brief Scan for EtherNet/IP devices on the network
 * @param devices Array to store device information
 * @param max_devices Maximum number of devices to scan for
 * @param timeout_ms Timeout for each device scan in milliseconds
 * @return Number of devices found
 */
int enip_scanner_scan_devices(enip_scanner_device_info_t *devices, int max_devices, uint32_t timeout_ms);

/**
 * @brief Read assembly data from a specific device
 * @param ip_address Target device IP address
 * @param assembly_instance Assembly instance number (e.g., 100, 150)
 * @param result Pointer to store scan result (caller must free result->data)
 * @param timeout_ms Timeout for the scan in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_read_assembly(const ip4_addr_t *ip_address, uint16_t assembly_instance, 
                                     enip_scanner_assembly_result_t *result, uint32_t timeout_ms);

/**
 * @brief Free assembly scan result data
 * @param result Pointer to scan result
 */
void enip_scanner_free_assembly_result(enip_scanner_assembly_result_t *result);

/**
 * @brief Write assembly data to a specific device
 * @param ip_address Target device IP address
 * @param assembly_instance Assembly instance number (e.g., 100, 150)
 * @param data Data to write
 * @param data_length Length of data to write
 * @param timeout_ms Timeout for the write in milliseconds
 * @param error_message Buffer to store error message (128 bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_write_assembly(const ip4_addr_t *ip_address, uint16_t assembly_instance,
                                     const uint8_t *data, uint16_t data_length, uint32_t timeout_ms,
                                     char *error_message);

/**
 * @brief Check if an assembly is writable
 * @param ip_address Target device IP address
 * @param assembly_instance Assembly instance number
 * @param timeout_ms Timeout for the check in milliseconds
 * @return true if writable, false otherwise
 */
bool enip_scanner_is_assembly_writable(const ip4_addr_t *ip_address, uint16_t assembly_instance, uint32_t timeout_ms);

/**
 * @brief Discover valid assembly instances for a device
 * @param ip_address Target device IP address
 * @param instances Array to store discovered instance numbers
 * @param max_instances Maximum number of instances to discover
 * @param timeout_ms Timeout for each probe in milliseconds
 * @return Number of valid instances found
 */
int enip_scanner_discover_assemblies(const ip4_addr_t *ip_address, uint16_t *instances, int max_instances, uint32_t timeout_ms);

/**
 * @brief Register an EtherNet/IP session
 * @param ip_address Target device IP address
 * @param session_handle Pointer to store session handle
 * @param timeout_ms Timeout for registration in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_register_session(const ip4_addr_t *ip_address,
                                        uint32_t *session_handle,
                                        uint32_t timeout_ms,
                                        char *error_message);

/**
 * @brief Unregister an EtherNet/IP session
 * @param ip_address Target device IP address
 * @param session_handle Session handle to unregister
 * @param timeout_ms Timeout for unregistration in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_unregister_session(const ip4_addr_t *ip_address,
                                          uint32_t session_handle,
                                          uint32_t timeout_ms);

#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT

/**
 * @brief CIP data type codes (from CIP specification)
 */
#define CIP_DATA_TYPE_BOOL    0xC1
#define CIP_DATA_TYPE_SINT    0xC2
#define CIP_DATA_TYPE_INT     0xC3
#define CIP_DATA_TYPE_DINT    0xC4
#define CIP_DATA_TYPE_LINT    0xC5
#define CIP_DATA_TYPE_USINT   0xC6
#define CIP_DATA_TYPE_UINT    0xC7
#define CIP_DATA_TYPE_UDINT   0xC8
#define CIP_DATA_TYPE_ULINT   0xC9
#define CIP_DATA_TYPE_REAL    0xCA
#define CIP_DATA_TYPE_LREAL   0xCB
#define CIP_DATA_TYPE_STIME   0xCC
#define CIP_DATA_TYPE_DATE    0xCD
#define CIP_DATA_TYPE_TIME_OF_DAY 0xCE
#define CIP_DATA_TYPE_DATE_AND_TIME 0xCF
#define CIP_DATA_TYPE_STRING  0xDA
#define CIP_DATA_TYPE_BYTE    0xD1
#define CIP_DATA_TYPE_WORD    0xD2
#define CIP_DATA_TYPE_DWORD   0xD3
#define CIP_DATA_TYPE_LWORD   0xD4

/**
 * @brief Tag read result structure
 */
typedef struct {
    ip4_addr_t ip_address;      // Device IP address
    char tag_path[128];         // Tag path that was read
    bool success;               // Read was successful
    uint8_t *data;              // Tag data (allocated, caller must free)
    uint16_t data_length;       // Length of tag data in bytes
    uint16_t cip_data_type;     // CIP data type code (e.g., CIP_DATA_TYPE_DINT)
    uint32_t response_time_ms;  // Response time in milliseconds
    char error_message[128];   // Error message if read failed
} enip_scanner_tag_result_t;

/**
 * @brief Read a tag from an Allen-Bradley device (Micro800, CompactLogix, etc.)
 * @param ip_address Target device IP address
 * @param tag_path Tag name/path (e.g., "MyTag", "Program:MainProgram.Tag", "MyArray[0]")
 * @param result Pointer to store result (caller must free result->data)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Tag names are case-sensitive and must match exactly
 * @note Micro800 PLCs do not support tag browsing - tag names must be known in advance
 * @note For structured tags, use dot notation: "Program:MainProgram.MyTag"
 * @note For array elements, use bracket notation: "MyArray[0]"
 */
esp_err_t enip_scanner_read_tag(const ip4_addr_t *ip_address,
                                const char *tag_path,
                                enip_scanner_tag_result_t *result,
                                uint32_t timeout_ms);

/**
 * @brief Free tag read result data
 * @param result Pointer to tag result
 */
void enip_scanner_free_tag_result(enip_scanner_tag_result_t *result);

/**
 * @brief Write a tag to an Allen-Bradley device (Micro800, CompactLogix, etc.)
 * @param ip_address Target device IP address
 * @param tag_path Tag name/path (e.g., "MyTag", "Program:MainProgram.Tag")
 * @param data Data to write
 * @param data_length Length of data to write in bytes
 * @param cip_data_type CIP data type code (e.g., CIP_DATA_TYPE_DINT)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Tag names are case-sensitive and must match exactly
 * @note Data type must match the tag's actual data type in the PLC
 * @note Not all tags are writable - check PLC configuration
 */
esp_err_t enip_scanner_write_tag(const ip4_addr_t *ip_address,
                                 const char *tag_path,
                                 const uint8_t *data,
                                 uint16_t data_length,
                                 uint16_t cip_data_type,
                                 uint32_t timeout_ms,
                                 char *error_message);

/**
 * @brief Get human-readable name for CIP data type
 * @param cip_data_type CIP data type code
 * @return String name of data type, or "Unknown" if not recognized
 */
const char *enip_scanner_get_data_type_name(uint16_t cip_data_type);

#endif // CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT

#ifdef __cplusplus
}
#endif

#endif // ENIP_SCANNER_H

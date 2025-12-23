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
#include "esp_err.h"
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
 * 
 * @note For Micro800 PLCs: CIP_DATA_TYPE_STIME (0xCC) is called "TIME" in the PLC programming environment,
 *       not "STIME" as in the CIP specification. The constant name remains CIP_DATA_TYPE_STIME
 *       for consistency with the CIP specification.
 */
#define CIP_DATA_TYPE_BOOL    0xC1  ///< Boolean (1 byte)
#define CIP_DATA_TYPE_SINT    0xC2  ///< Signed 8-bit integer
#define CIP_DATA_TYPE_INT     0xC3  ///< Signed 16-bit integer
#define CIP_DATA_TYPE_DINT    0xC4  ///< Signed 32-bit integer
#define CIP_DATA_TYPE_LINT    0xC5  ///< Signed 64-bit integer
#define CIP_DATA_TYPE_USINT   0xC6  ///< Unsigned 8-bit integer
#define CIP_DATA_TYPE_UINT    0xC7  ///< Unsigned 16-bit integer
#define CIP_DATA_TYPE_UDINT   0xC8  ///< Unsigned 32-bit integer
#define CIP_DATA_TYPE_ULINT   0xC9  ///< Unsigned 64-bit integer
#define CIP_DATA_TYPE_REAL    0xCA  ///< IEEE 754 single precision float
#define CIP_DATA_TYPE_LREAL   0xCB  ///< IEEE 754 double precision float
#define CIP_DATA_TYPE_STIME   0xCC  ///< Time (4 bytes, milliseconds). Called "TIME" on Micro800 PLCs
#define CIP_DATA_TYPE_DATE    0xCD  ///< Date (2 bytes)
#define CIP_DATA_TYPE_TIME_OF_DAY 0xCE  ///< Time of day (4 bytes, milliseconds since midnight)
#define CIP_DATA_TYPE_DATE_AND_TIME 0xCF  ///< Date and time combined (8 bytes)
#define CIP_DATA_TYPE_STRING  0xDA  ///< String (variable length, max 255 chars with 1-byte length prefix)
#define CIP_DATA_TYPE_BYTE    0xD1  ///< 8-bit bit string
#define CIP_DATA_TYPE_WORD    0xD2  ///< 16-bit bit string
#define CIP_DATA_TYPE_DWORD   0xD3  ///< 32-bit bit string
#define CIP_DATA_TYPE_LWORD   0xD4  ///< 64-bit bit string

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
 * @param tag_path Tag name/path (e.g., "MyTag", "MyArray[0]")
 * @param result Pointer to store result (caller must free result->data)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Tag names are case-sensitive and must match exactly
 * @note Micro800 PLCs do not support tag browsing - tag names must be known in advance
 * @note Micro800 PLCs do not support program-scoped tags - tags must be in the global variable table
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
 * @param tag_path Tag name/path (e.g., "MyTag", "MyArray[0]")
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

#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT

/**
 * @brief Motoman robot status structure
 * 
 * Status data from Class 0x72 (Read current status)
 * Reference: Motoman Manual 165838-1CD, Section 5.2.1
 */
typedef struct {
    ip4_addr_t ip_address;      // Robot IP address
    bool success;               // Read was successful
    uint32_t data1;             // Status Data 1 (bits: Step, 1 cycle, Auto, Running, Safety speed, Teach, Play, Command remote)
    uint32_t data2;             // Status Data 2 (bits: Reserved, Hold (Pendant), Hold (external), Hold (Command), Alarm, Error, Servo on, Reserved)
    uint32_t response_time_ms;  // Response time in milliseconds
    char error_message[128];   // Error message if read failed
} enip_scanner_motoman_status_t;

/**
 * @brief Read robot status from Motoman controller
 * 
 * Reads Class 0x72, Instance 1, Get_Attribute_All
 * Returns both Data 1 and Data 2 status words.
 * 
 * @param ip_address Target robot IP address
 * @param status Pointer to store status result
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Status bits interpretation:
 *   Data 1: bit 0=Step, bit 1=1 cycle, bit 2=Auto, bit 3=Running, bit 4=Safety speed, bit 5=Teach, bit 6=Play, bit 7=Command remote
 *   Data 2: bit 1=Hold (Pendant), bit 2=Hold (external), bit 3=Hold (Command), bit 4=Alarm, bit 5=Error, bit 6=Servo on
 */
esp_err_t enip_scanner_motoman_read_status(const ip4_addr_t *ip_address,
                                           enip_scanner_motoman_status_t *status,
                                           uint32_t timeout_ms);

/**
 * @brief Read I/O data from Motoman controller
 * 
 * Reads Class 0x78 (I/O data), Instance = signal_number / 10, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param signal_number Signal number (1-256: General input, 1001-1256: General output, etc.)
 * @param value Pointer to store I/O value (1 byte)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Instance = signal_number / 10 (per Motoman manual)
 * @note Signal number ranges:
 *   - 1-256: General input
 *   - 1001-1256: General output
 *   - 2001-2256: External input
 *   - 2501-2756: Network input (writable)
 *   - 3001-3256: External output
 *   - 3501-3756: Network output
 *   - 4001-4160: Specific input
 *   - 5001-5200: Specific output
 *   - 6001-6064: Interface panel input
 *   - 7001-7999: Auxiliary relay
 *   - 8001-8064: Control status
 *   - 8201-8220: Pseudo input
 */
esp_err_t enip_scanner_motoman_read_io(const ip4_addr_t *ip_address, uint16_t signal_number,
                                       uint8_t *value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Write I/O data to Motoman controller
 * 
 * Writes Class 0x78 (I/O data), Instance = signal_number / 10, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param signal_number Signal number (must be writable type)
 * @param value I/O value to write (1 byte)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Only writable signal types: Network input (2501-2756), Network output (3501-3756), etc.
 */
esp_err_t enip_scanner_motoman_write_io(const ip4_addr_t *ip_address, uint16_t signal_number,
                                        uint8_t value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Read byte-type variable (B) from Motoman controller
 * 
 * Reads Class 0x7A, Instance = variable_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable B number (0-based)
 * @param value Pointer to store variable value
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Instance = variable_number + 1 (when RS022=0, default)
 * @note If RS022=1, instance = variable_number (not currently supported)
 */
esp_err_t enip_scanner_motoman_read_variable_b(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               uint8_t *value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Write byte-type variable (B) to Motoman controller
 * 
 * Writes Class 0x7A, Instance = variable_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable B number (0-based)
 * @param value Variable value to write
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_write_variable_b(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 uint8_t value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Read integer-type variable (I) from Motoman controller
 * 
 * Reads Class 0x7B, Instance = variable_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable I number (0-based)
 * @param value Pointer to store variable value (int16_t)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_read_variable_i(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               int16_t *value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Write integer-type variable (I) to Motoman controller
 * 
 * Writes Class 0x7B, Instance = variable_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable I number (0-based)
 * @param value Variable value to write (int16_t)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_write_variable_i(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 int16_t value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Read double precision integer-type variable (D) from Motoman controller
 * 
 * Reads Class 0x7C, Instance = variable_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable D number (0-based)
 * @param value Pointer to store variable value (int32_t)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_read_variable_d(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               int32_t *value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Write double precision integer-type variable (D) to Motoman controller
 * 
 * Writes Class 0x7C, Instance = variable_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable D number (0-based)
 * @param value Variable value to write (int32_t)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_write_variable_d(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 int32_t value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Read real-type variable (R) from Motoman controller
 * 
 * Reads Class 0x7D, Instance = variable_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable R number (0-based)
 * @param value Pointer to store variable value (float)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_read_variable_r(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               float *value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Write real-type variable (R) to Motoman controller
 * 
 * Writes Class 0x7D, Instance = variable_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable R number (0-based)
 * @param value Variable value to write (float)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_write_variable_r(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 float value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Read register data from Motoman controller
 * 
 * Reads Class 0x79, Instance = register_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param register_number Register number (0-999)
 * @param value Pointer to store register value (uint16_t)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Instance = register_number + 1 (when RS022=0, default)
 */
esp_err_t enip_scanner_motoman_read_register(const ip4_addr_t *ip_address, uint16_t register_number,
                                             uint16_t *value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Write register data to Motoman controller
 * 
 * Writes Class 0x79, Instance = register_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param register_number Register number (0-999)
 * @param value Register value to write (uint16_t)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_write_register(const ip4_addr_t *ip_address, uint16_t register_number,
                                              uint16_t value, uint32_t timeout_ms, char *error_message);

#endif // CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT

#ifdef __cplusplus
}
#endif

#endif // ENIP_SCANNER_H

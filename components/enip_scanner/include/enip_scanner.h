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
 * @brief Configure RS022 instance mapping
 *
 * When enabled, instance = variable/register number (RS022=1).
 * When disabled, instance = number + 1 (RS022=0, default).
 */
void enip_scanner_motoman_set_rs022_instance_direct(bool instance_direct);

/**
 * @brief Get current RS022 instance mapping mode
 */
bool enip_scanner_motoman_get_rs022_instance_direct(void);


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

/**
 * @brief Motoman alarm structure
 * 
 * Alarm data from Class 0x70 (Current alarm) or Class 0x71 (Alarm history)
 * Reference: Motoman Manual 165838-1CD, Section 5.2.1
 */
typedef struct {
    ip4_addr_t ip_address;      // Robot IP address
    bool success;               // Read was successful
    uint32_t alarm_code;        // Alarm code (0-9999)
    uint32_t alarm_data;        // Alarm data
    uint32_t alarm_data_type;   // Alarm data type (0-10, see manual)
    char alarm_date_time[17];   // Alarm occurrence date/time (16 bytes + null terminator, format: "2010/10/10 10:10")
    char alarm_string[33];      // Alarm name string (32 bytes + null terminator)
    char error_message[128];    // Error message if read failed
} enip_scanner_motoman_alarm_t;

/**
 * @brief Read current alarm from Motoman controller
 * 
 * Reads Class 0x70, Instance 1-4, Get_Attribute_All
 * Instance: 1=Latest alarm, 2=Alarm immediately before 1, 3=Alarm immediately before 2, 4=Alarm immediately before 3
 * 
 * @param ip_address Target robot IP address
 * @param alarm_instance Alarm instance (1-4)
 * @param alarm Pointer to store alarm result
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_read_alarm(const ip4_addr_t *ip_address, uint8_t alarm_instance,
                                         enip_scanner_motoman_alarm_t *alarm, uint32_t timeout_ms);

/**
 * @brief Read alarm history from Motoman controller
 * 
 * Reads Class 0x71, Instance ranges:
 * - 1-100: Major failure
 * - 1001-1100: Minor failure
 * - 2001-2100: User (System)
 * - 3001-3100: User (User)
 * - 4001-4100: Off-line
 * 
 * @param ip_address Target robot IP address
 * @param alarm_instance Alarm history instance number
 * @param alarm Pointer to store alarm result
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_read_alarm_history(const ip4_addr_t *ip_address, uint16_t alarm_instance,
                                                  enip_scanner_motoman_alarm_t *alarm, uint32_t timeout_ms);

/**
 * @brief Motoman job information structure
 * 
 * Job information from Class 0x73 (Read current active job information)
 * Reference: Motoman Manual 165838-1CD, Section 5.2.1
 */
typedef struct {
    ip4_addr_t ip_address;      // Robot IP address
    bool success;               // Read was successful
    char job_name[33];          // Job name (32 bytes + null terminator)
    uint32_t line_number;       // Line number (0-9999)
    uint32_t step_number;       // Step number (1-9998)
    uint32_t speed_override;    // Speed override value (unit: 0.01%)
    char error_message[128];    // Error message if read failed
} enip_scanner_motoman_job_info_t;

/**
 * @brief Read active job information from Motoman controller
 * 
 * Reads Class 0x73, Instance 1, Get_Attribute_All
 * 
 * @param ip_address Target robot IP address
 * @param job_info Pointer to store job information result
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_read_job_info(const ip4_addr_t *ip_address,
                                            enip_scanner_motoman_job_info_t *job_info,
                                            uint32_t timeout_ms);

/**
 * @brief Motoman axis configuration structure
 * 
 * Axis configuration from Class 0x74 (Read current axis configuration)
 * Reference: Motoman Manual 165838-1CD, Section 5.2.1
 */
typedef struct {
    ip4_addr_t ip_address;      // Robot IP address
    bool success;               // Read was successful
    char axis_names[8][5];      // Axis coordinate names (8 axes, 4 bytes each + null terminator)
    char error_message[128];    // Error message if read failed
} enip_scanner_motoman_axis_config_t;

/**
 * @brief Read axis configuration from Motoman controller
 * 
 * Reads Class 0x74, Instance = control_group, Get_Attribute_All
 * Instance ranges:
 * - 1-8: Robot (pulse)
 * - 11-18: Base (pulse)
 * - 21-44: Station (Pulse)
 * - 101-108: Robot (robot coordinate, X-Y coordinate)
 * - 111-118: Base (linear)
 * 
 * @param ip_address Target robot IP address
 * @param control_group Control group number
 * @param config Pointer to store axis configuration result
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_read_axis_config(const ip4_addr_t *ip_address, uint16_t control_group,
                                               enip_scanner_motoman_axis_config_t *config, uint32_t timeout_ms);

/**
 * @brief Motoman robot position structure
 * 
 * Position data from Class 0x75 (Read current robot position) or Class 0x7F (Position variables)
 * Reference: Motoman Manual 165838-1CD, Section 5.2.1
 */
typedef struct {
    ip4_addr_t ip_address;      // Robot IP address
    bool success;               // Read was successful
    uint32_t data_type;         // Position data type: 0=Pulse, 16=Base, 17=Robot, 18=Tool, 19=User coordinates
    uint32_t configuration;     // Configuration bits (Back, Lower arm, No flip, R/T/S axis ≥ 180°)
    uint32_t tool_number;       // Tool number
    uint32_t reservation;       // Reservation (or User coordinate number for Class 0x7F)
    uint32_t extended_configuration; // Extended configuration (7-axis robot: θL/θU/θB/θE/θW ≥ 180°)
    int32_t axis_data[8];       // Axis data (8 axes, 4 bytes each)
    char error_message[128];    // Error message if read failed
} enip_scanner_motoman_position_t;

/**
 * @brief Read robot position from Motoman controller
 * 
 * Reads Class 0x75, Instance = control_group, Get_Attribute_All
 * Instance ranges:
 * - 1-8: Robot (Pulse)
 * - 11-18: Base (Pulse)
 * - 21-44: Station (Pulse)
 * - 101-108: Robot (Base)
 * 
 * @param ip_address Target robot IP address
 * @param control_group Control group number
 * @param position Pointer to store position result
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_read_position(const ip4_addr_t *ip_address, uint16_t control_group,
                                             enip_scanner_motoman_position_t *position, uint32_t timeout_ms);

/**
 * @brief Motoman position deviation structure
 * 
 * Position deviation from Class 0x76 (Read deviation of each axis position)
 * Reference: Motoman Manual 165838-1CD, Section 5.2.1
 */
typedef struct {
    ip4_addr_t ip_address;      // Robot IP address
    bool success;               // Read was successful
    int32_t axis_deviation[8];  // Deviation of each axis position (8 axes, pulse values)
    char error_message[128];    // Error message if read failed
} enip_scanner_motoman_position_deviation_t;

/**
 * @brief Read position deviation from Motoman controller
 * 
 * Reads Class 0x76, Instance = control_group, Get_Attribute_All
 * Instance ranges:
 * - 1-8: Robot
 * - 11-18: Base
 * - 21-44: Station
 * 
 * @param ip_address Target robot IP address
 * @param control_group Control group number
 * @param deviation Pointer to store position deviation result
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_read_position_deviation(const ip4_addr_t *ip_address, uint16_t control_group,
                                                      enip_scanner_motoman_position_deviation_t *deviation,
                                                      uint32_t timeout_ms);

/**
 * @brief Motoman torque structure
 * 
 * Torque data from Class 0x77 (Read torque of each axis)
 * Reference: Motoman Manual 165838-1CD, Section 5.2.1
 */
typedef struct {
    ip4_addr_t ip_address;      // Robot IP address
    bool success;               // Read was successful
    int32_t axis_torque[8];     // Torque of each axis (8 axes, percentage when nominal value is 100%)
    char error_message[128];    // Error message if read failed
} enip_scanner_motoman_torque_t;

/**
 * @brief Read torque from Motoman controller
 * 
 * Reads Class 0x77, Instance = control_group, Get_Attribute_All
 * Instance ranges:
 * - 1-8: Robot
 * - 11-18: Base
 * - 21-44: Station
 * 
 * @param ip_address Target robot IP address
 * @param control_group Control group number
 * @param torque Pointer to store torque result
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_read_torque(const ip4_addr_t *ip_address, uint16_t control_group,
                                           enip_scanner_motoman_torque_t *torque, uint32_t timeout_ms);

/**
 * @brief Read string-type variable (S) from Motoman controller
 * 
 * Reads Class 0x8C, Instance = variable_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable S number (0-based)
 * @param value Buffer to store variable value (must be at least 33 bytes for null terminator)
 * @param value_size Size of value buffer
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 * 
 * @note String variables are 32 bytes maximum
 * @note Instance = variable_number + 1 (when RS022=0, default)
 */
esp_err_t enip_scanner_motoman_read_variable_s(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               char *value, size_t value_size, uint32_t timeout_ms, char *error_message);

/**
 * @brief Write string-type variable (S) to Motoman controller
 * 
 * Writes Class 0x8C, Instance = variable_number + 1, Attribute 1
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable S number (0-based)
 * @param value Variable value to write (max 32 bytes)
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_write_variable_s(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 const char *value, uint32_t timeout_ms, char *error_message);

/**
 * @brief Read robot position-type variable (P) from Motoman controller
 * 
 * Reads Class 0x7F, Instance = variable_number + 1, Get_Attribute_All
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable P number (0-based)
 * @param position Pointer to store position result
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Instance = variable_number + 1 (when RS022=0, default)
 * @note Position structure includes: data type, configuration, tool number, user coordinate number, extended configuration, 8 axis data
 */
esp_err_t enip_scanner_motoman_read_variable_p(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               enip_scanner_motoman_position_t *position, uint32_t timeout_ms);

/**
 * @brief Write robot position-type variable (P) to Motoman controller
 * 
 * Writes Class 0x7F, Instance = variable_number + 1, Set_Attribute_All
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable P number (0-based)
 * @param position Position data to write
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_write_variable_p(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 const enip_scanner_motoman_position_t *position,
                                                 uint32_t timeout_ms, char *error_message);

/**
 * @brief Motoman base position structure
 * 
 * Base position data from Class 0x80 (Base position variables)
 * Reference: Motoman Manual 165838-1CD, Section 5.2.1
 */
typedef struct {
    ip4_addr_t ip_address;      // Robot IP address
    bool success;               // Read was successful
    uint32_t data_type;         // Position data type: 0=Pulse, 16=Base
    int32_t axis_data[8];       // Axis data (8 axes, 4 bytes each)
    char error_message[128];    // Error message if read failed
} enip_scanner_motoman_base_position_t;

/**
 * @brief Read base position-type variable (BP) from Motoman controller
 * 
 * Reads Class 0x80, Instance = variable_number + 1, Get_Attribute_All
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable BP number (0-based)
 * @param position Pointer to store base position result
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Instance = variable_number + 1 (when RS022=0, default)
 */
esp_err_t enip_scanner_motoman_read_variable_bp(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 enip_scanner_motoman_base_position_t *position, uint32_t timeout_ms);

/**
 * @brief Write base position-type variable (BP) to Motoman controller
 * 
 * Writes Class 0x80, Instance = variable_number + 1, Set_Attribute_All
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable BP number (0-based)
 * @param position Base position data to write
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_write_variable_bp(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                  const enip_scanner_motoman_base_position_t *position,
                                                  uint32_t timeout_ms, char *error_message);

/**
 * @brief Motoman external axis position structure
 * 
 * External axis position data from Class 0x81 (External axis position variables)
 * Reference: Motoman Manual 165838-1CD, Section 5.2.1
 */
typedef struct {
    ip4_addr_t ip_address;      // Robot IP address
    bool success;               // Read was successful
    uint32_t data_type;         // Position data type: 0=Pulse
    int32_t axis_data[8];       // Axis data (8 axes, 4 bytes each)
    char error_message[128];    // Error message if read failed
} enip_scanner_motoman_external_position_t;

/**
 * @brief Read external axis position-type variable (EX) from Motoman controller
 * 
 * Reads Class 0x81, Instance = variable_number + 1, Get_Attribute_All
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable EX number (0-based)
 * @param position Pointer to store external position result
 * @param timeout_ms Timeout for the operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Instance = variable_number + 1 (when RS022=0, default)
 */
esp_err_t enip_scanner_motoman_read_variable_ex(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 enip_scanner_motoman_external_position_t *position, uint32_t timeout_ms);

/**
 * @brief Write external axis position-type variable (EX) to Motoman controller
 * 
 * Writes Class 0x81, Instance = variable_number + 1, Set_Attribute_All
 * 
 * @param ip_address Target robot IP address
 * @param variable_number Variable EX number (0-based)
 * @param position External position data to write
 * @param timeout_ms Timeout for the operation in milliseconds
 * @param error_message Buffer to store error message (128 bytes, can be NULL)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_motoman_write_variable_ex(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 const enip_scanner_motoman_external_position_t *position,
                                                 uint32_t timeout_ms, char *error_message);

#endif // CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT

#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT

/**
 * @brief Callback function type for implicit messaging data reception
 * @param ip_address Source device IP address
 * @param assembly_instance Assembly instance number that produced the data
 * @param data Received assembly data
 * @param data_length Length of received data in bytes
 * @param user_data User-provided context pointer
 */
typedef void (*enip_implicit_data_callback_t)(
    const ip4_addr_t *ip_address,
    uint16_t assembly_instance,
    const uint8_t *data,
    uint16_t data_length,
    void *user_data
);

/**
 * @brief Open an implicit messaging connection (I/O data) to an EtherNet/IP device
 * 
 * Establishes a bidirectional implicit messaging connection using Forward Open.
 * Creates UDP tasks for O-to-T heartbeat and T-to-O data reception.
 * 
 * @param ip_address Target device IP address
 * @param assembly_instance_consumed Assembly instance for O-to-T data (e.g., 150)
 * @param assembly_instance_produced Assembly instance for T-to-O data (e.g., 100)
 * @param rpi_ms Requested Packet Interval in milliseconds (10-10000)
 * @param callback Callback function to receive T-to-O data
 * @param user_data User context pointer passed to callback
 * @param timeout_ms Timeout for Forward Open operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Only one implicit connection per IP address is supported
 * @note RPI should account for WiFi latency (recommended: 200ms minimum)
 * @note Connection uses UDP port 2222 for implicit I/O data
 * @note TCP port 44818 is used for Forward Open/Close
 */
esp_err_t enip_scanner_implicit_open(const ip4_addr_t *ip_address,
                                     uint16_t assembly_instance_consumed,
                                     uint16_t assembly_instance_produced,
                                     uint16_t assembly_data_size_consumed,  // O-to-T data size in bytes (e.g., 40)
                                     uint16_t assembly_data_size_produced,  // T-to-O data size in bytes (e.g., 72)
                                     uint32_t rpi_ms,
                                     enip_implicit_data_callback_t callback,
                                     void *user_data,
                                     uint32_t timeout_ms,
                                     bool exclusive_owner);  // true = PTP (Point-to-Point, exclusive owner), false = non-PTP (Multicast T-to-O, non-exclusive owner)

/**
 * @brief Close an implicit messaging connection
 * @param ip_address Target device IP address
 * @param timeout_ms Timeout for Forward Close operation in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t enip_scanner_implicit_close(const ip4_addr_t *ip_address, uint32_t timeout_ms);

/**
 * @brief Write data to O-to-T assembly instance (sent in heartbeat packets)
 * @param ip_address Target device IP address
 * @param data Data to write
 * @param data_length Length of data to write (must match assembly_data_size_consumed used in open)
 * @return ESP_OK on success, error code otherwise
 * 
 * @note Data will be sent in the next heartbeat packet
 * @note Data length must not exceed the assembly_data_size_consumed specified when opening the connection
 */
esp_err_t enip_scanner_implicit_write_data(const ip4_addr_t *ip_address,
                                          const uint8_t *data,
                                          uint16_t data_length);

/**
 * @brief Read the current O-to-T data that's being sent in heartbeat packets
 * @param ip_address Target device IP address
 * @param data Buffer to store the data (must be at least assembly_data_size_consumed bytes)
 * @param data_length Pointer to store the actual data length
 * @param max_length Maximum length of the data buffer
 * @return ESP_OK on success, error code otherwise
 * 
 * @note This reads the data that was last written via enip_scanner_implicit_write_data()
 * @note If no data has been written, returns zero-filled buffer
 */
esp_err_t enip_scanner_implicit_read_o_to_t_data(const ip4_addr_t *ip_address,
                                                  uint8_t *data,
                                                  uint16_t *data_length,
                                                  uint16_t max_length);

#endif // CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT

#ifdef __cplusplus
}
#endif

#endif // ENIP_SCANNER_H

# EtherNet/IP Scanner API Documentation

[![GitHub](https://img.shields.io/badge/GitHub-Repository-blue)](https://github.com/AGSweeney/ESP32_ENIPScanner)

Complete API reference for the EtherNet/IP Scanner component with detailed examples for all functions.

## Table of Contents

1. [Introduction](#introduction)
2. [Initialization](#initialization)
3. [Device Discovery](#device-discovery)
4. [Assembly Operations](#assembly-operations)
5. [Tag Operations](#tag-operations)
6. [Motoman Robot Operations](#motoman-robot-operations)
7. [Implicit Messaging Operations](#implicit-messaging-operations)
8. [Session Management](#session-management)
9. [Data Structures](#data-structures)
10. [Error Handling](#error-handling)
11. [Thread Safety](#thread-safety)
12. [Resource Management](#resource-management)
13. [Complete Examples](#complete-examples)

---

## Introduction

The EtherNet/IP Scanner component provides explicit and implicit messaging capabilities for communicating with EtherNet/IP devices over TCP/IP networks. It supports device discovery, assembly data read/write, tag-based communication for Micro800 series PLCs, and real-time Class 1 I/O data exchange.

**Protocol Support:**
- EtherNet/IP explicit messaging (TCP port 44818)
- EtherNet/IP implicit messaging (UDP port 2222, Class 1 I/O)
- CIP (Common Industrial Protocol) services
- UDP device discovery (List Identity)

**Supported Operations:**
- Device discovery via UDP broadcast
- Assembly data read/write
- Assembly instance discovery
- Tag read/write (Micro800 series)
- Implicit messaging (Class 1 I/O)
- Session management

**Architecture:**
- Modular design with separate modules for core protocol, tag operations, and data type handling
- Thread-safe with internal mutex protection
- Memory-safe with comprehensive resource cleanup

---

## Initialization

### `enip_scanner_init()`

Initialize the EtherNet/IP scanner component. Must be called once before using any other API functions.

**Prototype:**
```c
esp_err_t enip_scanner_init(void);
```

**Returns:**
- `ESP_OK` - Initialization successful
- `ESP_ERR_NO_MEM` - Failed to create mutex
- `ESP_FAIL` - Other failure

**Example:**
```c
#include "enip_scanner.h"

void app_main(void)
{
    // Initialize network first (WiFi/Ethernet)
    // ... network initialization code ...
    
    // Initialize the scanner
    esp_err_t ret = enip_scanner_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize scanner: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "EtherNet/IP Scanner initialized successfully");
}
```

**Important Notes:**
- Call after network initialization (after receiving IP address)
- Idempotent - safe to call multiple times (returns ESP_OK if already initialized)
- Thread-safe - can be called from any task
- Creates internal mutex for thread synchronization
- Must be called before any other API functions

---

## Device Discovery

### `enip_scanner_scan_devices()`

Scan the local network for EtherNet/IP devices using UDP broadcast List Identity requests.

**Prototype:**
```c
int enip_scanner_scan_devices(enip_scanner_device_info_t *devices, 
                               int max_devices, 
                               uint32_t timeout_ms);
```

**Parameters:**
- `devices` - Pre-allocated array to store device information
- `max_devices` - Maximum number of devices to scan (size of array)
- `timeout_ms` - Timeout for collecting responses (milliseconds)

**Returns:**
- Number of devices found (0 or more)
- Returns after timeout expires

**Example:**
```c
#include "enip_scanner.h"
#include "lwip/inet.h"

void scan_network(void)
{
    enip_scanner_device_info_t devices[32];
    int count = enip_scanner_scan_devices(devices, 32, 5000);
    
    ESP_LOGI(TAG, "Found %d device(s)", count);
    
    for (int i = 0; i < count; i++) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&devices[i].ip_address));
        
        ESP_LOGI(TAG, "Device %d:", i + 1);
        ESP_LOGI(TAG, "  IP Address: %s", ip_str);
        ESP_LOGI(TAG, "  Product Name: %s", devices[i].product_name);
        ESP_LOGI(TAG, "  Vendor ID: 0x%04X", devices[i].vendor_id);
        ESP_LOGI(TAG, "  Device Type: 0x%04X", devices[i].device_type);
        ESP_LOGI(TAG, "  Product Code: 0x%04X", devices[i].product_code);
        ESP_LOGI(TAG, "  Revision: %d.%d", 
                 devices[i].major_revision, devices[i].minor_revision);
        ESP_LOGI(TAG, "  Serial Number: 0x%08lX", 
                 (unsigned long)devices[i].serial_number);
        ESP_LOGI(TAG, "  Response Time: %lu ms", devices[i].response_time_ms);
        ESP_LOGI(TAG, "  Online: %s", devices[i].online ? "Yes" : "No");
    }
}
```

**Behavior:**
- Broadcasts UDP List Identity request to network broadcast address
- Collects responses from all devices on the network
- Automatically limits scan range to prevent excessive traffic
- Thread-safe - can be called concurrently
- Non-blocking - returns after timeout expires

**Performance Notes:**
- Typical scan time: 1-5 seconds depending on network size
- Recommended timeout: 3000-5000ms
- Maximum devices configurable via menuconfig

---

## Assembly Operations

### `enip_scanner_read_assembly()`

Read assembly data from an EtherNet/IP device using explicit messaging (Get_Attribute_Single CIP service).

**Prototype:**
```c
esp_err_t enip_scanner_read_assembly(const ip4_addr_t *ip_address, 
                                     uint16_t assembly_instance, 
                                     enip_scanner_assembly_result_t *result, 
                                     uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target device IP address
- `assembly_instance` - Assembly instance number (e.g., 100, 150, 20)
- `result` - Pointer to result structure (caller must free `result->data`)
- `timeout_ms` - Operation timeout (milliseconds)

**Returns:**
- `ESP_OK` - Operation completed (check `result->success`)
- `ESP_ERR_INVALID_ARG` - Invalid parameters
- `ESP_ERR_INVALID_STATE` - Scanner not initialized
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_FAIL` - General failure

**Example:**
```c
#include "enip_scanner.h"
#include "lwip/inet.h"

void read_assembly_example(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);

    enip_scanner_assembly_result_t result;
    memset(&result, 0, sizeof(result));
    
    esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);

    if (ret == ESP_OK && result.success) {
        ESP_LOGI(TAG, "Read %d bytes from assembly %d", 
                 result.data_length, result.assembly_instance);
        ESP_LOGI(TAG, "Response time: %lu ms", result.response_time_ms);
        
        // Access individual bytes
        if (result.data_length > 0) {
            uint8_t byte0 = result.data[0];
            bool bit0 = (byte0 & 0x01) != 0;  // Check bit 0
            bool bit1 = (byte0 & 0x02) != 0;  // Check bit 1
            
            ESP_LOGI(TAG, "Byte 0: 0x%02X (bit0=%d, bit1=%d)", byte0, bit0, bit1);
            
            // Print all bytes
            ESP_LOGI(TAG, "Data: ");
            for (uint16_t i = 0; i < result.data_length; i++) {
                ESP_LOGI(TAG, "  [%d] = 0x%02X", i, result.data[i]);
            }
        }
        
        // Always free the result
        enip_scanner_free_assembly_result(&result);
    } else {
        ESP_LOGE(TAG, "Read failed: %s", result.error_message);
        enip_scanner_free_assembly_result(&result);
    }
}
```

**Bit Manipulation:**
```c
uint8_t byte = result.data[0];

// Check if bit N is set (bits numbered 0-7, right to left)
bool bit_set = (byte & (1 << N)) != 0;

// Set bit N
byte |= (1 << N);

// Clear bit N
byte &= ~(1 << N);

// Toggle bit N
byte ^= (1 << N);

// Check multiple bits
bool bits_0_and_2_set = (byte & 0x05) == 0x05;  // Bits 0 and 2
```

**Memory Management:**
- `result->data` is allocated by the function
- **Always** call `enip_scanner_free_assembly_result()` to free memory
- Free even if `result->success` is false
- Thread-safe - can be called concurrently

### `enip_scanner_write_assembly()`

Write assembly data to an EtherNet/IP device using explicit messaging (Set_Attribute_Single CIP service).

**Prototype:**
```c
esp_err_t enip_scanner_write_assembly(const ip4_addr_t *ip_address,
                                      uint16_t assembly_instance,
                                      const uint8_t *data,
                                      uint16_t data_length,
                                      uint32_t timeout_ms,
                                      char *error_message);
```

**Parameters:**
- `ip_address` - Target device IP address
- `assembly_instance` - Assembly instance number
- `data` - Data buffer to write
- `data_length` - Length of data in bytes
- `timeout_ms` - Operation timeout (milliseconds)
- `error_message` - Buffer for error message (128 bytes, can be NULL)

**Returns:**
- `ESP_OK` - Write successful
- `ESP_ERR_INVALID_ARG` - Invalid parameters
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_FAIL` - General failure

**Example:**
```c
#include "enip_scanner.h"
#include "lwip/inet.h"

void write_assembly_example(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    // Set bit 2 in byte 0
    uint8_t output_data[4] = {0x04, 0x00, 0x00, 0x00};  // 0x04 = bit 2 set

    char error_msg[128];
    esp_err_t ret = enip_scanner_write_assembly(&device_ip, 150, output_data, 4, 5000, error_msg);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Write successful");
    } else {
        ESP_LOGE(TAG, "Write failed: %s", error_msg);
    }
}
```

**Bit Manipulation Example:**
```c
uint8_t output_data[4] = {0};

// Set bit 2
output_data[0] |= (1 << 2);  // Value becomes 0x04

// Set multiple bits
output_data[0] |= (1 << 0) | (1 << 2);  // Sets bits 0 and 2, value becomes 0x05

// Clear bit 1
output_data[0] &= ~(1 << 1);

// Set all bits in byte 0
output_data[0] = 0xFF;

// Clear all bits in byte 0
output_data[0] = 0x00;

char error_msg[128];
enip_scanner_write_assembly(&device_ip, 150, output_data, 4, 5000, error_msg);
```

**Important:**
- Not all assemblies are writable - check before writing
- Thread-safe - can be called concurrently
- All resources cleaned up on error
- Data is sent as-is (little-endian byte order)

### `enip_scanner_discover_assemblies()`

Discover valid assembly instances for a device by probing common instance numbers.

**Prototype:**
```c
int enip_scanner_discover_assemblies(const ip4_addr_t *ip_address, 
                                     uint16_t *instances, 
                                     int max_instances, 
                                     uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target device IP address
- `instances` - Pre-allocated array to store instance numbers
- `max_instances` - Maximum instances to discover (array size)
- `timeout_ms` - Timeout per probe (milliseconds)

**Returns:**
- Number of valid instances found

**Example:**
```c
#include "enip_scanner.h"
#include "lwip/inet.h"

void discover_assemblies_example(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);

    uint16_t instances[32];
    int count = enip_scanner_discover_assemblies(&device_ip, instances, 32, 2000);

    ESP_LOGI(TAG, "Found %d assembly instance(s)", count);
    for (int i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  Instance %d", instances[i]);
        
        // Check if writable
        bool writable = enip_scanner_is_assembly_writable(&device_ip, instances[i], 2000);
        ESP_LOGI(TAG, "    Writable: %s", writable ? "Yes" : "No");
    }
}
```

**Behavior:**
- Probes common assembly instance numbers (20, 100, 150, etc.)
- Returns only instances that respond successfully
- Thread-safe - can be called concurrently
- May take several seconds depending on timeout and number of instances

### `enip_scanner_is_assembly_writable()`

Check if an assembly instance is writable by attempting to read its configuration.

**Prototype:**
```c
bool enip_scanner_is_assembly_writable(const ip4_addr_t *ip_address, 
                                        uint16_t assembly_instance, 
                                        uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target device IP address
- `assembly_instance` - Assembly instance number
- `timeout_ms` - Timeout for the check (milliseconds)

**Returns:**
- `true` - Assembly is writable
- `false` - Assembly is read-only or doesn't exist

**Example:**
```c
#include "enip_scanner.h"
#include "lwip/inet.h"

void check_writable_example(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    uint16_t instance = 150;
    bool writable = enip_scanner_is_assembly_writable(&device_ip, instance, 2000);
    
    if (writable) {
        ESP_LOGI(TAG, "Assembly %d is writable", instance);
} else {
        ESP_LOGI(TAG, "Assembly %d is read-only or doesn't exist", instance);
    }
}
```

### `enip_scanner_free_assembly_result()`

Free memory allocated for assembly read result.

**Prototype:**
```c
void enip_scanner_free_assembly_result(enip_scanner_assembly_result_t *result);
```

**Parameters:**
- `result` - Pointer to result structure

**Example:**
```c
enip_scanner_assembly_result_t result;
memset(&result, 0, sizeof(result));

esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);

// Always free, even on error
if (ret == ESP_OK && result.success) {
    // Use result.data...
}

enip_scanner_free_assembly_result(&result);
```

**Important:**
- Always call this function after reading assembly data
- Safe to call multiple times (idempotent)
- Sets `result->data` to NULL after freeing

---

## Tag Operations

Tag operations are only available when `CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT` is enabled.

### API vs Web UI Support

**API Support (Complete):**
The API (`enip_scanner_read_tag()` and `enip_scanner_write_tag()`) supports **all 20 CIP data types**:
- BOOL, SINT, INT, DINT, LINT
- USINT, UINT, UDINT, ULINT
- REAL, LREAL
- TIME (STIME in CIP spec, called TIME on Micro800), DATE, TIME_OF_DAY, DATE_AND_TIME
- STRING
- BYTE, WORD, DWORD, LWORD

**Web UI Support (Limited):**
The web interface currently supports **6 data types** for tag writing:
- BOOL (0xC1)
- SINT (0xC2)
- INT (0xC3)
- DINT (0xC4)
- REAL (0xCA)
- STRING (0xDA)

To use the remaining 14 data types, you must use the API directly. See the complete examples below.

### `enip_scanner_read_tag()`

Read a tag from an Allen-Bradley device (Micro800, CompactLogix, etc.) using symbolic name.

**Prototype:**
```c
esp_err_t enip_scanner_read_tag(const ip4_addr_t *ip_address,
                                const char *tag_path,
                                enip_scanner_tag_result_t *result,
                                uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target device IP address
- `tag_path` - Tag name/path (e.g., "MyTag", "MyArray[0]")
- `result` - Pointer to store result (caller must free `result->data`)
- `timeout_ms` - Timeout for the operation (milliseconds)

**Returns:**
- `ESP_OK` - Operation completed (check `result->success`)
- `ESP_ERR_INVALID_ARG` - Invalid parameters
- `ESP_ERR_INVALID_STATE` - Scanner not initialized
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_FAIL` - General failure

**Example - Reading Different Data Types:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
#include "enip_scanner.h"
#include "lwip/inet.h"

void read_tag_examples(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    enip_scanner_tag_result_t result;
    memset(&result, 0, sizeof(result));
    
    // Read BOOL tag
    esp_err_t ret = enip_scanner_read_tag(&device_ip, "Output1", &result, 5000);
    if (ret == ESP_OK && result.success && result.cip_data_type == CIP_DATA_TYPE_BOOL) {
        bool value = result.data[0] != 0;
        ESP_LOGI(TAG, "Output1 = %s", value ? "true" : "false");
    }
    enip_scanner_free_tag_result(&result);
    
    // Read DINT tag
    memset(&result, 0, sizeof(result));
    ret = enip_scanner_read_tag(&device_ip, "Counter", &result, 5000);
    if (ret == ESP_OK && result.success && result.cip_data_type == CIP_DATA_TYPE_DINT) {
        int32_t value = (int32_t)(result.data[0] | 
                                  (result.data[1] << 8) | 
                                  (result.data[2] << 16) | 
                                  (result.data[3] << 24));
        ESP_LOGI(TAG, "Counter = %ld", value);
    }
    enip_scanner_free_tag_result(&result);
    
    // Read REAL tag
    memset(&result, 0, sizeof(result));
    ret = enip_scanner_read_tag(&device_ip, "Temperature", &result, 5000);
    if (ret == ESP_OK && result.success && result.cip_data_type == CIP_DATA_TYPE_REAL) {
        union {
            uint32_t u32;
            float f;
        } converter;
        converter.u32 = (uint32_t)(result.data[0] | 
                                  (result.data[1] << 8) | 
                                  (result.data[2] << 16) | 
                                  (result.data[3] << 24));
        ESP_LOGI(TAG, "Temperature = %.2f", converter.f);
    }
    enip_scanner_free_tag_result(&result);
    
    // Read STRING tag
    memset(&result, 0, sizeof(result));
    ret = enip_scanner_read_tag(&device_ip, "Message", &result, 5000);
    if (ret == ESP_OK && result.success && result.cip_data_type == CIP_DATA_TYPE_STRING) {
        // STRING format: [Length (1 byte)] [String bytes]
        if (result.data_length > 0) {
            uint8_t str_len = result.data[0];
            if (str_len > 0 && result.data_length > str_len) {
                char str_value[256];
                size_t copy_len = (str_len < sizeof(str_value) - 1) ? str_len : sizeof(str_value) - 1;
                memcpy(str_value, &result.data[1], copy_len);
                str_value[copy_len] = '\0';
                ESP_LOGI(TAG, "Message = \"%s\"", str_value);
            }
        }
    }
    enip_scanner_free_tag_result(&result);
}
#endif
```

**Tag Path Format:**
- `"MyTag"` - Simple tag name
- `"MyArray[0]"` - Array element (use bracket notation)
- `"MyStruct.Field"` - Structure field (use dot notation)
- Case-sensitive - must match exactly

**Important Notes:**
- Micro800 PLCs do not support program-scoped tags - tags must be in the global variable table
- Tag names are case-sensitive
- Always free result data using `enip_scanner_free_tag_result()`

### `enip_scanner_write_tag()`

Write a tag to an Allen-Bradley device (Micro800, CompactLogix, etc.) using symbolic name.

**Prototype:**
```c
esp_err_t enip_scanner_write_tag(const ip4_addr_t *ip_address,
                                 const char *tag_path,
                                 const uint8_t *data,
                                 uint16_t data_length,
                                 uint16_t cip_data_type,
                                 uint32_t timeout_ms,
                                 char *error_message);
```

**Parameters:**
- `ip_address` - Target device IP address
- `tag_path` - Tag name/path (e.g., "MyTag", "MyArray[0]")
- `data` - Data buffer to write
- `data_length` - Length of data in bytes
- `cip_data_type` - CIP data type code (e.g., `CIP_DATA_TYPE_DINT`)
- `timeout_ms` - Timeout for the operation (milliseconds)
- `error_message` - Buffer for error message (128 bytes, can be NULL)

**Returns:**
- `ESP_OK` - Write successful
- `ESP_ERR_INVALID_ARG` - Invalid parameters
- `ESP_ERR_INVALID_STATE` - Scanner not initialized
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_FAIL` - General failure

**Example - Writing Different Data Types:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
#include "enip_scanner.h"
#include "lwip/inet.h"
#include <string.h>

void write_tag_examples(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    char error_msg[128];
    
    // Write BOOL tag
    uint8_t bool_value = 1;  // true
    esp_err_t ret = enip_scanner_write_tag(&device_ip, "Output1", &bool_value, 1, 
                                           CIP_DATA_TYPE_BOOL, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "BOOL write successful");
    } else {
        ESP_LOGE(TAG, "BOOL write failed: %s", error_msg);
    }
    
    // Write DINT tag
    int32_t dint_value = 12345;
    uint8_t dint_bytes[4];
    dint_bytes[0] = dint_value & 0xFF;
    dint_bytes[1] = (dint_value >> 8) & 0xFF;
    dint_bytes[2] = (dint_value >> 16) & 0xFF;
    dint_bytes[3] = (dint_value >> 24) & 0xFF;
    
    ret = enip_scanner_write_tag(&device_ip, "Setpoint", dint_bytes, 4, 
                                 CIP_DATA_TYPE_DINT, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "DINT write successful");
    } else {
        ESP_LOGE(TAG, "DINT write failed: %s", error_msg);
    }
    
    // Write REAL tag
    float real_value = 123.45f;
    union {
        uint32_t u32;
        float f;
    } converter;
    converter.f = real_value;
    uint8_t real_bytes[4];
    real_bytes[0] = converter.u32 & 0xFF;
    real_bytes[1] = (converter.u32 >> 8) & 0xFF;
    real_bytes[2] = (converter.u32 >> 16) & 0xFF;
    real_bytes[3] = (converter.u32 >> 24) & 0xFF;
    
    ret = enip_scanner_write_tag(&device_ip, "Temperature", real_bytes, 4, 
                                 CIP_DATA_TYPE_REAL, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "REAL write successful");
    } else {
        ESP_LOGE(TAG, "REAL write failed: %s", error_msg);
    }
    
    // Write STRING tag
    const char *str_value = "Hello, PLC!";
    uint8_t str_bytes[256];
    size_t str_len = strlen(str_value);
    if (str_len > 255) str_len = 255;  // Enforce max length
    memcpy(str_bytes, str_value, str_len);  // Copy string bytes (no null terminator)
    
    ret = enip_scanner_write_tag(&device_ip, "Message", str_bytes, str_len, 
                                 CIP_DATA_TYPE_STRING, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "STRING write successful");
    } else {
        ESP_LOGE(TAG, "STRING write failed: %s", error_msg);
    }
}
#endif
```

**STRING Data Format:**
- STRING tags use a 1-byte length prefix format: `[Length (1 byte)] [String bytes]`
- Maximum length: 255 characters (due to 1-byte length prefix)
- Length prefix is handled automatically by the encoder
- Provide only the string bytes (no null terminator, no length prefix)

**Complete Data Type Examples:**

The API supports all 20 CIP data types. Here are examples for each:

```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
#include "enip_scanner.h"
#include "lwip/inet.h"
#include <string.h>
#include <stdint.h>

void write_all_data_types_example(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    char error_msg[128];
    
    // BOOL (1 byte)
    uint8_t bool_val = 1;
    enip_scanner_write_tag(&device_ip, "Output1", &bool_val, 1, 
                          CIP_DATA_TYPE_BOOL, 5000, error_msg);
    
    // SINT (1 byte, signed)
    int8_t sint_val = -100;
    uint8_t sint_bytes[1] = {(uint8_t)sint_val};
    enip_scanner_write_tag(&device_ip, "SIntTag", sint_bytes, 1, 
                          CIP_DATA_TYPE_SINT, 5000, error_msg);
    
    // INT (2 bytes, signed)
    int16_t int_val = -12345;
    uint8_t int_bytes[2];
    int_bytes[0] = int_val & 0xFF;
    int_bytes[1] = (int_val >> 8) & 0xFF;
    enip_scanner_write_tag(&device_ip, "IntTag", int_bytes, 2, 
                          CIP_DATA_TYPE_INT, 5000, error_msg);
    
    // DINT (4 bytes, signed)
    int32_t dint_val = -123456789;
    uint8_t dint_bytes[4];
    dint_bytes[0] = dint_val & 0xFF;
    dint_bytes[1] = (dint_val >> 8) & 0xFF;
    dint_bytes[2] = (dint_val >> 16) & 0xFF;
    dint_bytes[3] = (dint_val >> 24) & 0xFF;
    enip_scanner_write_tag(&device_ip, "DIntTag", dint_bytes, 4, 
                          CIP_DATA_TYPE_DINT, 5000, error_msg);
    
    // LINT (8 bytes, signed)
    int64_t lint_val = -123456789012345LL;
    uint8_t lint_bytes[8];
    for (int i = 0; i < 8; i++) {
        lint_bytes[i] = (lint_val >> (i * 8)) & 0xFF;
    }
    enip_scanner_write_tag(&device_ip, "LIntTag", lint_bytes, 8, 
                          CIP_DATA_TYPE_LINT, 5000, error_msg);
    
    // USINT (1 byte, unsigned)
    uint8_t usint_val = 200;
    enip_scanner_write_tag(&device_ip, "USIntTag", &usint_val, 1, 
                          CIP_DATA_TYPE_USINT, 5000, error_msg);
    
    // UINT (2 bytes, unsigned)
    uint16_t uint_val = 50000;
    uint8_t uint_bytes[2];
    uint_bytes[0] = uint_val & 0xFF;
    uint_bytes[1] = (uint_val >> 8) & 0xFF;
    enip_scanner_write_tag(&device_ip, "UIntTag", uint_bytes, 2, 
                          CIP_DATA_TYPE_UINT, 5000, error_msg);
    
    // UDINT (4 bytes, unsigned)
    uint32_t udint_val = 4000000000UL;
    uint8_t udint_bytes[4];
    udint_bytes[0] = udint_val & 0xFF;
    udint_bytes[1] = (udint_val >> 8) & 0xFF;
    udint_bytes[2] = (udint_val >> 16) & 0xFF;
    udint_bytes[3] = (udint_val >> 24) & 0xFF;
    enip_scanner_write_tag(&device_ip, "UDIntTag", udint_bytes, 4, 
                          CIP_DATA_TYPE_UDINT, 5000, error_msg);
    
    // ULINT (8 bytes, unsigned)
    uint64_t ulint_val = 9000000000000000000ULL;
    uint8_t ulint_bytes[8];
    for (int i = 0; i < 8; i++) {
        ulint_bytes[i] = (ulint_val >> (i * 8)) & 0xFF;
    }
    enip_scanner_write_tag(&device_ip, "ULIntTag", ulint_bytes, 8, 
                          CIP_DATA_TYPE_ULINT, 5000, error_msg);
    
    // REAL (4 bytes, IEEE 754 float)
    float real_val = 123.456f;
    union { uint32_t u32; float f; } real_conv;
    real_conv.f = real_val;
    uint8_t real_bytes[4];
    real_bytes[0] = real_conv.u32 & 0xFF;
    real_bytes[1] = (real_conv.u32 >> 8) & 0xFF;
    real_bytes[2] = (real_conv.u32 >> 16) & 0xFF;
    real_bytes[3] = (real_conv.u32 >> 24) & 0xFF;
    enip_scanner_write_tag(&device_ip, "RealTag", real_bytes, 4, 
                          CIP_DATA_TYPE_REAL, 5000, error_msg);
    
    // LREAL (8 bytes, IEEE 754 double)
    double lreal_val = 123456.789012;
    union { uint64_t u64; double d; } lreal_conv;
    lreal_conv.d = lreal_val;
    uint8_t lreal_bytes[8];
    for (int i = 0; i < 8; i++) {
        lreal_bytes[i] = (lreal_conv.u64 >> (i * 8)) & 0xFF;
    }
    enip_scanner_write_tag(&device_ip, "LRealTag", lreal_bytes, 8, 
                          CIP_DATA_TYPE_LREAL, 5000, error_msg);
    
    // TIME (4 bytes, milliseconds) - Note: Called "TIME" on Micro800, "STIME" in CIP spec
    uint32_t time_val = 5000;  // 5 seconds
    uint8_t time_bytes[4];
    time_bytes[0] = time_val & 0xFF;
    time_bytes[1] = (time_val >> 8) & 0xFF;
    time_bytes[2] = (time_val >> 16) & 0xFF;
    time_bytes[3] = (time_val >> 24) & 0xFF;
    enip_scanner_write_tag(&device_ip, "TimeTag", time_bytes, 4, 
                          CIP_DATA_TYPE_STIME, 5000, error_msg);
    
    // DATE (2 bytes, days since 1970-01-01)
    uint16_t date_val = 20000;  // Example date
    uint8_t date_bytes[2];
    date_bytes[0] = date_val & 0xFF;
    date_bytes[1] = (date_val >> 8) & 0xFF;
    enip_scanner_write_tag(&device_ip, "DateTag", date_bytes, 2, 
                          CIP_DATA_TYPE_DATE, 5000, error_msg);
    
    // TIME_OF_DAY (4 bytes, milliseconds since midnight)
    uint32_t tod_val = 43200000;  // Noon (12:00:00)
    uint8_t tod_bytes[4];
    tod_bytes[0] = tod_val & 0xFF;
    tod_bytes[1] = (tod_val >> 8) & 0xFF;
    tod_bytes[2] = (tod_val >> 16) & 0xFF;
    tod_bytes[3] = (tod_val >> 24) & 0xFF;
    enip_scanner_write_tag(&device_ip, "TODTag", tod_bytes, 4, 
                          CIP_DATA_TYPE_TIME_OF_DAY, 5000, error_msg);
    
    // DATE_AND_TIME (8 bytes, combined date/time)
    uint64_t dt_val = 0x1234567890ABCDEFULL;  // Example value
    uint8_t dt_bytes[8];
    for (int i = 0; i < 8; i++) {
        dt_bytes[i] = (dt_val >> (i * 8)) & 0xFF;
    }
    enip_scanner_write_tag(&device_ip, "DateTimeTag", dt_bytes, 8, 
                          CIP_DATA_TYPE_DATE_AND_TIME, 5000, error_msg);
    
    // STRING (variable length, max 255 chars)
    const char *str_val = "Hello, PLC!";
    uint8_t str_bytes[256];
    size_t str_len = strlen(str_val);
    if (str_len > 255) str_len = 255;
    memcpy(str_bytes, str_val, str_len);
    enip_scanner_write_tag(&device_ip, "StringTag", str_bytes, str_len, 
                          CIP_DATA_TYPE_STRING, 5000, error_msg);
    
    // BYTE (1 byte, bit string)
    uint8_t byte_val = 0xAA;  // 10101010
    enip_scanner_write_tag(&device_ip, "ByteTag", &byte_val, 1, 
                          CIP_DATA_TYPE_BYTE, 5000, error_msg);
    
    // WORD (2 bytes, bit string)
    uint16_t word_val = 0xAABB;
    uint8_t word_bytes[2];
    word_bytes[0] = word_val & 0xFF;
    word_bytes[1] = (word_val >> 8) & 0xFF;
    enip_scanner_write_tag(&device_ip, "WordTag", word_bytes, 2, 
                          CIP_DATA_TYPE_WORD, 5000, error_msg);
    
    // DWORD (4 bytes, bit string)
    uint32_t dword_val = 0xAABBCCDD;
    uint8_t dword_bytes[4];
    dword_bytes[0] = dword_val & 0xFF;
    dword_bytes[1] = (dword_val >> 8) & 0xFF;
    dword_bytes[2] = (dword_val >> 16) & 0xFF;
    dword_bytes[3] = (dword_val >> 24) & 0xFF;
    enip_scanner_write_tag(&device_ip, "DWordTag", dword_bytes, 4, 
                          CIP_DATA_TYPE_DWORD, 5000, error_msg);
    
    // LWORD (8 bytes, bit string)
    uint64_t lword_val = 0xAABBCCDD11223344ULL;
    uint8_t lword_bytes[8];
    for (int i = 0; i < 8; i++) {
        lword_bytes[i] = (lword_val >> (i * 8)) & 0xFF;
    }
    enip_scanner_write_tag(&device_ip, "LWordTag", lword_bytes, 8, 
                          CIP_DATA_TYPE_LWORD, 5000, error_msg);
}
#endif
```

**Important Notes:**
- Data type must match the tag's actual data type in the PLC
- Not all tags are writable - check PLC configuration
- Tag names are case-sensitive
- Micro800 PLCs do not support program-scoped tags
- **API vs Web UI**: The API supports all 20 data types shown above. The web UI currently supports only 6 types (BOOL, SINT, INT, DINT, REAL, STRING). Use the API for the remaining 14 types.

### `enip_scanner_free_tag_result()`

Free memory allocated for tag read result.

**Prototype:**
```c
void enip_scanner_free_tag_result(enip_scanner_tag_result_t *result);
```

**Parameters:**
- `result` - Pointer to tag result structure

**Example:**
```c
enip_scanner_tag_result_t result;
memset(&result, 0, sizeof(result));

esp_err_t ret = enip_scanner_read_tag(&device_ip, "MyTag", &result, 5000);

// Always free, even on error
if (ret == ESP_OK && result.success) {
    // Use result.data...
}

enip_scanner_free_tag_result(&result);
```

**Important:**
- Always call this function after reading tag data
- Safe to call multiple times (idempotent)
- Sets `result->data` to NULL after freeing

### `enip_scanner_get_data_type_name()`

Get human-readable name for CIP data type code.

**Prototype:**
```c
const char *enip_scanner_get_data_type_name(uint16_t cip_data_type);
```

---

## Motoman Robot Operations

Motoman robot operations are only available when `CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT` is enabled.

The component provides high-level APIs for interacting with Motoman DX200/YRC1000 robot controllers via vendor-specific CIP classes. These functions abstract the low-level CIP message construction and provide easy-to-use interfaces for common robot operations.

**Implementation Status**: **All 18 Motoman CIP classes** are now implemented. See [MOTOMAN_CIP_CLASSES.md](MOTOMAN_CIP_CLASSES.md) for the complete list of available classes.

**Reference**: Motoman Manual 165838-1CD, Section 5.2 "Message Communication Using CIP"

### `enip_scanner_motoman_read_status()`

Read robot status from Motoman controller using CIP Class 0x72.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_status(const ip4_addr_t *ip_address,
                                           enip_scanner_motoman_status_t *status,
                                           uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `status` - Pointer to status structure (populated on success)
- `timeout_ms` - Timeout for the operation in milliseconds

**Returns:**
- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_ERR_INVALID_STATE` - Scanner not initialized
- `ESP_FAIL` - Operation failed (check `status->error_message`)

**Status Bits (Data 1):**
- Bit 0: Step
- Bit 1: 1 cycle
- Bit 2: Auto
- Bit 3: Running
- Bit 4: Safety speed operation
- Bit 5: Teach
- Bit 6: Play
- Bit 7: Command remote

**Status Bits (Data 2):**
- Bit 1: Hold (Programming pendant)
- Bit 2: Hold (external)
- Bit 3: Hold (Command)
- Bit 4: Alarm
- Bit 5: Error
- Bit 6: Servo on

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
#include "lwip/inet.h"

void read_robot_status_example(void)
{
    ip4_addr_t robot_ip;
    inet_aton("192.168.1.200", &robot_ip);
    
    enip_scanner_motoman_status_t status;
    memset(&status, 0, sizeof(status));
    
    esp_err_t ret = enip_scanner_motoman_read_status(&robot_ip, &status, 5000);
    if (ret == ESP_OK && status.success) {
        // Parse status bits
        bool running = (status.data1 & 0x08) != 0;  // Bit 3: Running
        bool error = (status.data2 & 0x20) != 0;     // Bit 5: Error
        bool servo_on = (status.data2 & 0x40) != 0;  // Bit 6: Servo on
        
        ESP_LOGI(TAG, "Robot Status:");
        ESP_LOGI(TAG, "  Running: %s", running ? "Yes" : "No");
        ESP_LOGI(TAG, "  Error: %s", error ? "Yes" : "No");
        ESP_LOGI(TAG, "  Servo On: %s", servo_on ? "Yes" : "No");
        ESP_LOGI(TAG, "  Response time: %lu ms", status.response_time_ms);
    } else {
        ESP_LOGE(TAG, "Failed to read status: %s", status.error_message);
    }
}
#endif
```

### `enip_scanner_motoman_read_io()` / `enip_scanner_motoman_write_io()`

Read or write I/O data from/to Motoman controller using CIP Class 0x78.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_io(const ip4_addr_t *ip_address, uint16_t signal_number,
                                       uint8_t *value, uint32_t timeout_ms, char *error_message);

esp_err_t enip_scanner_motoman_write_io(const ip4_addr_t *ip_address, uint16_t signal_number,
                                        uint8_t value, uint32_t timeout_ms, char *error_message);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `signal_number` - Signal number (see ranges below)
- `value` - Pointer to store value (read) or value to write (write)
- `timeout_ms` - Timeout for the operation in milliseconds
- `error_message` - Buffer for error message (128 bytes, can be NULL)

**Signal Number Ranges:**
- 1-256: General input
- 1001-1256: General output
- 2001-2256: External input
- 2501-2756: Network input (writable)
- 3001-3256: External output
- 3501-3756: Network output
- 4001-4160: Specific input
- 5001-5200: Specific output
- 6001-6064: Interface panel input
- 7001-7999: Auxiliary relay
- 8001-8064: Control status
- 8201-8220: Pseudo input

**Note:** Instance = signal_number / 10 (per Motoman manual)

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
void read_write_io_example(void)
{
    ip4_addr_t robot_ip;
    inet_aton("192.168.1.200", &robot_ip);
    char error_msg[128];
    
    // Read General Input 1
    uint8_t input_value = 0;
    esp_err_t ret = enip_scanner_motoman_read_io(&robot_ip, 1, &input_value, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "General Input 1: %d", input_value);
    } else {
        ESP_LOGE(TAG, "Read failed: %s", error_msg);
    }
    
    // Write General Output 1001
    uint8_t output_value = 1;
    ret = enip_scanner_motoman_write_io(&robot_ip, 1001, output_value, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "General Output 1001 written");
    } else {
        ESP_LOGE(TAG, "Write failed: %s", error_msg);
    }
}
#endif
```

### Variable Read/Write Functions

The component provides functions for reading and writing robot variables of different types:

**Byte Variables (B):**
```c
esp_err_t enip_scanner_motoman_read_variable_b(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               uint8_t *value, uint32_t timeout_ms, char *error_message);
esp_err_t enip_scanner_motoman_write_variable_b(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 uint8_t value, uint32_t timeout_ms, char *error_message);
```

**Integer Variables (I):**
```c
esp_err_t enip_scanner_motoman_read_variable_i(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               int16_t *value, uint32_t timeout_ms, char *error_message);
esp_err_t enip_scanner_motoman_write_variable_i(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 int16_t value, uint32_t timeout_ms, char *error_message);
```

**Double Integer Variables (D):**
```c
esp_err_t enip_scanner_motoman_read_variable_d(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               int32_t *value, uint32_t timeout_ms, char *error_message);
esp_err_t enip_scanner_motoman_write_variable_d(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 int32_t value, uint32_t timeout_ms, char *error_message);
```

**Real Variables (R):**
```c
esp_err_t enip_scanner_motoman_read_variable_r(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               float *value, uint32_t timeout_ms, char *error_message);
esp_err_t enip_scanner_motoman_write_variable_r(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 float value, uint32_t timeout_ms, char *error_message);
```

**Note:** Instance = variable_number + 1 (when RS022=0, default). If RS022=1, instance = variable_number.

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
void read_write_variables_example(void)
{
    ip4_addr_t robot_ip;
    inet_aton("192.168.1.200", &robot_ip);
    char error_msg[128];
    
    // Read Real variable R[0]
    float r_value = 0.0f;
    esp_err_t ret = enip_scanner_motoman_read_variable_r(&robot_ip, 0, &r_value, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "R[0] = %.2f", r_value);
    }
    
    // Write Integer variable I[0]
    int16_t i_value = 100;
    ret = enip_scanner_motoman_write_variable_i(&robot_ip, 0, i_value, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I[0] written: %d", i_value);
    }
}
#endif
```

### `enip_scanner_motoman_read_register()` / `enip_scanner_motoman_write_register()`

Read or write register data from/to Motoman controller using CIP Class 0x79.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_register(const ip4_addr_t *ip_address, uint16_t register_number,
                                             uint16_t *value, uint32_t timeout_ms, char *error_message);

esp_err_t enip_scanner_motoman_write_register(const ip4_addr_t *ip_address, uint16_t register_number,
                                              uint16_t value, uint32_t timeout_ms, char *error_message);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `register_number` - Register number (0-999)
- `value` - Pointer to store value (read) or value to write (write)
- `timeout_ms` - Timeout for the operation in milliseconds
- `error_message` - Buffer for error message (128 bytes, can be NULL)

**Note:** Instance = register_number + 1 (when RS022=0, default)

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
void read_write_register_example(void)
{
    ip4_addr_t robot_ip;
    inet_aton("192.168.1.200", &robot_ip);
    char error_msg[128];
    
    // Read register 0
    uint16_t reg_value = 0;
    esp_err_t ret = enip_scanner_motoman_read_register(&robot_ip, 0, &reg_value, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Register 0: %d", reg_value);
    }
    
    // Write register 0
    uint16_t new_value = 1234;
    ret = enip_scanner_motoman_write_register(&robot_ip, 0, new_value, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Register 0 written: %d", new_value);
    }
}
#endif
```

### `enip_scanner_motoman_read_alarm()` / `enip_scanner_motoman_read_alarm_history()`

Read current alarm or alarm history from Motoman controller.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_alarm(const ip4_addr_t *ip_address, uint8_t alarm_instance,
                                         enip_scanner_motoman_alarm_t *alarm, uint32_t timeout_ms);
esp_err_t enip_scanner_motoman_read_alarm_history(const ip4_addr_t *ip_address, uint16_t alarm_instance,
                                                  enip_scanner_motoman_alarm_t *alarm, uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `alarm_instance` - For current alarm: 1-4 (1=Latest, 2=Before 1, 3=Before 2, 4=Before 3)
  - For history: 1-100 (Major), 1001-1100 (Minor), 2001-2100 (User System), 3001-3100 (User User), 4001-4100 (Off-line)
- `alarm` - Pointer to alarm structure (populated on success)
- `timeout_ms` - Timeout for the operation in milliseconds

**Returns:**
- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_FAIL` - Operation failed (check `alarm->error_message`)

**Alarm Structure:**
```c
typedef struct {
    ip4_addr_t ip_address;
    bool success;
    uint32_t alarm_code;        // Alarm code (0-9999)
    uint32_t alarm_data;        // Alarm data
    uint32_t alarm_data_type;   // Alarm data type (0-10)
    char alarm_date_time[17];   // Date/time string ("2010/10/10 10:10")
    char alarm_string[33];      // Alarm name string
    char error_message[128];
} enip_scanner_motoman_alarm_t;
```

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
enip_scanner_motoman_alarm_t alarm;
memset(&alarm, 0, sizeof(alarm));

esp_err_t ret = enip_scanner_motoman_read_alarm(&robot_ip, 1, &alarm, 5000);
if (ret == ESP_OK && alarm.success) {
    ESP_LOGI(TAG, "Alarm Code: %lu", alarm.alarm_code);
    ESP_LOGI(TAG, "Alarm: %s", alarm.alarm_string);
    ESP_LOGI(TAG, "Date/Time: %s", alarm.alarm_date_time);
}
#endif
```

### `enip_scanner_motoman_read_job_info()`

Read active job information from Motoman controller.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_job_info(const ip4_addr_t *ip_address,
                                            enip_scanner_motoman_job_info_t *job_info,
                                            uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `job_info` - Pointer to job info structure (populated on success)
- `timeout_ms` - Timeout for the operation in milliseconds

**Returns:**
- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_FAIL` - Operation failed (check `job_info->error_message`)

**Job Info Structure:**
```c
typedef struct {
    ip4_addr_t ip_address;
    bool success;
    char job_name[33];          // Job name (32 bytes)
    uint32_t line_number;       // Line number (0-9999)
    uint32_t step_number;       // Step number (1-9998)
    uint32_t speed_override;    // Speed override (unit: 0.01%)
    char error_message[128];
} enip_scanner_motoman_job_info_t;
```

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
enip_scanner_motoman_job_info_t job_info;
memset(&job_info, 0, sizeof(job_info));

esp_err_t ret = enip_scanner_motoman_read_job_info(&robot_ip, &job_info, 5000);
if (ret == ESP_OK && job_info.success) {
    ESP_LOGI(TAG, "Job: %s", job_info.job_name);
    ESP_LOGI(TAG, "Line: %lu, Step: %lu", job_info.line_number, job_info.step_number);
    ESP_LOGI(TAG, "Speed Override: %.2f%%", job_info.speed_override / 100.0f);
}
#endif
```

### `enip_scanner_motoman_read_axis_config()`

Read axis configuration from Motoman controller.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_axis_config(const ip4_addr_t *ip_address, uint16_t control_group,
                                               enip_scanner_motoman_axis_config_t *config, uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `control_group` - Control group (1-8: Robot pulse, 11-18: Base pulse, 21-44: Station pulse, 101-108: Robot coordinate, 111-118: Base linear)
- `config` - Pointer to axis config structure (populated on success)
- `timeout_ms` - Timeout for the operation in milliseconds

**Returns:**
- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_FAIL` - Operation failed (check `config->error_message`)

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
enip_scanner_motoman_axis_config_t config;
memset(&config, 0, sizeof(config));

esp_err_t ret = enip_scanner_motoman_read_axis_config(&robot_ip, 1, &config, 5000);
if (ret == ESP_OK && config.success) {
    for (int i = 0; i < 8; i++) {
        ESP_LOGI(TAG, "Axis %d: %s", i+1, config.axis_names[i]);
    }
}
#endif
```

### `enip_scanner_motoman_read_position()`

Read current robot position from Motoman controller.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_position(const ip4_addr_t *ip_address, uint16_t control_group,
                                             enip_scanner_motoman_position_t *position, uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `control_group` - Control group (1-8: Robot Pulse, 11-18: Base Pulse, 21-44: Station Pulse, 101-108: Robot Base)
- `position` - Pointer to position structure (populated on success)
- `timeout_ms` - Timeout for the operation in milliseconds

**Returns:**
- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_FAIL` - Operation failed (check `position->error_message`)

**Position Structure:**
```c
typedef struct {
    ip4_addr_t ip_address;
    bool success;
    uint32_t data_type;         // 0=Pulse, 16=Base
    uint32_t configuration;     // Configuration bits
    uint32_t tool_number;       // Tool number
    uint32_t reservation;       // Reservation
    uint32_t extended_configuration; // Extended configuration (7-axis)
    int32_t axis_data[8];       // 8 axis data values
    char error_message[128];
} enip_scanner_motoman_position_t;
```

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
enip_scanner_motoman_position_t position;
memset(&position, 0, sizeof(position));

esp_err_t ret = enip_scanner_motoman_read_position(&robot_ip, 1, &position, 5000);
if (ret == ESP_OK && position.success) {
    ESP_LOGI(TAG, "Data Type: %lu", position.data_type);
    ESP_LOGI(TAG, "Tool Number: %lu", position.tool_number);
    for (int i = 0; i < 8; i++) {
        ESP_LOGI(TAG, "Axis %d: %ld", i+1, position.axis_data[i]);
    }
}
#endif
```

### `enip_scanner_motoman_read_position_deviation()`

Read position deviation of each axis from Motoman controller.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_position_deviation(const ip4_addr_t *ip_address, uint16_t control_group,
                                                      enip_scanner_motoman_position_deviation_t *deviation,
                                                      uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `control_group` - Control group (1-8: Robot, 11-18: Base, 21-44: Station)
- `deviation` - Pointer to deviation structure (populated on success)
- `timeout_ms` - Timeout for the operation in milliseconds

**Returns:**
- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_FAIL` - Operation failed (check `deviation->error_message`)

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
enip_scanner_motoman_position_deviation_t deviation;
memset(&deviation, 0, sizeof(deviation));

esp_err_t ret = enip_scanner_motoman_read_position_deviation(&robot_ip, 1, &deviation, 5000);
if (ret == ESP_OK && deviation.success) {
    for (int i = 0; i < 8; i++) {
        ESP_LOGI(TAG, "Axis %d Deviation: %ld pulses", i+1, deviation.axis_deviation[i]);
    }
}
#endif
```

### `enip_scanner_motoman_read_torque()`

Read torque of each axis from Motoman controller.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_torque(const ip4_addr_t *ip_address, uint16_t control_group,
                                           enip_scanner_motoman_torque_t *torque, uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `control_group` - Control group (1-8: Robot, 11-18: Base, 21-44: Station)
- `torque` - Pointer to torque structure (populated on success)
- `timeout_ms` - Timeout for the operation in milliseconds

**Returns:**
- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_FAIL` - Operation failed (check `torque->error_message`)

**Note:** Torque values are percentages when nominal value is 100%.

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
enip_scanner_motoman_torque_t torque;
memset(&torque, 0, sizeof(torque));

esp_err_t ret = enip_scanner_motoman_read_torque(&robot_ip, 1, &torque, 5000);
if (ret == ESP_OK && torque.success) {
    for (int i = 0; i < 8; i++) {
        ESP_LOGI(TAG, "Axis %d Torque: %ld%%", i+1, torque.axis_torque[i]);
    }
}
#endif
```

### `enip_scanner_motoman_read_variable_s()` / `enip_scanner_motoman_write_variable_s()`

Read/write string-type variable (S) from/to Motoman controller.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_variable_s(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               char *value, size_t value_size, uint32_t timeout_ms, char *error_message);
esp_err_t enip_scanner_motoman_write_variable_s(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 const char *value, uint32_t timeout_ms, char *error_message);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `variable_number` - Variable S number (0-based)
- `value` - Buffer to store/contain string value (max 32 bytes for read, max 32 bytes for write)
- `value_size` - Size of value buffer (for read only)
- `timeout_ms` - Timeout for the operation in milliseconds
- `error_message` - Buffer to store error message (128 bytes, can be NULL)

**Returns:**
- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_FAIL` - Operation failed (check `error_message`)

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
char str_value[33];
char error_msg[128];

// Read string variable S[0]
esp_err_t ret = enip_scanner_motoman_read_variable_s(&robot_ip, 0, str_value, sizeof(str_value), 5000, error_msg);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "S[0] = %s", str_value);
}

// Write string variable S[0]
ret = enip_scanner_motoman_write_variable_s(&robot_ip, 0, "Hello Robot", 5000, error_msg);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "S[0] written successfully");
}
#endif
```

### `enip_scanner_motoman_read_variable_p()` / `enip_scanner_motoman_write_variable_p()`

Read/write robot position-type variable (P) from/to Motoman controller.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_variable_p(const ip4_addr_t *ip_address, uint16_t variable_number,
                                               enip_scanner_motoman_position_t *position, uint32_t timeout_ms);
esp_err_t enip_scanner_motoman_write_variable_p(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 const enip_scanner_motoman_position_t *position,
                                                 uint32_t timeout_ms, char *error_message);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `variable_number` - Variable P number (0-based)
- `position` - Pointer to position structure (read: populated on success, write: data to write)
- `timeout_ms` - Timeout for the operation in milliseconds
- `error_message` - Buffer to store error message (128 bytes, can be NULL, write only)

**Returns:**
- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_FAIL` - Operation failed (check `position->error_message` or `error_message`)

**Note:** Position structure includes data type, configuration, tool number, user coordinate number, extended configuration, and 8 axis data.

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
enip_scanner_motoman_position_t pos_var;
memset(&pos_var, 0, sizeof(pos_var));

// Read position variable P[0]
esp_err_t ret = enip_scanner_motoman_read_variable_p(&robot_ip, 0, &pos_var, 5000);
if (ret == ESP_OK && pos_var.success) {
    ESP_LOGI(TAG, "P[0] Data Type: %lu", pos_var.data_type);
    ESP_LOGI(TAG, "P[0] Tool: %lu", pos_var.tool_number);
}

// Write position variable P[0]
pos_var.data_type = 0;  // Pulse
pos_var.tool_number = 1;
pos_var.axis_data[0] = 1000;
// ... set other fields ...
char error_msg[128];
ret = enip_scanner_motoman_write_variable_p(&robot_ip, 0, &pos_var, 5000, error_msg);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "P[0] written successfully");
}
#endif
```

### `enip_scanner_motoman_read_variable_bp()` / `enip_scanner_motoman_write_variable_bp()`

Read/write base position-type variable (BP) from/to Motoman controller.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_variable_bp(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 enip_scanner_motoman_base_position_t *position, uint32_t timeout_ms);
esp_err_t enip_scanner_motoman_write_variable_bp(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                  const enip_scanner_motoman_base_position_t *position,
                                                  uint32_t timeout_ms, char *error_message);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `variable_number` - Variable BP number (0-based)
- `position` - Pointer to base position structure
- `timeout_ms` - Timeout for the operation in milliseconds
- `error_message` - Buffer to store error message (128 bytes, can be NULL, write only)

**Returns:**
- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_FAIL` - Operation failed

**Base Position Structure:**
```c
typedef struct {
    ip4_addr_t ip_address;
    bool success;
    uint32_t data_type;         // 0=Pulse, 16=Base
    int32_t axis_data[8];       // 8 axis data values
    char error_message[128];
} enip_scanner_motoman_base_position_t;
```

### `enip_scanner_motoman_read_variable_ex()` / `enip_scanner_motoman_write_variable_ex()`

Read/write external axis position-type variable (EX) from/to Motoman controller.

**Prototype:**
```c
esp_err_t enip_scanner_motoman_read_variable_ex(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 enip_scanner_motoman_external_position_t *position, uint32_t timeout_ms);
esp_err_t enip_scanner_motoman_write_variable_ex(const ip4_addr_t *ip_address, uint16_t variable_number,
                                                 const enip_scanner_motoman_external_position_t *position,
                                                 uint32_t timeout_ms, char *error_message);
```

**Parameters:**
- `ip_address` - Target robot IP address
- `variable_number` - Variable EX number (0-based)
- `position` - Pointer to external position structure
- `timeout_ms` - Timeout for the operation in milliseconds
- `error_message` - Buffer to store error message (128 bytes, can be NULL, write only)

**Returns:**
- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_FAIL` - Operation failed

**External Position Structure:**
```c
typedef struct {
    ip4_addr_t ip_address;
    bool success;
    uint32_t data_type;         // 0=Pulse
    int32_t axis_data[8];       // 8 axis data values
    char error_message[128];
} enip_scanner_motoman_external_position_t;
```

**See Also:**
- [MOTOMAN_CIP_CLASSES.md](MOTOMAN_CIP_CLASSES.md) - Complete reference for all Motoman CIP classes
- [Examples](../../examples/README.md) - Real-world translator example

---

**Parameters:**
- `cip_data_type` - CIP data type code

**Returns:**
- String name of data type (e.g., "DINT", "REAL", "STRING")
- "Unknown" if not recognized

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
enip_scanner_tag_result_t result;
memset(&result, 0, sizeof(result));

esp_err_t ret = enip_scanner_read_tag(&device_ip, "MyTag", &result, 5000);
if (ret == ESP_OK && result.success) {
    const char *type_name = enip_scanner_get_data_type_name(result.cip_data_type);
    ESP_LOGI(TAG, "Tag type: %s (0x%04X)", type_name, result.cip_data_type);
}

enip_scanner_free_tag_result(&result);
#endif
```

### Complete Data Type Reference

The following table lists all 20 CIP data types supported by the API:

| Data Type | Code | Size | Description | API | Web UI |
|-----------|------|------|-------------|-----|--------|
| BOOL | 0xC1 | 1 byte | Boolean (0 or 1) | ✅ | ✅ |
| SINT | 0xC2 | 1 byte | Signed 8-bit integer (-128 to 127) | ✅ | ✅ |
| INT | 0xC3 | 2 bytes | Signed 16-bit integer (-32768 to 32767) | ✅ | ✅ |
| DINT | 0xC4 | 4 bytes | Signed 32-bit integer | ✅ | ✅ |
| LINT | 0xC5 | 8 bytes | Signed 64-bit integer | ✅ | ❌ |
| USINT | 0xC6 | 1 byte | Unsigned 8-bit integer (0 to 255) | ✅ | ❌ |
| UINT | 0xC7 | 2 bytes | Unsigned 16-bit integer (0 to 65535) | ✅ | ❌ |
| UDINT | 0xC8 | 4 bytes | Unsigned 32-bit integer | ✅ | ❌ |
| ULINT | 0xC9 | 8 bytes | Unsigned 64-bit integer | ✅ | ❌ |
| REAL | 0xCA | 4 bytes | IEEE 754 single precision float | ✅ | ✅ |
| LREAL | 0xCB | 8 bytes | IEEE 754 double precision float | ✅ | ❌ |
| TIME | 0xCC | 4 bytes | Time (milliseconds) - Called "TIME" on Micro800, "STIME" in CIP spec | ✅ | ❌ |
| DATE | 0xCD | 2 bytes | Date (days since 1970-01-01) | ✅ | ❌ |
| TIME_OF_DAY | 0xCE | 4 bytes | Time of day (milliseconds since midnight) | ✅ | ❌ |
| DATE_AND_TIME | 0xCF | 8 bytes | Date and time (combined) | ✅ | ❌ |
| STRING | 0xDA | Variable | String (max 255 chars, 1-byte length prefix) | ✅ | ✅ |
| BYTE | 0xD1 | 1 byte | 8-bit bit string | ✅ | ❌ |
| WORD | 0xD2 | 2 bytes | 16-bit bit string | ✅ | ❌ |
| DWORD | 0xD3 | 4 bytes | 32-bit bit string | ✅ | ❌ |
| LWORD | 0xD4 | 8 bytes | 64-bit bit string | ✅ | ❌ |

**Legend:**
- ✅ = Supported
- ❌ = Not supported (use API instead)

**Notes:**
- All data types use little-endian byte order
- STRING type includes a 1-byte length prefix automatically handled by the encoder/decoder
- Date/time types follow CIP specification encoding
- Bit string types (BYTE, WORD, DWORD, LWORD) are treated as raw byte arrays

---

## Implicit Messaging Operations

Implicit messaging operations are only available when `CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT` is enabled.

Implicit messaging (Class 1 I/O) provides real-time, cyclic data exchange between an EtherNet/IP scanner and target device using UDP-based packets on port 2222. This is designed for time-critical I/O data transfer.

**Key Features:**
- UDP-based cyclic data exchange (port 2222)
- Bidirectional data streams (O-to-T and T-to-O)
- Automatic heartbeat at configured RPI (Requested Packet Interval)
- Asynchronous T-to-O data reception via callback
- Connection-based with Forward Open/Close management

**See Also:** [IMPLICIT_MESSAGING_API.md](IMPLICIT_MESSAGING_API.md) for complete guide and examples.

### `enip_scanner_implicit_open()`

Open an implicit messaging connection to a target device.

**Prototype:**
```c
esp_err_t enip_scanner_implicit_open(
    const ip4_addr_t *ip_address,
    uint16_t assembly_instance_consumed,
    uint16_t assembly_instance_produced,
    uint16_t assembly_data_size_consumed,
    uint16_t assembly_data_size_produced,
    uint32_t rpi_ms,
    enip_implicit_data_callback_t callback,
    void *user_data,
    uint32_t timeout_ms,
    bool exclusive_owner
);
```

**Parameters:**
- `ip_address`: Target device IP address
- `assembly_instance_consumed`: O-to-T assembly instance (typically 150)
- `assembly_instance_produced`: T-to-O assembly instance (typically 100)
- `assembly_data_size_consumed`: O-to-T data size in bytes (0 = autodetect)
- `assembly_data_size_produced`: T-to-O data size in bytes (0 = autodetect)
- `rpi_ms`: Requested Packet Interval in milliseconds (10-10000)
- `callback`: Function called when T-to-O data is received
- `user_data`: User data passed to callback
- `timeout_ms`: Timeout for Forward Open operation
- `exclusive_owner`: `true` for PTP mode, `false` for non-PTP mode

**Returns:**
- `ESP_OK`: Connection opened successfully
- `ESP_ERR_INVALID_ARG`: Invalid parameters
- `ESP_ERR_INVALID_STATE`: Scanner not initialized or connection already open
- `ESP_ERR_NO_MEM`: No free connection slots
- `ESP_ERR_NOT_FOUND`: Autodetection failed
- `ESP_FAIL`: Forward Open failed

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
#include "enip_scanner.h"
#include "lwip/inet.h"

void t_to_o_callback(const ip4_addr_t *ip_address,
                    uint16_t assembly_instance,
                    const uint8_t *data,
                    uint16_t data_length,
                    void *user_data)
{
    ESP_LOGI("app", "Received %u bytes from assembly %u", data_length, assembly_instance);
}

void open_implicit_connection(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    esp_err_t ret = enip_scanner_implicit_open(
        &device_ip,
        150,        // O-to-T assembly
        100,        // T-to-O assembly
        0,          // Autodetect O-to-T size
        0,          // Autodetect T-to-O size
        100,        // 100ms RPI
        t_to_o_callback,
        NULL,
        5000,
        true        // PTP mode
    );
    
    if (ret == ESP_OK) {
        ESP_LOGI("app", "Implicit connection opened");
    }
}
#endif
```

### `enip_scanner_implicit_close()`

Close an implicit messaging connection.

**Prototype:**
```c
esp_err_t enip_scanner_implicit_close(
    const ip4_addr_t *ip_address,
    uint32_t timeout_ms
);
```

**Parameters:**
- `ip_address`: Target device IP address
- `timeout_ms`: Timeout for Forward Close operation

**Returns:**
- `ESP_OK`: Connection closed successfully
- `ESP_ERR_INVALID_ARG`: Invalid IP address
- `ESP_ERR_NOT_FOUND`: No connection found

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
void close_implicit_connection(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    esp_err_t ret = enip_scanner_implicit_close(&device_ip, 5000);
    if (ret == ESP_OK) {
        ESP_LOGI("app", "Connection closed");
    }
}
#endif
```

### `enip_scanner_implicit_write_data()`

Write data to the O-to-T assembly instance. Data is stored in memory and sent automatically every RPI.

**Prototype:**
```c
esp_err_t enip_scanner_implicit_write_data(
    const ip4_addr_t *ip_address,
    const uint8_t *data,
    uint16_t data_length
);
```

**Parameters:**
- `ip_address`: Target device IP address
- `data`: Data buffer to write
- `data_length`: Data length in bytes (must match `assembly_data_size_consumed`)

**Returns:**
- `ESP_OK`: Data written successfully
- `ESP_ERR_INVALID_ARG`: Invalid parameters
- `ESP_ERR_NOT_FOUND`: No connection found
- `ESP_ERR_NO_MEM`: Memory allocation failed

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
void write_output_data(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    uint8_t output_data[40] = {0x01, 0x00, 0x00, 0x00};
    esp_err_t ret = enip_scanner_implicit_write_data(&device_ip, output_data, 40);
    
    if (ret == ESP_OK) {
        ESP_LOGI("app", "Output data written");
    }
}
#endif
```

### `enip_scanner_implicit_read_o_to_t_data()`

Read the current O-to-T data that's being sent in heartbeat packets.

**Prototype:**
```c
esp_err_t enip_scanner_implicit_read_o_to_t_data(
    const ip4_addr_t *ip_address,
    uint8_t *data,
    uint16_t *data_length,
    uint16_t max_length
);
```

**Parameters:**
- `ip_address`: Target device IP address
- `data`: Buffer to store data
- `data_length`: Pointer to store actual data length
- `max_length`: Maximum bytes to read

**Returns:**
- `ESP_OK`: Data read successfully
- `ESP_ERR_INVALID_ARG`: Invalid parameters
- `ESP_ERR_NOT_FOUND`: No connection found

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
void read_current_output(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    uint8_t current_data[40];
    uint16_t data_length = 0;
    
    esp_err_t ret = enip_scanner_implicit_read_o_to_t_data(
        &device_ip, current_data, &data_length, sizeof(current_data));
    
    if (ret == ESP_OK) {
        ESP_LOGI("app", "Current O-to-T data: %u bytes", data_length);
    }
}
#endif
```

### Callback Function Type

```c
typedef void (*enip_implicit_data_callback_t)(
    const ip4_addr_t *ip_address,
    uint16_t assembly_instance,
    const uint8_t *data,
    uint16_t data_length,
    void *user_data
);
```

**Notes:**
- Callback is called from receive task when T-to-O data arrives
- Data buffer is freed after callback returns - copy data if needed
- Keep callback fast - don't block or perform heavy operations
- Use `user_data` parameter to pass context information

---

## Session Management

### `enip_scanner_register_session()`

Register an EtherNet/IP session with a device. Sessions are required for explicit messaging operations.

**Prototype:**
```c
esp_err_t enip_scanner_register_session(const ip4_addr_t *ip_address,
                                        uint32_t *session_handle,
                                        uint32_t timeout_ms,
                                        char *error_message);
```

**Parameters:**
- `ip_address` - Target device IP address
- `session_handle` - Pointer to store session handle
- `timeout_ms` - Timeout for registration (milliseconds)
- `error_message` - Buffer for error message (128 bytes, can be NULL)

**Returns:**
- `ESP_OK` - Session registered successfully
- `ESP_ERR_INVALID_ARG` - Invalid parameters
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_FAIL` - General failure

**Example:**
```c
#include "enip_scanner.h"
#include "lwip/inet.h"

void register_session_example(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);

    uint32_t session_handle = 0;
    char error_msg[128];

    esp_err_t ret = enip_scanner_register_session(&device_ip, &session_handle, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Session registered: 0x%08lX", (unsigned long)session_handle);
        
        // Use session for operations...
        
        // Unregister when done
        enip_scanner_unregister_session(&device_ip, session_handle, 5000);
    } else {
        ESP_LOGE(TAG, "Session registration failed: %s", error_msg);
    }
}
```

**Note:** Most API functions handle session management automatically. This function is provided for advanced use cases.

### `enip_scanner_unregister_session()`

Unregister an EtherNet/IP session with a device.

**Prototype:**
```c
esp_err_t enip_scanner_unregister_session(const ip4_addr_t *ip_address,
                                          uint32_t session_handle,
                                          uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target device IP address
- `session_handle` - Session handle to unregister
- `timeout_ms` - Timeout for unregistration (milliseconds)

**Returns:**
- `ESP_OK` - Session unregistered successfully
- `ESP_ERR_INVALID_ARG` - Invalid parameters
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_FAIL` - General failure

**Example:**
```c
#include "enip_scanner.h"
#include "lwip/inet.h"

void unregister_session_example(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    uint32_t session_handle = 0x12345678;  // From previous registration
    
    esp_err_t ret = enip_scanner_unregister_session(&device_ip, session_handle, 5000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Session unregistered successfully");
    } else {
        ESP_LOGE(TAG, "Session unregistration failed");
    }
}
```

---

## Data Structures

### `enip_scanner_device_info_t`

Device information structure returned by `enip_scanner_scan_devices()`.

```c
typedef struct {
    ip4_addr_t ip_address;      // Device IP address
    uint16_t vendor_id;         // Vendor ID
    uint16_t device_type;       // Device Type
    uint16_t product_code;      // Product Code
    uint8_t major_revision;     // Major Revision
    uint8_t minor_revision;     // Minor Revision
    uint16_t status;            // Status word
    uint32_t serial_number;     // Serial Number
    char product_name[33];       // Product Name (null-terminated, max 32 chars)
    bool online;                // Device is online and responding
    uint32_t response_time_ms;  // Response time in milliseconds
} enip_scanner_device_info_t;
```

### `enip_scanner_assembly_result_t`

Assembly read result structure.

```c
typedef struct {
    ip4_addr_t ip_address;      // Device IP address
    uint16_t assembly_instance; // Assembly instance number
    bool success;               // Scan was successful
    uint8_t *data;              // Assembly data (allocated, caller must free)
    uint16_t data_length;       // Length of assembly data
    uint32_t response_time_ms;  // Response time in milliseconds
    char error_message[128];   // Error message if scan failed
} enip_scanner_assembly_result_t;
```

### `enip_scanner_tag_result_t`

Tag read result structure (only available when tag support is enabled).

```c
typedef struct {
    ip4_addr_t ip_address;      // Device IP address
    char tag_path[128];         // Tag path that was read
    bool success;               // Read was successful
    uint8_t *data;              // Tag data (allocated, caller must free)
    uint16_t data_length;       // Length of tag data in bytes
    uint16_t cip_data_type;     // CIP data type code
    uint32_t response_time_ms;  // Response time in milliseconds
    char error_message[128];   // Error message if read failed
} enip_scanner_tag_result_t;
```

---

## Error Handling

All API functions return `esp_err_t` error codes. Always check return values:

```c
esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);

if (ret == ESP_OK) {
    // Operation completed - check result->success for actual status
    if (result.success) {
        // Success - use result.data
    } else {
        // Failed - check result.error_message
        ESP_LOGE(TAG, "Error: %s", result.error_message);
    }
} else {
    // Operation failed before completion
    ESP_LOGE(TAG, "Operation failed: %s", esp_err_to_name(ret));
}

// Always free resources
enip_scanner_free_assembly_result(&result);
```

**Common Error Codes:**
- `ESP_OK` - Success
- `ESP_ERR_INVALID_ARG` - Invalid parameters
- `ESP_ERR_INVALID_STATE` - Scanner not initialized
- `ESP_ERR_NO_MEM` - Memory allocation failed
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_FAIL` - General failure

---

## Thread Safety

All API functions are **thread-safe** and can be called from multiple FreeRTOS tasks concurrently. The component uses internal mutexes to protect shared state - no additional synchronization is required from application code.

**Example - Concurrent Operations:**
```c
// Task 1
void task1(void *pvParameters)
{
    enip_scanner_device_info_t devices[10];
    int count = enip_scanner_scan_devices(devices, 10, 5000);
    // ... use devices ...
}

// Task 2
void task2(void *pvParameters)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    enip_scanner_assembly_result_t result;
    memset(&result, 0, sizeof(result));
    enip_scanner_read_assembly(&device_ip, 100, &result, 5000);
    // ... use result ...
    enip_scanner_free_assembly_result(&result);
}

// Both tasks can run concurrently safely
xTaskCreate(task1, "scan_task", 4096, NULL, 5, NULL);
xTaskCreate(task2, "read_task", 4096, NULL, 5, NULL);
```

---

## Resource Management

### Memory Management

Functions that allocate memory:
- `enip_scanner_read_assembly()` - Allocates `result->data`
- `enip_scanner_read_tag()` - Allocates `result->data`

**Always free allocated memory:**
```c
enip_scanner_assembly_result_t result;
memset(&result, 0, sizeof(result));

esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);

// Always free, even on error
enip_scanner_free_assembly_result(&result);
```

### Socket Management

All socket operations are handled internally. Sockets are automatically closed on error or completion. No manual socket management is required.

---

## Complete Examples

### Example 1: Device Discovery and Assembly Read

```c
#include "enip_scanner.h"
#include "lwip/inet.h"

void discover_and_read_example(void)
{
    // Initialize scanner
    esp_err_t ret = enip_scanner_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize scanner");
        return;
    }
    
    // Discover devices
    enip_scanner_device_info_t devices[10];
    int count = enip_scanner_scan_devices(devices, 10, 5000);
    
    ESP_LOGI(TAG, "Found %d device(s)", count);
    
    // Read assembly from first device
    if (count > 0) {
        enip_scanner_assembly_result_t result;
        memset(&result, 0, sizeof(result));
        
        ret = enip_scanner_read_assembly(&devices[0].ip_address, 100, &result, 5000);
        if (ret == ESP_OK && result.success) {
            ESP_LOGI(TAG, "Read %d bytes from device", result.data_length);
            // Process data...
        }
        
        enip_scanner_free_assembly_result(&result);
    }
}
```

### Example 2: Tag Read/Write Loop

```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
#include "enip_scanner.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void tag_loop_example(void *pvParameters)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    while (1) {
        // Read tag
        enip_scanner_tag_result_t result;
        memset(&result, 0, sizeof(result));
        
        esp_err_t ret = enip_scanner_read_tag(&device_ip, "Counter", &result, 5000);
        if (ret == ESP_OK && result.success && result.cip_data_type == CIP_DATA_TYPE_DINT) {
            int32_t value = (int32_t)(result.data[0] | 
                                      (result.data[1] << 8) | 
                                      (result.data[2] << 16) | 
                                      (result.data[3] << 24));
            ESP_LOGI(TAG, "Counter = %ld", value);
        }
        enip_scanner_free_tag_result(&result);
        
        // Write tag
        int32_t new_value = 100;
        uint8_t bytes[4];
        bytes[0] = new_value & 0xFF;
        bytes[1] = (new_value >> 8) & 0xFF;
        bytes[2] = (new_value >> 16) & 0xFF;
        bytes[3] = (new_value >> 24) & 0xFF;
        
        char error_msg[128];
        ret = enip_scanner_write_tag(&device_ip, "Setpoint", bytes, 4, 
                                     CIP_DATA_TYPE_DINT, 5000, error_msg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Write failed: %s", error_msg);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));  // Wait 1 second
    }
}
#endif
```

### Example 3: Assembly Discovery and Monitoring

```c
#include "enip_scanner.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void monitor_assemblies_example(void *pvParameters)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    // Discover assemblies
    uint16_t instances[32];
    int count = enip_scanner_discover_assemblies(&device_ip, instances, 32, 2000);
    
    ESP_LOGI(TAG, "Found %d assembly instance(s)", count);
    
    // Monitor each assembly
    while (1) {
        for (int i = 0; i < count; i++) {
            enip_scanner_assembly_result_t result;
            memset(&result, 0, sizeof(result));
            
            esp_err_t ret = enip_scanner_read_assembly(&device_ip, instances[i], &result, 2000);
            if (ret == ESP_OK && result.success) {
                ESP_LOGI(TAG, "Assembly %d: %d bytes", instances[i], result.data_length);
                // Process data...
            }
            
            enip_scanner_free_assembly_result(&result);
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // Wait 5 seconds
    }
}
```

---

## Additional Resources

- **Main Repository**: [ESP32_ENIPScanner on GitHub](https://github.com/AGSweeney/ESP32_ENIPScanner)
- **Component README**: [Component README](README.md)
- **Motoman CIP Classes**: [MOTOMAN_CIP_CLASSES.md](MOTOMAN_CIP_CLASSES.md) - Complete reference for Motoman vendor-specific CIP classes
- **Examples**: [Examples README](../../examples/README.md) - Real-world translator example (Micro800 ↔ Motoman)
- **Project README**: [Main Project README](../../README.md)

## License

See LICENSE file in project root.

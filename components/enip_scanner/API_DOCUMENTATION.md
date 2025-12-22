# EtherNet/IP Scanner API Documentation

## Overview

The EtherNet/IP Scanner component provides **explicit messaging** capabilities for communicating with EtherNet/IP devices over TCP/IP networks. This API allows you to discover devices, read and write assembly data, and manage EtherNet/IP sessions.

![EtherNet/IP Scanner Web Interface](ESP32-ENIPScanner.png)

*Screenshot of the EtherNet/IP Scanner web interface showing device discovery, assembly read/write operations, and data editing capabilities.*

**Key Features:**
- Device discovery via UDP broadcast
- Read/Write assembly data using explicit messaging
- Session management for persistent connections
- Assembly instance discovery
- **Allen-Bradley Tag Support** (Experimental) - Read and write tags on Micro800 series PLCs using symbolic addressing
- **Thread-safe operations** - All API functions are protected with mutexes for safe concurrent access
- **Robust error handling** - Comprehensive error reporting and resource cleanup
- **Memory safety** - Proper bounds checking and overflow protection

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Initialization](#initialization)
3. [Device Discovery](#device-discovery)
4. [Reading Assembly Data](#reading-assembly-data)
5. [Writing Assembly Data](#writing-assembly-data)
6. [Assembly Discovery](#assembly-discovery)
7. [Session Management](#session-management)
8. [Tag Support (Micro800 Series - Experimental)](#tag-support-micro800-series---experimental)
9. [Data Structures](#data-structures)
10. [Error Handling](#error-handling)
11. [Thread Safety](#thread-safety)
12. [Resource Management](#resource-management)
13. [Complete Example](#complete-example)
14. [Notes](#notes)

---

## Quick Start

```c
#include "enip_scanner.h"

// Initialize the scanner
enip_scanner_init();

// Read assembly data
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);

enip_scanner_assembly_result_t result;
if (enip_scanner_read_assembly(&device_ip, 100, &result, 5000) == ESP_OK && result.success) {
    // Use result.data[0], result.data[1], etc.
    uint8_t byte0 = result.data[0];
    bool bit1 = (byte0 & 0x02) != 0;  // Check bit 1
    
    enip_scanner_free_assembly_result(&result);
}

// Write assembly data
uint8_t output_data[4] = {0x04, 0x00, 0x00, 0x00};  // Set bit 2 in byte 0
char error_msg[128];
if (enip_scanner_write_assembly(&device_ip, 150, output_data, 4, 5000, error_msg) != ESP_OK) {
    ESP_LOGE(TAG, "Write failed: %s", error_msg);
}
```

---

## Initialization

### `enip_scanner_init()`

Initialize the EtherNet/IP scanner component. Must be called once before using any other functions.

**Prototype:**
```c
esp_err_t enip_scanner_init(void);
```

**Returns:**
- `ESP_OK` on success
- Error code on failure

**Example:**
```c
esp_err_t ret = enip_scanner_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize scanner: %s", esp_err_to_name(ret));
    return;
}
```

**Note:** 
- This function should be called after network initialization (e.g., after receiving IP address)
- The function is **idempotent** - safe to call multiple times (returns `ESP_OK` if already initialized)
- **Thread-safe** - Can be called from multiple tasks concurrently
- Creates internal mutex for thread synchronization

---

## Device Discovery

### `enip_scanner_scan_devices()`

Scan the local network for EtherNet/IP devices using UDP broadcast List Identity requests. This function broadcasts a discovery packet and collects responses from all devices on the network.

**Prototype:**
```c
int enip_scanner_scan_devices(enip_scanner_device_info_t *devices, 
                               int max_devices, 
                               uint32_t timeout_ms);
```

**Parameters:**
- `devices` - Array to store discovered device information (must be pre-allocated)
- `max_devices` - Maximum number of devices to scan for (size of devices array)
- `timeout_ms` - Timeout for collecting device responses in milliseconds

**Returns:**
- Number of devices found (0 or more)
- Returns immediately after timeout expires

**Example:**
```c
enip_scanner_device_info_t devices[10];
int count = enip_scanner_scan_devices(devices, 10, 5000);

ESP_LOGI(TAG, "Found %d EtherNet/IP device(s)", count);
for (int i = 0; i < count; i++) {
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&devices[i].ip_address));
    ESP_LOGI(TAG, "Device %d: %s - %s", i+1, ip_str, devices[i].product_name);
}
```

**Note:** 
- This function uses UDP broadcast and may take several seconds to complete
- The timeout applies to the entire scan operation, not per device
- **Thread-safe** - Can be called from multiple tasks concurrently
- Automatically limits scan range to prevent excessive network traffic (max 254 addresses)
- Includes integer overflow protection for network range calculations

---

## Reading Assembly Data

### `enip_scanner_read_assembly()`

Read assembly data from an EtherNet/IP device using explicit messaging (Get_Attribute_Single CIP service). Each call establishes a TCP connection, reads the data, and closes the connection.

**Prototype:**
```c
esp_err_t enip_scanner_read_assembly(const ip4_addr_t *ip_address, 
                                     uint16_t assembly_instance, 
                                     enip_scanner_assembly_result_t *result, 
                                     uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target device IP address (must be valid)
- `assembly_instance` - Assembly instance number (e.g., 100, 150, 20)
- `result` - Pointer to structure to store result (caller must free `result->data` after use)
- `timeout_ms` - Timeout for the operation in milliseconds

**Returns:**
- `ESP_OK` on success (check `result->success` to verify data was read)
- `ESP_ERR_INVALID_ARG` if parameters are invalid
- `ESP_ERR_TIMEOUT` if operation timed out
- `ESP_FAIL` on general failure

**Example:**
```c
ip4_addr_t target_ip;
inet_aton("192.168.1.100", &target_ip);

enip_scanner_assembly_result_t result;
esp_err_t ret = enip_scanner_read_assembly(&target_ip, 100, &result, 5000);

if (ret == ESP_OK && result.success) {
    ESP_LOGI(TAG, "Read %d bytes from assembly %d", result.data_length, result.assembly_instance);
    
    // Access individual bytes
    if (result.data_length > 0) {
        uint8_t byte0 = result.data[0];
        bool bit0 = (byte0 & 0x01) != 0;  // Check bit 0
        bool bit1 = (byte0 & 0x02) != 0;  // Check bit 1
        bool bit2 = (byte0 & 0x04) != 0;  // Check bit 2
        
        ESP_LOGI(TAG, "Byte 0: 0x%02X (bit 0=%d, bit 1=%d, bit 2=%d)", 
                 byte0, bit0, bit1, bit2);
    }
    
    // Always free the allocated data
    enip_scanner_free_assembly_result(&result);
} else {
    ESP_LOGE(TAG, "Failed to read assembly: %s", result.error_message);
}
```

**Bit Manipulation:**
```c
// Check if bit N is set (bits are numbered 0-7, right to left)
bool bit_is_set = (byte & (1 << N)) != 0;

// Set bit N
byte |= (1 << N);

// Clear bit N
byte &= ~(1 << N);

// Toggle bit N
byte ^= (1 << N);
```

**Memory Management:**
- `result->data` is allocated by the function and must be freed using `enip_scanner_free_assembly_result()`
- **Always free the result** even if `result->success` is false or an error occurs
- Do not access `result->data` after freeing
- The function ensures proper cleanup of all resources (sockets, memory) on all error paths
- **Thread-safe** - Can be called from multiple tasks concurrently

**Resource Cleanup:**
- All sockets are properly closed even on error
- Memory is freed on all code paths
- Session cleanup is handled automatically

---

## Writing Assembly Data

### `enip_scanner_write_assembly()`

Write assembly data to an EtherNet/IP device using explicit messaging (Set_Attribute_Single CIP service). Each call establishes a TCP connection, writes the data, and closes the connection.

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
- `data` - Data buffer to write (must be valid)
- `data_length` - Length of data to write in bytes
- `timeout_ms` - Timeout for the operation in milliseconds
- `error_message` - Buffer to store error message (128 bytes, can be NULL)

**Returns:**
- `ESP_OK` on success
- `ESP_ERR_INVALID_ARG` if parameters are invalid
- `ESP_ERR_TIMEOUT` if operation timed out
- `ESP_FAIL` on general failure

**Example:**
```c
ip4_addr_t target_ip;
inet_aton("192.168.1.100", &target_ip);

// Prepare output data - set bit 2 in byte 0
uint8_t output_data[4] = {0x04, 0x00, 0x00, 0x00};  // 0x04 = bit 2 set

char error_msg[128];
esp_err_t ret = enip_scanner_write_assembly(&target_ip, 150, output_data, 4, 5000, error_msg);

if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Successfully wrote %d bytes to assembly 150", 4);
} else {
    ESP_LOGE(TAG, "Write failed: %s", error_msg);
}
```

**Bit Manipulation Example:**
```c
uint8_t output_data[4] = {0};

// Set bit 2 in byte 0
output_data[0] |= (1 << 2);  // Sets bit 2, value becomes 0x04

// Set multiple bits
output_data[0] |= (1 << 0) | (1 << 2);  // Sets bits 0 and 2, value becomes 0x05

// Clear bit 1 in byte 0
output_data[0] &= ~(1 << 1);

// Write the data
enip_scanner_write_assembly(&target_ip, 150, output_data, 4, 5000, NULL);
```

**Note:** 
- Not all assemblies are writable. Use `enip_scanner_is_assembly_writable()` to check before writing
- **Thread-safe** - Can be called from multiple tasks concurrently
- All sockets are properly closed even on error
- Memory allocation failures are handled gracefully

---

## Assembly Discovery

### `enip_scanner_discover_assemblies()`

Discover valid assembly instances for a device by probing common instances or reading the Max Instance attribute from the Assembly class.

**Prototype:**
```c
int enip_scanner_discover_assemblies(const ip4_addr_t *ip_address, 
                                     uint16_t *instances, 
                                     int max_instances, 
                                     uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target device IP address
- `instances` - Array to store discovered instance numbers (must be pre-allocated)
- `max_instances` - Maximum number of instances to discover (size of instances array)
- `timeout_ms` - Timeout for each probe in milliseconds

**Returns:**
- Number of valid instances found (0 or more)

**Example:**
```c
ip4_addr_t target_ip;
inet_aton("192.168.1.100", &target_ip);

uint16_t instances[32];
int count = enip_scanner_discover_assemblies(&target_ip, instances, 32, 5000);

ESP_LOGI(TAG, "Found %d assembly instance(s)", count);
for (int i = 0; i < count; i++) {
    ESP_LOGI(TAG, "  Instance %d", instances[i]);
}
```

### `enip_scanner_is_assembly_writable()`

Check if an assembly instance is writable by attempting to read it. If readable, it's assumed to be writable.

**Prototype:**
```c
bool enip_scanner_is_assembly_writable(const ip4_addr_t *ip_address, 
                                        uint16_t assembly_instance, 
                                        uint32_t timeout_ms);
```

**Returns:**
- `true` if assembly is readable (assumed writable)
- `false` otherwise

**Note:**
- **Thread-safe** - Can be called from multiple tasks concurrently
- Always frees resources even on error

**Example:**
```c
if (enip_scanner_is_assembly_writable(&target_ip, 150, 5000)) {
    ESP_LOGI(TAG, "Assembly 150 is writable");
} else {
    ESP_LOGE(TAG, "Assembly 150 is not writable");
}
```

---

## Session Management

### `enip_scanner_register_session()`

Register an EtherNet/IP session with a device. Sessions are required for some operations and can be reused for multiple operations.

**Prototype:**
```c
esp_err_t enip_scanner_register_session(const ip4_addr_t *ip_address,
                                        uint32_t *session_handle,
                                        uint32_t timeout_ms,
                                        char *error_message);
```

**Parameters:**
- `ip_address` - Target device IP address
- `session_handle` - Pointer to store session handle (output parameter)
- `timeout_ms` - Timeout for registration in milliseconds
- `error_message` - Buffer to store error message (128 bytes, can be NULL)

**Returns:**
- `ESP_OK` on success
- Error code on failure

**Example:**
```c
ip4_addr_t target_ip;
inet_aton("192.168.1.100", &target_ip);

uint32_t session_handle = 0;
char error_msg[128];
esp_err_t ret = enip_scanner_register_session(&target_ip, &session_handle, 5000, error_msg);

if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Session registered: 0x%08lX", (unsigned long)session_handle);
    // Use session_handle for operations...
} else {
    ESP_LOGE(TAG, "Session registration failed: %s", error_msg);
}
```

### `enip_scanner_unregister_session()`

Unregister an EtherNet/IP session. Call this when done with a session to clean up resources.

**Prototype:**
```c
esp_err_t enip_scanner_unregister_session(const ip4_addr_t *ip_address,
                                          uint32_t session_handle,
                                          uint32_t timeout_ms);
```

**Parameters:**
- `ip_address` - Target device IP address
- `session_handle` - Session handle to unregister
- `timeout_ms` - Timeout for unregistration in milliseconds

**Returns:**
- `ESP_OK` on success
- Error code on failure

**Example:**
```c
enip_scanner_unregister_session(&target_ip, session_handle, 5000);
```

**Note:** 
- Session management is optional for basic read/write operations. The API automatically manages sessions internally
- Use these functions only if you need explicit session control
- **Thread-safe** - Can be called from multiple tasks concurrently
- All sockets are properly closed even on error

---

## Tag Support (Micro800 Series - Experimental)

**⚠️ Experimental Feature:** Tag support is an experimental feature specifically designed for Allen-Bradley Micro800 series PLCs. This feature must be enabled via Kconfig before use.

### Enabling Tag Support

Tag support is disabled by default to reduce code size. To enable:

1. Run `idf.py menuconfig`
2. Navigate to: **Component config** → **EtherNet/IP Scanner Configuration**
3. Enable: **"Enable Allen-Bradley tag support"**
4. Save and rebuild your project

### Overview

Tag support allows you to read and write PLC tags using symbolic names (e.g., `"MyCounter"`, `"Program:MainProgram.Tag"`) instead of numeric assembly instances. This is particularly useful for Micro800 PLCs where tags are the primary way to access data.

**Supported Data Types:**
- `CIP_DATA_TYPE_BOOL` (0xC1) - Boolean (1 byte)
- `CIP_DATA_TYPE_SINT` (0xC2) - Signed 8-bit integer
- `CIP_DATA_TYPE_INT` (0xC3) - Signed 16-bit integer
- `CIP_DATA_TYPE_DINT` (0xC4) - Signed 32-bit integer
- `CIP_DATA_TYPE_REAL` (0xCA) - IEEE 754 single precision float
- `CIP_DATA_TYPE_STRING` (0xDA) - String (variable length)

### Reading Tags

#### `enip_scanner_read_tag()`

Read a tag value from a Micro800 PLC using its symbolic name.

**Prototype:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
esp_err_t enip_scanner_read_tag(const ip4_addr_t *ip_address,
                                const char *tag_path,
                                enip_scanner_tag_result_t *result,
                                uint32_t timeout_ms);
#endif
```

**Parameters:**
- `ip_address` - Target device IP address
- `tag_path` - Tag path/name (e.g., `"MyTag"`, `"Program:MainProgram.Tag"`, `"MyArray[0]"`)
- `result` - Pointer to structure to store result (caller must free `result->data` after use)
- `timeout_ms` - Timeout for the operation in milliseconds

**Returns:**
- `ESP_OK` on success (check `result->success` to verify data was read)
- `ESP_ERR_INVALID_ARG` if parameters are invalid
- `ESP_ERR_TIMEOUT` if operation timed out
- `ESP_FAIL` on general failure

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);

// Read a DINT tag
enip_scanner_tag_result_t result;
esp_err_t ret = enip_scanner_read_tag(&device_ip, "MyCounter", &result, 5000);

if (ret == ESP_OK && result.success) {
    ESP_LOGI(TAG, "Tag: %s", result.tag_path);
    ESP_LOGI(TAG, "Data Type: %s (0x%04X)", 
             enip_scanner_get_data_type_name(result.cip_data_type),
             result.cip_data_type);
    ESP_LOGI(TAG, "Data Length: %d bytes", result.data_length);
    ESP_LOGI(TAG, "Response Time: %lu ms", result.response_time_ms);
    
    // Interpret based on data type
    if (result.cip_data_type == CIP_DATA_TYPE_BOOL && result.data_length >= 1) {
        bool value = (result.data[0] != 0);
        ESP_LOGI(TAG, "Value (BOOL): %s", value ? "true" : "false");
    } else if (result.cip_data_type == CIP_DATA_TYPE_DINT && result.data_length >= 4) {
        int32_t value = (result.data[0] | (result.data[1] << 8) | 
                        (result.data[2] << 16) | (result.data[3] << 24));
        ESP_LOGI(TAG, "Value (DINT): %ld", value);
    } else if (result.cip_data_type == CIP_DATA_TYPE_REAL && result.data_length >= 4) {
        union {
            uint32_t i;
            float f;
        } u;
        u.i = (result.data[0] | (result.data[1] << 8) | 
               (result.data[2] << 16) | (result.data[3] << 24));
        ESP_LOGI(TAG, "Value (REAL): %f", u.f);
    }
    
    // Always free the result data
    enip_scanner_free_tag_result(&result);
} else {
    ESP_LOGE(TAG, "Failed to read tag: %s", result.error_message);
    enip_scanner_free_tag_result(&result);
}
#endif
```

**Tag Path Examples:**
- Simple tag: `"MyTag"`
- Program-scoped tag: `"Program:MainProgram.MyTag"`
- Array element: `"MyArray[0]"`
- Nested structure: `"MyStruct.Field"`

**Memory Management:**
- `result->data` is allocated by the function and must be freed using `enip_scanner_free_tag_result()`
- **Always free the result** even if `result->success` is false or an error occurs
- **Thread-safe** - Can be called from multiple tasks concurrently

### Writing Tags

#### `enip_scanner_write_tag()`

Write a tag value to a Micro800 PLC using its symbolic name.

**Prototype:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
esp_err_t enip_scanner_write_tag(const ip4_addr_t *ip_address,
                                 const char *tag_path,
                                 const uint8_t *data,
                                 uint16_t data_length,
                                 uint16_t cip_data_type,
                                 uint32_t timeout_ms,
                                 char *error_message);
#endif
```

**Parameters:**
- `ip_address` - Target device IP address
- `tag_path` - Tag path/name (e.g., `"MyTag"`, `"Program:MainProgram.Tag"`)
- `data` - Data buffer to write (must be valid)
- `data_length` - Length of data to write in bytes
- `cip_data_type` - CIP data type code (e.g., `CIP_DATA_TYPE_DINT`, `CIP_DATA_TYPE_BOOL`)
- `timeout_ms` - Timeout for the operation in milliseconds
- `error_message` - Buffer to store error message (128 bytes, can be NULL)

**Returns:**
- `ESP_OK` on success
- `ESP_ERR_INVALID_ARG` if parameters are invalid
- `ESP_ERR_TIMEOUT` if operation timed out
- `ESP_FAIL` on general failure

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);

// Write a BOOL tag
uint8_t bool_value = 1;  // true
char error_msg[128];
esp_err_t ret = enip_scanner_write_tag(&device_ip, "MyBool", &bool_value, 1, 
                                      CIP_DATA_TYPE_BOOL, 5000, error_msg);

if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Successfully wrote tag MyBool");
} else {
    ESP_LOGE(TAG, "Failed to write tag: %s", error_msg);
}

// Write a DINT tag
int32_t dint_value = 12345;
uint8_t dint_bytes[4];
dint_bytes[0] = dint_value & 0xFF;
dint_bytes[1] = (dint_value >> 8) & 0xFF;
dint_bytes[2] = (dint_value >> 16) & 0xFF;
dint_bytes[3] = (dint_value >> 24) & 0xFF;

ret = enip_scanner_write_tag(&device_ip, "MyCounter", dint_bytes, 4, 
                             CIP_DATA_TYPE_DINT, 5000, error_msg);

// Write a REAL tag
float real_value = 3.14159f;
union {
    uint32_t i;
    float f;
} u;
u.f = real_value;
uint8_t real_bytes[4];
real_bytes[0] = u.i & 0xFF;
real_bytes[1] = (u.i >> 8) & 0xFF;
real_bytes[2] = (u.i >> 16) & 0xFF;
real_bytes[3] = (u.i >> 24) & 0xFF;

ret = enip_scanner_write_tag(&device_ip, "MyReal", real_bytes, 4, 
                             CIP_DATA_TYPE_REAL, 5000, error_msg);
#endif
```

**Note:**
- Data must be provided in little-endian byte order
- Data length must match the expected size for the specified CIP data type
- **Thread-safe** - Can be called from multiple tasks concurrently
- All sockets are properly closed even on error

### Data Type Helper

#### `enip_scanner_get_data_type_name()`

Get a human-readable name for a CIP data type code.

**Prototype:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
const char *enip_scanner_get_data_type_name(uint16_t cip_data_type);
#endif
```

**Parameters:**
- `cip_data_type` - CIP data type code

**Returns:**
- Human-readable string (e.g., `"DINT"`, `"BOOL"`, `"REAL"`)
- `"UNKNOWN"` for unrecognized data types

**Example:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
const char *type_name = enip_scanner_get_data_type_name(CIP_DATA_TYPE_DINT);
ESP_LOGI(TAG, "Data type: %s", type_name);  // Prints "DINT"
#endif
```

### Tag Result Structure

#### `enip_scanner_tag_result_t`

Tag read result structure returned by `enip_scanner_read_tag()`.

```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
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
#endif
```

**Important:**
- Always call `enip_scanner_free_tag_result()` to free `data` after use
- The function safely handles NULL pointers and already-freed data
- Call this function even if `success` is false to ensure proper cleanup

### Limitations and Notes

1. **Experimental Status**: This feature is experimental and may have limitations or bugs
2. **Micro800 Specific**: Designed specifically for Allen-Bradley Micro800 series PLCs
3. **Tag Discovery**: Tag names must be known in advance - there is no tag discovery/scanning capability
4. **Data Type Support**: Limited to basic data types (BOOL, SINT, INT, DINT, REAL, STRING)
5. **Tag Path Format**: Follows Allen-Bradley tag naming conventions
6. **Web UI**: A web-based tag test interface is available at `/tags` when tag support is enabled

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
    char product_name[33];      // Product Name (null-terminated, max 32 chars)
    bool online;                // Device is online and responding
    uint32_t response_time_ms;  // Response time in milliseconds
} enip_scanner_device_info_t;
```

### `enip_scanner_assembly_result_t`

Assembly read result structure returned by `enip_scanner_read_assembly()`.

```c
typedef struct {
    ip4_addr_t ip_address;      // Device IP address
    uint16_t assembly_instance; // Assembly instance number
    bool success;               // Read was successful
    uint8_t *data;              // Assembly data (allocated, caller must free)
    uint16_t data_length;       // Length of assembly data in bytes
    uint32_t response_time_ms;  // Response time in milliseconds
    char error_message[128];   // Error message if read failed
} enip_scanner_assembly_result_t;
```

**Important:** 
- Always call `enip_scanner_free_assembly_result()` to free `data` after use
- The function safely handles NULL pointers and already-freed data
- Call this function even if `success` is false to ensure proper cleanup

---

## Error Handling

All functions return `esp_err_t` error codes. Common error codes:

- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid parameters (NULL pointer, invalid IP, etc.)
- `ESP_ERR_INVALID_STATE` - Scanner not initialized
- `ESP_ERR_INVALID_RESPONSE` - Invalid response from device
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_ERR_NO_MEM` - Memory allocation failed
- `ESP_FAIL` - General failure

**Error Handling Best Practices:**
- Always check return values before accessing result data
- Check `result->success` even when return value is `ESP_OK`
- Always free resources using `enip_scanner_free_assembly_result()` even on error
- Log error messages for debugging

**Error Message Retrieval:**
- For `enip_scanner_read_assembly()`: Check `result->error_message`
- For `enip_scanner_write_assembly()`: Check `error_message` parameter
- For `enip_scanner_register_session()`: Check `error_message` parameter

**Example Error Handling:**
```c
enip_scanner_assembly_result_t result;
memset(&result, 0, sizeof(result));  // Initialize structure

esp_err_t ret = enip_scanner_read_assembly(&target_ip, 100, &result, 5000);

if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
    // Still free result in case any memory was allocated
    enip_scanner_free_assembly_result(&result);
    return;
}

if (!result.success) {
    ESP_LOGE(TAG, "Read failed: %s", result.error_message);
    enip_scanner_free_assembly_result(&result);
    return;
}

// Use result.data...
if (result.data != NULL && result.data_length > 0) {
    // Process data...
}

// Always free resources
enip_scanner_free_assembly_result(&result);
```

---

## Thread Safety

The ENIP Scanner API is **fully thread-safe** and can be safely called from multiple FreeRTOS tasks concurrently.

### Implementation Details

- **Mutex Protection**: All API functions use an internal FreeRTOS mutex (`s_scanner_mutex`) to protect shared state
- **Initialization State**: The `s_scanner_initialized` flag is protected and checked atomically
- **Network Interface Access**: Network interface access is synchronized to prevent race conditions
- **No External Synchronization Required**: Application code does not need to add mutexes or semaphores

### Concurrent Usage Example

```c
// Task 1: Device scanning
void scan_task(void *pvParameters) {
    enip_scanner_device_info_t devices[10];
    while (1) {
        int count = enip_scanner_scan_devices(devices, 10, 5000);
        ESP_LOGI("scan", "Found %d devices", count);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// Task 2: Reading assembly data
void read_task(void *pvParameters) {
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    while (1) {
        enip_scanner_assembly_result_t result;
        if (enip_scanner_read_assembly(&device_ip, 100, &result, 5000) == ESP_OK && result.success) {
            // Process data...
            enip_scanner_free_assembly_result(&result);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Both tasks can run concurrently without issues
xTaskCreate(scan_task, "scan", 4096, NULL, 5, NULL);
xTaskCreate(read_task, "read", 4096, NULL, 5, NULL);
```

### Best Practices

- ✅ Safe to call API functions from multiple tasks
- ✅ No need for application-level mutexes
- ✅ Initialization can be called multiple times safely
- ⚠️ Each task should manage its own result structures
- ⚠️ Don't share `enip_scanner_assembly_result_t` structures between tasks

---

## Resource Management

The ENIP Scanner API implements comprehensive resource management to prevent leaks and ensure proper cleanup.

### Automatic Resource Cleanup

**Sockets:**
- All TCP/UDP sockets are properly closed on all code paths
- Error paths include socket cleanup
- Timeout scenarios properly close sockets

**Memory:**
- All allocated memory is freed on error paths
- `enip_scanner_free_assembly_result()` safely handles NULL pointers
- Buffer allocations are bounded to prevent excessive memory usage

**Sessions:**
- Sessions are automatically unregistered on error
- Cleanup occurs even if operations fail mid-way

### Resource Management Example

```c
// Example showing proper resource management
enip_scanner_assembly_result_t result;
memset(&result, 0, sizeof(result));

esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);

// Always free resources, regardless of return value
if (ret == ESP_OK && result.success) {
    // Use data...
} else {
    // Error occurred, but resources are already cleaned up internally
    // Still call free to be safe
}

// Always call free function
enip_scanner_free_assembly_result(&result);
```

### Memory Safety Features

- **Bounds Checking**: All buffer operations include bounds checking
- **Integer Overflow Protection**: Network calculations protected against overflow
- **Buffer Overflow Protection**: String operations validated for length
- **Allocation Limits**: Maximum allocation sizes enforced

### Error Recovery

- Functions clean up partially allocated resources
- Sockets closed even if operations fail
- Memory freed even if errors occur mid-operation
- No resource leaks on timeout or network errors

---

## Complete Example

This example demonstrates a practical I/O mapping scenario where:
- Input assembly byte 0 bit 1 controls GPIO1 output
- GPIO2 input state is written to output assembly byte 0 bit 2

The example shows:
- Reading input assembly data
- Checking individual bits
- Controlling GPIO outputs based on assembly data
- Reading GPIO inputs
- Writing output assembly data based on GPIO state

```c
#include "enip_scanner.h"
#include "driver/gpio.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define GPIO_OUTPUT_PIN 1
#define GPIO_INPUT_PIN 2
#define ENIP_DEVICE_IP "172.16.82.155"
#define ENIP_INPUT_ASSEMBLY_INSTANCE 100
#define ENIP_OUTPUT_ASSEMBLY_INSTANCE 150
#define ENIP_IO_POLL_INTERVAL_MS 100

static const char *TAG = "enip_io_example";

static void enip_io_task(void *pvParameters)
{
    ip4_addr_t device_ip;
    if (inet_aton(ENIP_DEVICE_IP, &device_ip) == 0) {
        ESP_LOGE(TAG, "Invalid device IP address: %s", ENIP_DEVICE_IP);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "EtherNet/IP I/O task started");
    ESP_LOGI(TAG, "Device IP: %s", ENIP_DEVICE_IP);
    ESP_LOGI(TAG, "Input Assembly: %d, Output Assembly: %d", 
             ENIP_INPUT_ASSEMBLY_INSTANCE, ENIP_OUTPUT_ASSEMBLY_INSTANCE);

    enip_scanner_assembly_result_t output_result;
    esp_err_t init_ret = enip_scanner_read_assembly(&device_ip, ENIP_OUTPUT_ASSEMBLY_INSTANCE,
                                                     &output_result, 5000);
    uint8_t last_output_byte0 = 0;
    if (init_ret == ESP_OK && output_result.success && output_result.data_length > 0) {
        last_output_byte0 = output_result.data[0];
        enip_scanner_free_assembly_result(&output_result);
    }

    bool last_gpio2_state = false;
    int gpio2_level = gpio_get_level(GPIO_INPUT_PIN);
    last_gpio2_state = (gpio2_level == 1);

    while (1) {
        enip_scanner_assembly_result_t input_result;
        esp_err_t ret = enip_scanner_read_assembly(&device_ip, ENIP_INPUT_ASSEMBLY_INSTANCE, 
                                                     &input_result, 5000);

        if (ret == ESP_OK && input_result.success && input_result.data_length > 0) {
            uint8_t input_byte0 = input_result.data[0];
            bool input_bit1 = (input_byte0 & 0x02) != 0;

            if (input_bit1) {
                gpio_set_level(GPIO_OUTPUT_PIN, 1);
            } else {
                gpio_set_level(GPIO_OUTPUT_PIN, 0);
            }

            gpio2_level = gpio_get_level(GPIO_INPUT_PIN);
            bool gpio2_state = (gpio2_level == 1);

            if (gpio2_state != last_gpio2_state) {
                uint8_t output_data[4] = {0};
                output_data[0] = last_output_byte0;
                
                if (gpio2_state) {
                    output_data[0] |= 0x04;
                } else {
                    output_data[0] &= ~0x04;
                }

                char error_msg[128];
                esp_err_t write_ret = enip_scanner_write_assembly(&device_ip, 
                                                                   ENIP_OUTPUT_ASSEMBLY_INSTANCE,
                                                                   output_data, 4, 5000, error_msg);

                if (write_ret == ESP_OK) {
                    last_output_byte0 = output_data[0];
                    last_gpio2_state = gpio2_state;
                    ESP_LOGI(TAG, "Wrote output assembly: byte0=0x%02X (GPIO2=%d)", 
                             output_data[0], gpio2_state);
                } else {
                    ESP_LOGW(TAG, "Failed to write output assembly: %s", error_msg);
                }
            }

            enip_scanner_free_assembly_result(&input_result);
        } else {
            if (ret == ESP_OK) {
                ESP_LOGW(TAG, "Failed to read input assembly: %s", input_result.error_message);
            } else {
                ESP_LOGW(TAG, "Failed to read input assembly: %s", esp_err_to_name(ret));
            }
            // Always free result even on error
            enip_scanner_free_assembly_result(&input_result);
        }

        vTaskDelay(pdMS_TO_TICKS(ENIP_IO_POLL_INTERVAL_MS));
    }
}

void setup_gpio_and_start_task(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_OUTPUT_PIN);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_INPUT_PIN);
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    gpio_set_level(GPIO_OUTPUT_PIN, 0);

    xTaskCreate(enip_io_task, "enip_io_task", 4096, NULL, 5, NULL);
}
```

**Explanation**:

1. **Initialization**: The task reads the current output assembly state to preserve existing bits when writing.

2. **Input Processing**: Every 100ms, reads the input assembly and checks bit 1 of byte 0. Sets GPIO1 HIGH if bit 1 is set, LOW otherwise.

3. **Output Processing**: Monitors GPIO2 input. When GPIO2 state changes, updates bit 2 of output assembly byte 0 and writes it to the device.

4. **Bit Manipulation**:
   - `input_byte0 & 0x02` checks if bit 1 is set (0x02 = bit 1)
   - `output_data[0] |= 0x04` sets bit 2 (0x04 = bit 2)
   - `output_data[0] &= ~0x04` clears bit 2

5. **Error Handling**: Logs errors but continues operation, allowing the system to recover from temporary network issues.

---

## Notes

1. **Memory Management**: 
   - Always free assembly result data using `enip_scanner_free_assembly_result()`
   - Call free function even if operation failed or `success` is false
   - The function safely handles NULL pointers and multiple calls

2. **Thread Safety**: 
   - ✅ **The API is fully thread-safe** - All functions use internal mutex protection
   - Safe to call from multiple FreeRTOS tasks concurrently
   - No additional synchronization required from application code
   - Initialization state is protected with mutexes

3. **Session Management**: 
   - Sessions are automatically managed for read/write operations
   - Explicit session management is optional
   - All sockets are properly closed even on error

4. **Assembly Instances**: 
   - Common assembly instances are 100 (input), 150 (output), and 20 (configuration)
   - Use `enip_scanner_discover_assemblies()` to find available instances
   - Discovery function includes proper resource cleanup

5. **Bit Numbering**: 
   - Bits are numbered 0-7 from right to left (LSB to MSB)
   - Bit 0 is the rightmost bit, bit 7 is the leftmost bit

6. **Network Requirements**: 
   - The device must have a valid IP address and network connectivity before using the scanner API
   - Network interface must be up and configured

7. **Error Recovery**: 
   - All functions properly clean up resources on error
   - Socket leaks are prevented through proper error handling
   - Memory leaks are prevented through consistent resource management

8. **Security**: 
   - Integer overflow protection in network calculations
   - Buffer overflow protection in string operations
   - Bounds checking for all network data parsing

9. **Tag Support (Experimental)**: 
   - Tag support is experimental and designed for Micro800 series PLCs
   - Must be enabled via Kconfig (`CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT`)
   - Tag names must be known in advance (no tag discovery)
   - Supports basic data types: BOOL, SINT, INT, DINT, REAL, STRING
   - Web UI available at `/tags` when enabled

10. **Component Usage**: 
   - Component can be used locally (in `components/` directory)
   - Can be distributed via Git dependency
   - See `README.md` for component integration details

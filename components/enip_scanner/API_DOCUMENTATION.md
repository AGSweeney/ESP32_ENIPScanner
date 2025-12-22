# EtherNet/IP Scanner API Documentation

Complete API reference for the EtherNet/IP Scanner component.

## Table of Contents

1. [Introduction](#introduction)
2. [Initialization](#initialization)
3. [Device Discovery](#device-discovery)
4. [Assembly Operations](#assembly-operations)
5. [Tag Operations](#tag-operations)
6. [Data Structures](#data-structures)
7. [Error Handling](#error-handling)
8. [Thread Safety](#thread-safety)
9. [Resource Management](#resource-management)
10. [Complete Examples](#complete-examples)

---

## Introduction

The EtherNet/IP Scanner component provides explicit messaging capabilities for communicating with EtherNet/IP devices over TCP/IP networks. It supports device discovery, assembly data read/write, and tag-based communication for Micro800 series PLCs.

**Protocol Support:**
- EtherNet/IP explicit messaging (TCP port 44818)
- CIP (Common Industrial Protocol) services
- UDP device discovery (List Identity)

**Supported Operations:**
- Device discovery via UDP broadcast
- Assembly data read/write
- Tag read/write (Micro800 series, experimental)
- Session management

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

**Usage:**
```c
esp_err_t ret = enip_scanner_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize scanner: %s", esp_err_to_name(ret));
    return;
}
```

**Important Notes:**
- Call after network initialization (after receiving IP address)
- Idempotent - safe to call multiple times
- Thread-safe - can be called from any task
- Creates internal mutex for thread synchronization

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
enip_scanner_device_info_t devices[10];
int count = enip_scanner_scan_devices(devices, 10, 5000);

ESP_LOGI(TAG, "Found %d device(s)", count);
for (int i = 0; i < count; i++) {
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&devices[i].ip_address));
    ESP_LOGI(TAG, "  %s - %s (Vendor: 0x%04X, Type: 0x%04X)", 
             ip_str, devices[i].product_name, 
             devices[i].vendor_id, devices[i].device_type);
}
```

**Behavior:**
- Broadcasts UDP List Identity request to network broadcast address
- Collects responses from all devices on the network
- Automatically limits scan range to prevent excessive traffic
- Thread-safe - can be called concurrently

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
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);

enip_scanner_assembly_result_t result;
memset(&result, 0, sizeof(result));

esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);

if (ret == ESP_OK && result.success) {
    ESP_LOGI(TAG, "Read %d bytes from assembly %d", 
             result.data_length, result.assembly_instance);
    
    // Access individual bytes
    if (result.data_length > 0) {
        uint8_t byte0 = result.data[0];
        bool bit0 = (byte0 & 0x01) != 0;  // Check bit 0
        bool bit1 = (byte0 & 0x02) != 0;  // Check bit 1
        
        ESP_LOGI(TAG, "Byte 0: 0x%02X (bit0=%d, bit1=%d)", byte0, bit0, bit1);
    }
    
    enip_scanner_free_assembly_result(&result);
} else {
    ESP_LOGE(TAG, "Read failed: %s", result.error_message);
    enip_scanner_free_assembly_result(&result);
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

enip_scanner_write_assembly(&device_ip, 150, output_data, 4, 5000, NULL);
```

**Important:**
- Not all assemblies are writable - check before writing
- Thread-safe - can be called concurrently
- All resources cleaned up on error

### `enip_scanner_discover_assemblies()`

Discover valid assembly instances for a device.

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
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);

uint16_t instances[32];
int count = enip_scanner_discover_assemblies(&device_ip, instances, 32, 5000);

ESP_LOGI(TAG, "Found %d assembly instance(s)", count);
for (int i = 0; i < count; i++) {
    ESP_LOGI(TAG, "  Instance %d", instances[i]);
}
```

### `enip_scanner_is_assembly_writable()`

Check if an assembly instance is writable.

**Prototype:**
```c
bool enip_scanner_is_assembly_writable(const ip4_addr_t *ip_address, 
                                        uint16_t assembly_instance, 
                                        uint32_t timeout_ms);
```

**Returns:**
- `true` - Assembly is readable (assumed writable)
- `false` - Assembly not readable or error occurred

**Example:**
```c
if (enip_scanner_is_assembly_writable(&device_ip, 150, 5000)) {
    ESP_LOGI(TAG, "Assembly 150 is writable");
} else {
    ESP_LOGE(TAG, "Assembly 150 is not writable");
}
```

### `enip_scanner_free_assembly_result()`

Free memory allocated for assembly read result.

**Prototype:**
```c
void enip_scanner_free_assembly_result(enip_scanner_assembly_result_t *result);
```

**Usage:**
```c
enip_scanner_assembly_result_t result;
// ... read assembly ...
enip_scanner_free_assembly_result(&result);  // Always call this
```

**Important:**
- Safe to call with NULL pointer
- Safe to call multiple times
- Always call even if read failed

---

## Tag Operations

Tag support enables reading and writing PLC tags using symbolic names instead of assembly instances. This feature is **experimental** and designed specifically for Allen-Bradley Micro800 series PLCs.

### Enabling Tag Support

1. Run `idf.py menuconfig`
2. Navigate to: **Component config** → **EtherNet/IP Scanner Configuration**
3. Enable: **"Enable Allen-Bradley tag support"**
4. Rebuild project

### Supported Data Types

- `CIP_DATA_TYPE_BOOL` (0xC1) - Boolean (1 byte)
- `CIP_DATA_TYPE_SINT` (0xC2) - Signed 8-bit integer
- `CIP_DATA_TYPE_INT` (0xC3) - Signed 16-bit integer
- `CIP_DATA_TYPE_DINT` (0xC4) - Signed 32-bit integer
- `CIP_DATA_TYPE_REAL` (0xCA) - IEEE 754 single precision float
- `CIP_DATA_TYPE_STRING` (0xDA) - String (variable length)

### `enip_scanner_read_tag()`

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
- `tag_path` - Tag path/name (e.g., `"MyTag"`, `"MyArray[0]"`, `"MyStruct.Field"`)
- `result` - Pointer to result structure (caller must free `result->data`)
- `timeout_ms` - Operation timeout (milliseconds)

**Returns:**
- `ESP_OK` - Operation completed (check `result->success`)
- `ESP_ERR_INVALID_ARG` - Invalid parameters
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_FAIL` - General failure

**Tag Path Format:**
- `"MyTag"` - Simple tag
- `"MyArray[0]"` - First element of array
- `"MyArray[5]"` - Sixth element (0-indexed)
- `"MyStruct.Field"` - Field in structure

**Note:** Micro800 PLCs do not support program-scoped tags - tags must be in the global variable table.

**Example - Reading Different Data Types:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);
enip_scanner_tag_result_t result;

// Read BOOL tag
memset(&result, 0, sizeof(result));
if (enip_scanner_read_tag(&device_ip, "StartButton", &result, 5000) == ESP_OK && result.success) {
    if (result.cip_data_type == CIP_DATA_TYPE_BOOL && result.data_length >= 1) {
        bool pressed = (result.data[0] != 0);
        ESP_LOGI(TAG, "StartButton = %s", pressed ? "PRESSED" : "RELEASED");
    }
    enip_scanner_free_tag_result(&result);
}

// Read DINT tag
memset(&result, 0, sizeof(result));
if (enip_scanner_read_tag(&device_ip, "Counter", &result, 5000) == ESP_OK && result.success) {
    if (result.cip_data_type == CIP_DATA_TYPE_DINT && result.data_length >= 4) {
        int32_t value = (int32_t)(result.data[0] | (result.data[1] << 8) | 
                                  (result.data[2] << 16) | (result.data[3] << 24));
        ESP_LOGI(TAG, "Counter = %ld", value);
    }
    enip_scanner_free_tag_result(&result);
}

// Read REAL tag
memset(&result, 0, sizeof(result));
if (enip_scanner_read_tag(&device_ip, "Temperature", &result, 5000) == ESP_OK && result.success) {
    if (result.cip_data_type == CIP_DATA_TYPE_REAL && result.data_length >= 4) {
        union { uint32_t i; float f; } u;
        u.i = (result.data[0] | (result.data[1] << 8) | 
               (result.data[2] << 16) | (result.data[3] << 24));
        ESP_LOGI(TAG, "Temperature = %.2f°C", u.f);
    }
    enip_scanner_free_tag_result(&result);
}
#endif
```

### `enip_scanner_write_tag()`

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
- `tag_path` - Tag path/name
- `data` - Data buffer (little-endian byte order)
- `data_length` - Length of data in bytes
- `cip_data_type` - CIP data type code (e.g., `CIP_DATA_TYPE_DINT`)
- `timeout_ms` - Operation timeout (milliseconds)
- `error_message` - Buffer for error message (128 bytes, can be NULL)

**Returns:**
- `ESP_OK` - Write successful
- `ESP_ERR_INVALID_ARG` - Invalid parameters
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_FAIL` - General failure

**Example - Writing Different Data Types:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);
char error_msg[128];

// Write BOOL tag
uint8_t bool_value = 1;  // true
esp_err_t ret = enip_scanner_write_tag(&device_ip, "Output1", &bool_value, 1, 
                                      CIP_DATA_TYPE_BOOL, 5000, error_msg);

// Write DINT tag
int32_t dint_value = 12345;
uint8_t dint_bytes[4];
dint_bytes[0] = dint_value & 0xFF;
dint_bytes[1] = (dint_value >> 8) & 0xFF;
dint_bytes[2] = (dint_value >> 16) & 0xFF;
dint_bytes[3] = (dint_value >> 24) & 0xFF;

ret = enip_scanner_write_tag(&device_ip, "Setpoint", dint_bytes, 4, 
                             CIP_DATA_TYPE_DINT, 5000, error_msg);

// Write REAL tag
float real_value = 75.5f;
union { uint32_t i; float f; } u;
u.f = real_value;
uint8_t real_bytes[4];
real_bytes[0] = u.i & 0xFF;
real_bytes[1] = (u.i >> 8) & 0xFF;
real_bytes[2] = (u.i >> 16) & 0xFF;
real_bytes[3] = (u.i >> 24) & 0xFF;

ret = enip_scanner_write_tag(&device_ip, "TempSetpoint", real_bytes, 4, 
                             CIP_DATA_TYPE_REAL, 5000, error_msg);
#endif
```

**Important:**
- Data must be in little-endian byte order
- Data length must match expected size for data type
- Thread-safe - can be called concurrently

### `enip_scanner_get_data_type_name()`

Get human-readable name for a CIP data type code.

**Prototype:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
const char *enip_scanner_get_data_type_name(uint16_t cip_data_type);
#endif
```

**Returns:**
- Human-readable string (e.g., `"DINT"`, `"BOOL"`, `"REAL"`)
- `"UNKNOWN"` for unrecognized types

**Example:**
```c
const char *type_name = enip_scanner_get_data_type_name(CIP_DATA_TYPE_DINT);
ESP_LOGI(TAG, "Data type: %s", type_name);  // Prints "DINT"
```

### `enip_scanner_free_tag_result()`

Free memory allocated for tag read result.

**Prototype:**
```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
void enip_scanner_free_tag_result(enip_scanner_tag_result_t *result);
#endif
```

**Usage:**
```c
enip_scanner_tag_result_t result;
// ... read tag ...
enip_scanner_free_tag_result(&result);  // Always call this
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

**Memory Management:**
- `data` is allocated by `enip_scanner_read_assembly()`
- Must be freed using `enip_scanner_free_assembly_result()`
- Always free even if `success` is false

### `enip_scanner_tag_result_t`

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

**Memory Management:**
- `data` is allocated by `enip_scanner_read_tag()`
- Must be freed using `enip_scanner_free_tag_result()`
- Always free even if `success` is false

---

## Error Handling

### Error Codes

All functions return `esp_err_t` error codes:

- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid parameters (NULL pointer, invalid IP, etc.)
- `ESP_ERR_INVALID_STATE` - Scanner not initialized
- `ESP_ERR_INVALID_RESPONSE` - Invalid response from device
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_ERR_NO_MEM` - Memory allocation failed
- `ESP_FAIL` - General failure

### Error Message Retrieval

**For functions returning result structures:**
- Check `result->error_message` for detailed error description
- Example: `enip_scanner_read_assembly()`, `enip_scanner_read_tag()`

**For functions with error_message parameter:**
- Check `error_message` buffer after function returns
- Example: `enip_scanner_write_assembly()`, `enip_scanner_write_tag()`

### Error Handling Best Practices

```c
// Always check return value
esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);

if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
    enip_scanner_free_assembly_result(&result);
    return;
}

// Check success flag even when return value is ESP_OK
if (!result.success) {
    ESP_LOGE(TAG, "Read failed: %s", result.error_message);
    enip_scanner_free_assembly_result(&result);
    return;
}

// Use data...
// Always free resources
enip_scanner_free_assembly_result(&result);
```

---

## Thread Safety

The ENIP Scanner API is **fully thread-safe** and can be safely called from multiple FreeRTOS tasks concurrently.

### Implementation

- **Mutex Protection**: All API functions use internal FreeRTOS mutex (`s_scanner_mutex`)
- **Initialization State**: Protected and checked atomically
- **Network Interface Access**: Synchronized to prevent race conditions
- **No External Synchronization Required**: Application code does not need mutexes

### Concurrent Usage

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

// Both tasks can run concurrently
xTaskCreate(scan_task, "scan", 4096, NULL, 5, NULL);
xTaskCreate(read_task, "read", 4096, NULL, 5, NULL);
```

**Best Practices:**
- ✅ Safe to call API functions from multiple tasks
- ✅ No need for application-level mutexes
- ⚠️ Each task should manage its own result structures
- ⚠️ Don't share result structures between tasks

---

## Resource Management

The API implements comprehensive resource management to prevent leaks.

### Automatic Cleanup

**Sockets:**
- All TCP/UDP sockets closed on all code paths
- Error paths include socket cleanup
- Timeout scenarios properly close sockets

**Memory:**
- All allocated memory freed on error paths
- Free functions safely handle NULL pointers
- Buffer allocations bounded to prevent excessive usage

**Sessions:**
- Sessions automatically unregistered on error
- Cleanup occurs even if operations fail mid-way

### Memory Safety Features

- **Bounds Checking**: All buffer operations include bounds checking
- **Integer Overflow Protection**: Network calculations protected
- **Buffer Overflow Protection**: String operations validated
- **Allocation Limits**: Maximum allocation sizes enforced

### Resource Management Example

```c
enip_scanner_assembly_result_t result;
memset(&result, 0, sizeof(result));

esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);

// Always free resources, regardless of return value
if (ret == ESP_OK && result.success) {
    // Use data...
} else {
    // Error occurred, but resources are already cleaned up internally
}

// Always call free function
enip_scanner_free_assembly_result(&result);
```

---

## Complete Examples

### Example 1: Basic I/O Mapping

Read input assembly and control GPIO based on bit state, write GPIO state to output assembly.

```c
#include "enip_scanner.h"
#include "driver/gpio.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GPIO_OUTPUT_PIN 1
#define GPIO_INPUT_PIN 2
#define DEVICE_IP "192.168.1.100"
#define INPUT_ASSEMBLY 100
#define OUTPUT_ASSEMBLY 150

static void enip_io_task(void *pvParameters)
{
    ip4_addr_t device_ip;
    inet_aton(DEVICE_IP, &device_ip);
    
    while (1) {
        // Read input assembly
        enip_scanner_assembly_result_t result;
        memset(&result, 0, sizeof(result));
        
        if (enip_scanner_read_assembly(&device_ip, INPUT_ASSEMBLY, &result, 5000) == ESP_OK && result.success) {
            if (result.data_length > 0) {
                uint8_t input_byte = result.data[0];
                bool input_bit1 = (input_byte & 0x02) != 0;  // Check bit 1
                
                // Control GPIO based on input
                gpio_set_level(GPIO_OUTPUT_PIN, input_bit1 ? 1 : 0);
                
                // Read GPIO input and write to output assembly
                int gpio_level = gpio_get_level(GPIO_INPUT_PIN);
                uint8_t output_data[4] = {0};
                if (gpio_level == 1) {
                    output_data[0] |= 0x04;  // Set bit 2
                }
                
                char error_msg[128];
                enip_scanner_write_assembly(&device_ip, OUTPUT_ASSEMBLY, output_data, 4, 5000, error_msg);
            }
            enip_scanner_free_assembly_result(&result);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### Example 2: Tag-Based Control

Read tag values and control outputs based on tag state.

```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
#include "enip_scanner.h"

static void tag_control_task(void *pvParameters)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    while (1) {
        enip_scanner_tag_result_t result;
        memset(&result, 0, sizeof(result));
        
        // Read counter tag
        if (enip_scanner_read_tag(&device_ip, "Counter", &result, 5000) == ESP_OK && result.success) {
            if (result.cip_data_type == CIP_DATA_TYPE_DINT && result.data_length >= 4) {
                int32_t count = (int32_t)(result.data[0] | (result.data[1] << 8) | 
                                          (result.data[2] << 16) | (result.data[3] << 24));
                
                ESP_LOGI(TAG, "Counter = %ld", count);
                
                // Write output tag if counter exceeds threshold
                if (count > 1000) {
                    uint8_t bool_value = 1;
                    char error_msg[128];
                    enip_scanner_write_tag(&device_ip, "AlarmOutput", &bool_value, 1, 
                                          CIP_DATA_TYPE_BOOL, 5000, error_msg);
                }
            }
            enip_scanner_free_tag_result(&result);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif
```

---

## Limitations and Notes

1. **Tag Support**: Experimental feature for Micro800 series PLCs only
2. **Tag Discovery**: Tag names must be known in advance - no tag discovery capability
3. **Data Types**: Limited to basic types (BOOL, SINT, INT, DINT, REAL, STRING)
4. **Program-Scoped Tags**: Micro800 does not support program-scoped tags externally
5. **Assembly Instances**: Common instances are 100 (input), 150 (output), 20 (configuration)
6. **Network Requirements**: Device must have valid IP address and network connectivity
7. **Bit Numbering**: Bits numbered 0-7 from right to left (LSB to MSB)
8. **Web Interface**: Available at ESP32's IP address when enabled

---

## Additional Resources

- See `README.md` for component integration and quick start
- Check ESP-IDF documentation for network configuration
- EtherNet/IP specification for protocol details
- Allen-Bradley documentation for Micro800 tag conventions

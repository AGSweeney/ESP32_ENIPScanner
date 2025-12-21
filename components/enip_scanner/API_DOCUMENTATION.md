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
- Thread-safe operations

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Initialization](#initialization)
3. [Device Discovery](#device-discovery)
4. [Reading Assembly Data](#reading-assembly-data)
5. [Writing Assembly Data](#writing-assembly-data)
6. [Assembly Discovery](#assembly-discovery)
7. [Session Management](#session-management)
8. [Data Structures](#data-structures)
9. [Error Handling](#error-handling)
10. [Complete Example](#complete-example)

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

**Note:** This function should be called after network initialization (e.g., after receiving IP address).

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

**Note:** This function uses UDP broadcast and may take several seconds to complete. The timeout applies to the entire scan operation, not per device.

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
- Always free the result even if `result->success` is false
- Do not access `result->data` after freeing

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

**Note:** Not all assemblies are writable. Use `enip_scanner_is_assembly_writable()` to check before writing.

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

**Note:** Session management is optional for basic read/write operations. The API automatically manages sessions internally. Use these functions only if you need explicit session control.

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

**Important:** Always call `enip_scanner_free_assembly_result()` to free `data` after use.

---

## Error Handling

All functions return `esp_err_t` error codes. Common error codes:

- `ESP_OK` - Operation successful
- `ESP_ERR_INVALID_ARG` - Invalid parameters (NULL pointer, invalid IP, etc.)
- `ESP_ERR_INVALID_RESPONSE` - Invalid response from device
- `ESP_ERR_TIMEOUT` - Operation timed out
- `ESP_ERR_NO_MEM` - Memory allocation failed
- `ESP_FAIL` - General failure

**Error Message Retrieval:**
- For `enip_scanner_read_assembly()`: Check `result->error_message`
- For `enip_scanner_write_assembly()`: Check `error_message` parameter
- For `enip_scanner_register_session()`: Check `error_message` parameter

**Example Error Handling:**
```c
enip_scanner_assembly_result_t result;
esp_err_t ret = enip_scanner_read_assembly(&target_ip, 100, &result, 5000);

if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
    return;
}

if (!result.success) {
    ESP_LOGE(TAG, "Read failed: %s", result.error_message);
    enip_scanner_free_assembly_result(&result);
    return;
}

// Use result.data...
enip_scanner_free_assembly_result(&result);
```

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

1. **Memory Management**: Always free assembly result data using `enip_scanner_free_assembly_result()`.

2. **Thread Safety**: The API is not thread-safe. Use appropriate synchronization (mutexes, semaphores) if accessing from multiple tasks.

3. **Session Management**: Sessions are automatically managed for read/write operations. Explicit session management is optional.

4. **Assembly Instances**: Common assembly instances are 100 (input), 150 (output), and 20 (configuration). Use `enip_scanner_discover_assemblies()` to find available instances.

5. **Bit Numbering**: Bits are numbered 0-7 from right to left (LSB to MSB). Bit 0 is the rightmost bit, bit 7 is the leftmost bit.

6. **Network Requirements**: The device must have a valid IP address and network connectivity before using the scanner API.

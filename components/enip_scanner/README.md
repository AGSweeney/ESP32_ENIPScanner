# EtherNet/IP Scanner Component

[![GitHub](https://img.shields.io/badge/GitHub-Repository-blue)](https://github.com/AGSweeney/ESP32_ENIPScanner)

EtherNet/IP scanner component for ESP-IDF that provides explicit messaging capabilities for communicating with industrial EtherNet/IP devices over TCP/IP networks.

![EtherNet/IP Scanner Web Interface](ESP32-ENIPScanner.png)

## Project Status

**Current Version:** Full feature set implemented

**Status:** ✅ Stable - All core features implemented

### Implemented Features

- ✅ **Device Discovery** - UDP broadcast-based network scanning
- ✅ **Assembly I/O** - Read/write assembly data via explicit messaging
- ✅ **Assembly Discovery** - Automatic discovery of valid assembly instances
- ✅ **Tag Support** - Read/write tags on Micro800 PLCs using symbolic names (20 CIP data types)
- ✅ **Motoman Robot Support** - Complete support for all 18 Motoman CIP classes (alarms, status, position, I/O, variables, registers)
- ✅ **Session Management** - EtherNet/IP session registration/unregistration
- ✅ **Thread Safety** - All operations protected with mutexes
- ✅ **Web Interface** - Built-in HTTP server for device management
- ✅ **Memory Safety** - Comprehensive resource cleanup and leak prevention
- ✅ **Error Handling** - Detailed error messages and status codes

### Architecture

The component has been refactored into a modular structure:

- **`enip_scanner.c`** - Core ENIP/CIP protocol implementation
- **`enip_scanner_tag.c`** - Tag read/write operations (conditionally compiled)
- **`enip_scanner_tag_data.c`** - Data type encoder/decoder handlers
- **`enip_scanner_tag_internal.h`** - Internal shared functions and types

This modular design makes it easy to add new data types and maintain the codebase.

## Overview

This component enables ESP32 devices to communicate with EtherNet/IP devices (such as Allen-Bradley PLCs) using explicit messaging. It supports device discovery, assembly data read/write operations, and tag-based communication for Micro800 series PLCs.

**Key Capabilities:**
- **Device Discovery**: Scan network for EtherNet/IP devices via UDP broadcast
- **Assembly I/O**: Read and write assembly data using explicit messaging
- **Tag Support**: Read and write tags on Micro800 PLCs using symbolic names (20 CIP data types)
- **Motoman Robot Support**: Read robot status, alarms, I/O signals, variables, and registers
- **Thread-Safe**: All operations protected with mutexes for concurrent access
- **Memory-Safe**: Comprehensive resource cleanup and leak prevention
- **Error Handling**: Detailed error messages and status codes

## Quick Start

### 1. Add to Your Project

Add this component to your ESP-IDF project's `components` directory, or add as a managed component via `idf_component.yml`:

```yaml
dependencies:
  enip_scanner:
    path: components/enip_scanner
```

### 2. Initialize the Component

```c
#include "enip_scanner.h"

void app_main(void)
{
    // Wait for network to be ready
    // ... (WiFi/Ethernet initialization code) ...
    
    // Initialize the scanner (call once after network is up)
    esp_err_t ret = enip_scanner_init();
    if (ret != ESP_OK) {
        ESP_LOGE("app", "Failed to initialize scanner: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI("app", "EtherNet/IP Scanner initialized");
}
```

### 3. Discover Devices

```c
#include "lwip/inet.h"

void discover_devices(void)
{
    enip_scanner_device_info_t devices[32];
    int count = enip_scanner_scan_devices(devices, 32, 5000);
    
    ESP_LOGI("app", "Found %d device(s)", count);
    for (int i = 0; i < count; i++) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&devices[i].ip_address));
        ESP_LOGI("app", "  %s - %s (Vendor: 0x%04X, Type: 0x%04X)", 
                 ip_str, devices[i].product_name, 
                 devices[i].vendor_id, devices[i].device_type);
    }
}
```

### 4. Read Assembly Data

```c
#include "lwip/inet.h"

void read_assembly_example(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    enip_scanner_assembly_result_t result;
    memset(&result, 0, sizeof(result));
    
    esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);
    if (ret == ESP_OK && result.success) {
        ESP_LOGI("app", "Read %d bytes from assembly %d", 
                 result.data_length, result.assembly_instance);
        
        // Access individual bytes
        if (result.data_length > 0) {
            uint8_t byte0 = result.data[0];
            bool bit0 = (byte0 & 0x01) != 0;  // Check bit 0
            ESP_LOGI("app", "Byte 0: 0x%02X (bit0=%d)", byte0, bit0);
        }
        
        // Always free the result
        enip_scanner_free_assembly_result(&result);
    } else {
        ESP_LOGE("app", "Read failed: %s", result.error_message);
        enip_scanner_free_assembly_result(&result);
    }
}
```

### 5. Write Assembly Data

```c
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
        ESP_LOGI("app", "Write successful");
    } else {
        ESP_LOGE("app", "Write failed: %s", error_msg);
    }
}
```

## Tag Support (Micro800 Series)

Tag support allows reading and writing PLC tags using symbolic names instead of assembly instances. This feature is designed specifically for Allen-Bradley Micro800 series PLCs.

### Enabling Tag Support

1. Run `idf.py menuconfig`
2. Navigate to: **Component config** → **EtherNet/IP Scanner Configuration**
3. Enable: **"Enable Allen-Bradley tag support"**
4. Rebuild your project

### Reading Tags

```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
#include "lwip/inet.h"

void read_tag_example(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    enip_scanner_tag_result_t result;
    memset(&result, 0, sizeof(result));
    
    // Read a DINT tag
    esp_err_t ret = enip_scanner_read_tag(&device_ip, "Counter", &result, 5000);
    if (ret == ESP_OK && result.success) {
        if (result.cip_data_type == CIP_DATA_TYPE_DINT && result.data_length == 4) {
            // Extract DINT value (little-endian)
            int32_t value = (int32_t)(result.data[0] | 
                                      (result.data[1] << 8) | 
                                      (result.data[2] << 16) | 
                                      (result.data[3] << 24));
            ESP_LOGI("app", "Counter = %ld", value);
        }
        enip_scanner_free_tag_result(&result);
    } else {
        ESP_LOGE("app", "Read failed: %s", result.error_message);
        enip_scanner_free_tag_result(&result);
    }
}
#endif
```

### Writing Tags

```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
#include "lwip/inet.h"
#include <string.h>

void write_tag_example(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    char error_msg[128];
    
    // Write a BOOL tag
    uint8_t bool_value = 1;  // true
    esp_err_t ret = enip_scanner_write_tag(&device_ip, "Output1", &bool_value, 1, 
                                           CIP_DATA_TYPE_BOOL, 5000, error_msg);
    if (ret != ESP_OK) {
        ESP_LOGE("app", "Write BOOL failed: %s", error_msg);
    }
    
    // Write a DINT tag
    int32_t dint_value = 12345;
    uint8_t dint_bytes[4];
    dint_bytes[0] = dint_value & 0xFF;
    dint_bytes[1] = (dint_value >> 8) & 0xFF;
    dint_bytes[2] = (dint_value >> 16) & 0xFF;
    dint_bytes[3] = (dint_value >> 24) & 0xFF;
    
    ret = enip_scanner_write_tag(&device_ip, "Setpoint", dint_bytes, 4, 
                                 CIP_DATA_TYPE_DINT, 5000, error_msg);
    if (ret != ESP_OK) {
        ESP_LOGE("app", "Write DINT failed: %s", error_msg);
    }
    
    // Write a STRING tag
    const char *str_value = "Hello, PLC!";
    uint8_t str_bytes[256];
    size_t str_len = strlen(str_value);
    if (str_len > 255) str_len = 255;  // Enforce max length
    memcpy(str_bytes, str_value, str_len);  // Copy string bytes (no null terminator)
    
    ret = enip_scanner_write_tag(&device_ip, "Message", str_bytes, str_len, 
                                 CIP_DATA_TYPE_STRING, 5000, error_msg);
    if (ret != ESP_OK) {
        ESP_LOGE("app", "Write STRING failed: %s", error_msg);
    }
}
#endif
```

**Tag Path Format:**
- `"MyTag"` - Simple tag
- `"MyArray[0]"` - Array element
- `"MyStruct.Field"` - Structure field (use dot notation)

**Important Notes:** 
- Micro800 PLCs do not support program-scoped tags - tags must be in the global variable table
- STRING tags have a maximum length of 255 characters (1-byte length prefix limitation)
- STRING data format: `[Length (1 byte)] [String bytes]` - length prefix is handled automatically
- Tag names are case-sensitive and must match exactly
- **API vs Web UI**: The API supports all 20 data types, while the web UI supports only 6 types (BOOL, SINT, INT, DINT, REAL, STRING)

## Supported Data Types

### API Support (Complete)

The API (`enip_scanner_read_tag()` and `enip_scanner_write_tag()`) supports **all 20 CIP data types** listed below:

| Data Type | Code | Size | Description |
|-----------|------|------|-------------|
| BOOL | 0xC1 | 1 byte | Boolean (0 or 1) |
| SINT | 0xC2 | 1 byte | Signed 8-bit integer |
| INT | 0xC3 | 2 bytes | Signed 16-bit integer |
| DINT | 0xC4 | 4 bytes | Signed 32-bit integer |
| LINT | 0xC5 | 8 bytes | Signed 64-bit integer |
| USINT | 0xC6 | 1 byte | Unsigned 8-bit integer |
| UINT | 0xC7 | 2 bytes | Unsigned 16-bit integer |
| UDINT | 0xC8 | 4 bytes | Unsigned 32-bit integer |
| ULINT | 0xC9 | 8 bytes | Unsigned 64-bit integer |
| REAL | 0xCA | 4 bytes | IEEE 754 single precision float |
| LREAL | 0xCB | 8 bytes | IEEE 754 double precision float |
| TIME | 0xCC | 4 bytes | Time (milliseconds) - Note: Called "TIME" on Micro800, "STIME" in CIP spec |
| DATE | 0xCD | 2 bytes | Date (days since 1970-01-01) |
| TIME_OF_DAY | 0xCE | 4 bytes | Time of day (milliseconds since midnight) |
| DATE_AND_TIME | 0xCF | 8 bytes | Date and time (combined) |
| STRING | 0xDA | Variable | String (max 255 chars, 1-byte length prefix) |
| BYTE | 0xD1 | 1 byte | 8-bit bit string |
| WORD | 0xD2 | 2 bytes | 16-bit bit string |
| DWORD | 0xD3 | 4 bytes | 32-bit bit string |
| LWORD | 0xD4 | 8 bytes | 64-bit bit string |

### Web UI Support (Limited)

The web interface currently supports **6 data types** for tag writing:
- BOOL (0xC1)
- SINT (0xC2)
- INT (0xC3)
- DINT (0xC4)
- REAL (0xCA)
- STRING (0xDA)

**Note:** To use the remaining 14 data types (LINT, USINT, UINT, UDINT, ULINT, LREAL, TIME, DATE, TIME_OF_DAY, DATE_AND_TIME, BYTE, WORD, DWORD, LWORD), you must use the API directly. See the [API Documentation](API_DOCUMENTATION.md) for complete examples of all supported types.

## Motoman Robot Support (Optional)

The component includes support for Motoman DX200/YRC1000 robot controllers via vendor-specific CIP classes. This allows reading robot status, alarms, I/O signals, variables, and registers.

### Enabling Motoman Support

1. Run `idf.py menuconfig`
2. Navigate to: **Component config** → **EtherNet/IP Scanner Configuration**
3. Enable: **"Enable Motoman robot CIP class support"**
4. Rebuild your project

### Reading Robot Status

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
        
        ESP_LOGI("app", "Robot Status - Running: %d, Error: %d, Servo On: %d",
                 running, error, servo_on);
    } else {
        ESP_LOGE("app", "Failed to read status: %s", status.error_message);
    }
}
#endif
```

### Reading/Writing I/O Signals

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
        ESP_LOGI("app", "General Input 1: %d", input_value);
    }
    
    // Write General Output 1001
    uint8_t output_value = 1;
    ret = enip_scanner_motoman_write_io(&robot_ip, 1001, output_value, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI("app", "General Output 1001 written");
    }
}
#endif
```

### Reading/Writing Variables

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
        ESP_LOGI("app", "R[0] = %.2f", r_value);
    }
    
    // Write Integer variable I[0]
    int16_t i_value = 100;
    ret = enip_scanner_motoman_write_variable_i(&robot_ip, 0, i_value, 5000, error_msg);
    if (ret == ESP_OK) {
        ESP_LOGI("app", "I[0] written");
    }
}
#endif
```

**Supported Motoman Functions (All 18 Classes):**

**Alarms:**
- `enip_scanner_motoman_read_alarm()` - Read current alarm (Class 0x70)
- `enip_scanner_motoman_read_alarm_history()` - Read alarm history (Class 0x71)

**Status and Information:**
- `enip_scanner_motoman_read_status()` - Read robot status (Class 0x72)
- `enip_scanner_motoman_read_job_info()` - Read active job information (Class 0x73)
- `enip_scanner_motoman_read_axis_config()` - Read axis configuration (Class 0x74)
- `enip_scanner_motoman_read_position()` - Read robot position (Class 0x75)
- `enip_scanner_motoman_read_position_deviation()` - Read position deviation (Class 0x76)
- `enip_scanner_motoman_read_torque()` - Read axis torque (Class 0x77)

**I/O and Registers:**
- `enip_scanner_motoman_read_io()` / `write_io()` - Read/write I/O signals (Class 0x78)
- `enip_scanner_motoman_read_register()` / `write_register()` - Read/write registers (Class 0x79)

**Variables:**
- `enip_scanner_motoman_read_variable_b()` / `write_variable_b()` - Byte variables (Class 0x7A)
- `enip_scanner_motoman_read_variable_i()` / `write_variable_i()` - Integer variables (Class 0x7B)
- `enip_scanner_motoman_read_variable_d()` / `write_variable_d()` - Double integer variables (Class 0x7C)
- `enip_scanner_motoman_read_variable_r()` / `write_variable_r()` - Real variables (Class 0x7D)
- `enip_scanner_motoman_read_variable_s()` / `write_variable_s()` - String variables (Class 0x8C)
- `enip_scanner_motoman_read_variable_p()` / `write_variable_p()` - Position variables (Class 0x7F)
- `enip_scanner_motoman_read_variable_bp()` / `write_variable_bp()` - Base position variables (Class 0x80)
- `enip_scanner_motoman_read_variable_ex()` / `write_variable_ex()` - External axis variables (Class 0x81)

**I/O Signal Number Ranges:**
- 1-256: General input
- 1001-1256: General output
- 2001-2256: External input
- 2501-2756: Network input (writable)
- 3001-3256: External output
- 3501-3756: Network output
- And more (see [MOTOMAN_CIP_CLASSES.md](MOTOMAN_CIP_CLASSES.md))

**See Also:**
- [MOTOMAN_CIP_CLASSES.md](MOTOMAN_CIP_CLASSES.md) - Complete reference for all Motoman CIP classes
- [Examples](../examples/README.md) - Example translator implementation (Micro800 ↔ Motoman)

## Requirements

- ESP-IDF v5.0 or later
- Network connectivity (WiFi or Ethernet)
- FreeRTOS (included with ESP-IDF)

**Required Components:**
- `lwip` - TCP/IP stack
- `esp_netif` - Network interface
- `freertos` - Task and mutex support

## Configuration

Configure via `idf.py menuconfig` under **"EtherNet/IP Scanner Configuration"**:

- **Enable debug logging**: Verbose debug output (default: disabled)
- **Maximum number of devices to scan**: Device discovery limit (default: 32)
- **Default timeout (milliseconds)**: Operation timeout (default: 5000ms)
- **Enable Allen-Bradley tag support**: Tag read/write support (default: disabled)
- **Enable Motoman robot CIP class support**: Motoman robot status, I/O, variables support (default: disabled)

## Thread Safety

All API functions are **thread-safe** and can be called from multiple FreeRTOS tasks concurrently. The component uses internal mutexes to protect shared state - no additional synchronization is required from application code.

## Error Handling

All functions return `esp_err_t` error codes. Always check return values and free allocated resources:

```c
enip_scanner_assembly_result_t result;
memset(&result, 0, sizeof(result));

esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);

// Always free resources, even on error
if (ret == ESP_OK && result.success) {
    // Use result.data...
} else {
    ESP_LOGE("app", "Error: %s", result.error_message);
}

enip_scanner_free_assembly_result(&result);
```

## Web Interface

When enabled, the component provides a web interface for:
- Device discovery
- Assembly read/write operations
- Tag read/write operations (if tag support is enabled)

Access the web interface at the ESP32's IP address after initialization.

## API Reference

See [API_DOCUMENTATION.md](API_DOCUMENTATION.md) for complete API reference, detailed examples, and advanced usage patterns.

## Additional Resources

- **Main Repository**: [ESP32_ENIPScanner on GitHub](https://github.com/AGSweeney/ESP32_ENIPScanner)
- **API Documentation**: [API_DOCUMENTATION.md](API_DOCUMENTATION.md)
- **Motoman CIP Classes**: [MOTOMAN_CIP_CLASSES.md](MOTOMAN_CIP_CLASSES.md) - Documentation for Motoman vendor-specific CIP classes
- **Project README**: [Main Project README](../../README.md)

## License

See LICENSE file in project root.

# EtherNet/IP Scanner Component

EtherNet/IP scanner component for ESP-IDF that provides explicit messaging capabilities for communicating with industrial EtherNet/IP devices over TCP/IP networks.

![EtherNet/IP Scanner Web Interface](ESP32-ENIPScanner.png)

## Overview

This component enables ESP32 devices to communicate with EtherNet/IP devices (such as Allen-Bradley PLCs) using explicit messaging. It supports device discovery, assembly data read/write operations, and tag-based communication for Micro800 series PLCs.

**Key Capabilities:**
- **Device Discovery**: Scan network for EtherNet/IP devices via UDP broadcast
- **Assembly I/O**: Read and write assembly data using explicit messaging
- **Tag Support**: Read and write tags on Micro800 PLCs using symbolic names (experimental)
- **Thread-Safe**: All operations protected with mutexes for concurrent access
- **Web Interface**: Built-in web UI for device discovery and tag operations

## Quick Start

### 1. Initialize the Component

```c
#include "enip_scanner.h"

// Initialize the scanner (call once after network is up)
esp_err_t ret = enip_scanner_init();
if (ret != ESP_OK) {
    ESP_LOGE("app", "Failed to initialize scanner");
    return;
}
```

### 2. Discover Devices

```c
enip_scanner_device_info_t devices[32];
int count = enip_scanner_scan_devices(devices, 32, 5000);

ESP_LOGI("app", "Found %d device(s)", count);
for (int i = 0; i < count; i++) {
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&devices[i].ip_address));
    ESP_LOGI("app", "  %s - %s", ip_str, devices[i].product_name);
}
```

### 3. Read Assembly Data

```c
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);

enip_scanner_assembly_result_t result;
memset(&result, 0, sizeof(result));

esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);
if (ret == ESP_OK && result.success) {
    ESP_LOGI("app", "Read %d bytes", result.data_length);
    // Access data: result.data[0], result.data[1], etc.
    
    // Always free the result
    enip_scanner_free_assembly_result(&result);
} else {
    ESP_LOGE("app", "Read failed: %s", result.error_message);
    enip_scanner_free_assembly_result(&result);
}
```

### 4. Write Assembly Data

```c
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);

uint8_t output_data[4] = {0x04, 0x00, 0x00, 0x00};  // Set bit 2
char error_msg[128];

esp_err_t ret = enip_scanner_write_assembly(&device_ip, 150, output_data, 4, 5000, error_msg);
if (ret == ESP_OK) {
    ESP_LOGI("app", "Write successful");
} else {
    ESP_LOGE("app", "Write failed: %s", error_msg);
}
```

## Tag Support (Micro800 Series)

Tag support allows reading and writing PLC tags using symbolic names instead of assembly instances. This feature is experimental and designed specifically for Allen-Bradley Micro800 series PLCs.

### Enabling Tag Support

1. Run `idf.py menuconfig`
2. Navigate to: **Component config** → **EtherNet/IP Scanner Configuration**
3. Enable: **"Enable Allen-Bradley tag support"**
4. Rebuild your project

### Reading Tags

```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);

enip_scanner_tag_result_t result;
memset(&result, 0, sizeof(result));

// Read a DINT tag
esp_err_t ret = enip_scanner_read_tag(&device_ip, "Counter", &result, 5000);
if (ret == ESP_OK && result.success) {
    if (result.cip_data_type == CIP_DATA_TYPE_DINT && result.data_length == 4) {
        int32_t value = (int32_t)(result.data[0] | (result.data[1] << 8) | 
                                  (result.data[2] << 16) | (result.data[3] << 24));
        ESP_LOGI("app", "Counter = %ld", value);
    }
    enip_scanner_free_tag_result(&result);
}
#endif
```

### Writing Tags

```c
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);

// Write a BOOL tag
uint8_t bool_value = 1;  // true
char error_msg[128];
esp_err_t ret = enip_scanner_write_tag(&device_ip, "Output1", &bool_value, 1, 
                                       CIP_DATA_TYPE_BOOL, 5000, error_msg);

// Write a DINT tag
int32_t dint_value = 12345;
uint8_t dint_bytes[4];
dint_bytes[0] = dint_value & 0xFF;
dint_bytes[1] = (dint_value >> 8) & 0xFF;
dint_bytes[2] = (dint_value >> 16) & 0xFF;
dint_bytes[3] = (dint_value >> 24) & 0xFF;

ret = enip_scanner_write_tag(&device_ip, "Setpoint", dint_bytes, 4, 
                             CIP_DATA_TYPE_DINT, 5000, error_msg);
#endif
```

**Tag Path Format:**
- `"MyTag"` - Simple tag
- `"MyArray[0]"` - Array element
- `"MyStruct.Field"` - Structure field

**Note:** Micro800 PLCs do not support program-scoped tags - tags must be in the global variable table.

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

## License

See LICENSE file in project root.

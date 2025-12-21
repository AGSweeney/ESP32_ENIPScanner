# ENIP Scanner Component

EtherNet/IP scanner component for ESP-IDF that provides explicit messaging capabilities for communicating with industrial EtherNet/IP devices.

## Features

- **Device Discovery**: Scan network for EtherNet/IP devices using UDP broadcast (List Identity)
- **Explicit Messaging**: Read and write assembly data using TCP-based explicit messaging
- **Session Management**: Register and unregister EtherNet/IP sessions
- **Thread-Safe**: Mutex-protected operations for safe concurrent access
- **Resource Management**: Proper cleanup of sockets and memory in all error paths

## Requirements

This component requires the following ESP-IDF components:
- `lwip` - TCP/IP stack
- `esp_netif` - Network interface abstraction
- `freertos` - FreeRTOS for mutex and task support
- `driver` - ESP32 driver framework
- `esp_common` - Common ESP-IDF utilities

## Usage

### Initialization

```c
#include "enip_scanner.h"

// Initialize the scanner (call once at startup)
esp_err_t ret = enip_scanner_init();
if (ret != ESP_OK) {
    ESP_LOGE("app", "Failed to initialize ENIP scanner");
    return;
}
```

### Scanning for Devices

```c
enip_scanner_device_info_t devices[32];
int count = enip_scanner_scan_devices(devices, 32, 5000);
ESP_LOGI("app", "Found %d devices", count);

for (int i = 0; i < count; i++) {
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&devices[i].ip_address));
    ESP_LOGI("app", "Device %d: %s - %s", i, ip_str, devices[i].product_name);
}
```

### Reading Assembly Data

```c
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);

enip_scanner_assembly_result_t result;
esp_err_t ret = enip_scanner_read_assembly(&device_ip, 100, &result, 5000);

if (ret == ESP_OK && result.success) {
    ESP_LOGI("app", "Read %d bytes from assembly 100", result.data_length);
    // Use result.data...
    
    // Always free the result data when done
    enip_scanner_free_assembly_result(&result);
} else {
    ESP_LOGE("app", "Failed to read assembly: %s", result.error_message);
}
```

### Writing Assembly Data

```c
ip4_addr_t device_ip;
inet_aton("192.168.1.100", &device_ip);

uint8_t data[32] = {0x01, 0x02, 0x03, ...};
char error_msg[128];
esp_err_t ret = enip_scanner_write_assembly(&device_ip, 150, data, 32, 5000, error_msg);

if (ret == ESP_OK) {
    ESP_LOGI("app", "Successfully wrote assembly 150");
} else {
    ESP_LOGE("app", "Failed to write assembly: %s", error_msg);
}
```

## API Reference

See [API_DOCUMENTATION.md](API_DOCUMENTATION.md) for complete API documentation.

## Configuration

The component can be configured via `idf.py menuconfig` under "EtherNet/IP Scanner Configuration":

- **Enable debug logging**: Enable verbose debug logging
- **Maximum number of devices to scan**: Limit for device discovery (default: 32)
- **Default timeout (milliseconds)**: Default timeout for operations (default: 5000ms)

## Thread Safety

All public API functions are thread-safe and can be called from multiple tasks concurrently. The component uses FreeRTOS mutexes internally to protect shared state.

## Error Handling

All functions return `esp_err_t` error codes. Functions that return data structures include error messages in the result structure. Always check return values and free allocated resources using `enip_scanner_free_assembly_result()`.

## License

See LICENSE file in project root.


# Implicit Messaging API Examples

Complete guide and examples for using EtherNet/IP implicit messaging (Class 1 I/O) with the ESP32 ENIP Scanner component.

## Table of Contents

1. [Overview](#overview)
2. [Enabling Implicit Messaging](#enabling-implicit-messaging)
3. [Basic Usage](#basic-usage)
4. [API Reference](#api-reference)
5. [Complete Examples](#complete-examples)
6. [Best Practices](#best-practices)
7. [Troubleshooting](#troubleshooting)

---

## Overview

Implicit messaging (Class 1 I/O) provides real-time, cyclic data exchange between an EtherNet/IP scanner and target device. Unlike explicit messaging which uses request/response pairs, implicit messaging uses UDP-based cyclic packets for high-speed I/O data transfer.

**Key Characteristics:**
- **UDP-based**: Uses port 2222 for I/O data packets
- **Cyclic**: Data is exchanged at a fixed rate (RPI - Requested Packet Interval)
- **Bidirectional**: O-to-T (Originator-to-Target) and T-to-O (Target-to-Originator) data streams
- **Connection-based**: Requires Forward Open/Close for connection management
- **Real-time**: Designed for time-critical I/O data exchange

**Use Cases:**
- Real-time control data exchange
- High-speed I/O monitoring
- Continuous data acquisition
- Process control applications

---

## Enabling Implicit Messaging

1. Run `idf.py menuconfig`
2. Navigate to: **Component config** → **EtherNet/IP Scanner Configuration**
3. Enable: **"Enable implicit messaging (Class 1 I/O) support"**
4. Rebuild your project

---

## Basic Usage

### Opening a Connection

```c
#include "enip_scanner.h"
#include "lwip/inet.h"

void open_implicit_connection(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    // Callback function to receive T-to-O data
    void implicit_data_callback(const ip4_addr_t *ip_address,
                                uint16_t assembly_instance,
                                const uint8_t *data,
                                uint16_t data_length,
                                void *user_data)
    {
        ESP_LOGI("app", "Received %u bytes from assembly %u", data_length, assembly_instance);
        // Process received data...
    }
    
    // Open implicit connection
    // Assembly 150: O-to-T (consumed, 40 bytes)
    // Assembly 100: T-to-O (produced, 72 bytes)
    // RPI: 100ms (10 packets per second)
    // Timeout: 5000ms
    // Exclusive owner: true (PTP mode)
    esp_err_t ret = enip_scanner_implicit_open(
        &device_ip,                    // Device IP address
        150,                            // O-to-T assembly instance (consumed)
        100,                            // T-to-O assembly instance (produced)
        40,                             // O-to-T data size (bytes) - 0 = autodetect
        72,                             // T-to-O data size (bytes) - 0 = autodetect
        100,                            // RPI in milliseconds
        implicit_data_callback,         // Callback for T-to-O data
        NULL,                           // User data (passed to callback)
        5000,                           // Timeout for Forward Open
        true                            // Exclusive owner (PTP mode)
    );
    
    if (ret == ESP_OK) {
        ESP_LOGI("app", "Implicit connection opened successfully");
    } else {
        ESP_LOGE("app, "Failed to open implicit connection: %s", esp_err_to_name(ret));
    }
}
```

### Writing O-to-T Data

```c
void write_output_data(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    // Prepare output data (40 bytes for assembly 150)
    uint8_t output_data[40] = {0};
    
    // Set some control bits
    output_data[0] = 0x01;  // Bit 0: Enable
    output_data[1] = 100;   // Speed value
    output_data[2] = 50;    // Position value (low byte)
    output_data[3] = 0;    // Position value (high byte)
    
    // Write data (will be sent in next heartbeat)
    esp_err_t ret = enip_scanner_implicit_write_data(&device_ip, output_data, 40);
    
    if (ret == ESP_OK) {
        ESP_LOGI("app", "Output data written successfully");
    } else {
        ESP_LOGE("app", "Failed to write output data: %s", esp_err_to_name(ret));
    }
}
```

### Reading Current O-to-T Data

```c
void read_current_output_data(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    uint8_t current_data[40];
    uint16_t data_length = 0;
    
    esp_err_t ret = enip_scanner_implicit_read_o_to_t_data(
        &device_ip,
        current_data,
        &data_length,
        sizeof(current_data)
    );
    
    if (ret == ESP_OK) {
        ESP_LOGI("app", "Current O-to-T data: %u bytes", data_length);
        ESP_LOGI("app, "First byte: 0x%02X", current_data[0]);
    }
}
```

### Closing a Connection

```c
void close_implicit_connection(void)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    esp_err_t ret = enip_scanner_implicit_close(&device_ip, 5000);
    
    if (ret == ESP_OK) {
        ESP_LOGI("app", "Implicit connection closed successfully");
    } else {
        ESP_LOGE("app", "Failed to close connection: %s", esp_err_to_name(ret));
    }
}
```

---

## API Reference

### `enip_scanner_implicit_open()`

Open an implicit messaging connection to a target device.

**Prototype:**
```c
esp_err_t enip_scanner_implicit_open(
    const ip4_addr_t *ip_address,
    uint16_t assembly_instance_consumed,      // O-to-T assembly instance
    uint16_t assembly_instance_produced,      // T-to-O assembly instance
    uint16_t assembly_data_size_consumed,     // O-to-T data size (bytes, 0 = autodetect)
    uint16_t assembly_data_size_produced,     // T-to-O data size (bytes, 0 = autodetect)
    uint32_t rpi_ms,                          // Requested Packet Interval (10-10000 ms)
    enip_implicit_data_callback_t callback,   // Callback for T-to-O data
    void *user_data,                          // User data passed to callback
    uint32_t timeout_ms,                      // Timeout for Forward Open
    bool exclusive_owner                      // true = PTP, false = non-PTP
);
```

**Parameters:**
- `ip_address`: Target device IP address
- `assembly_instance_consumed`: Assembly instance for O-to-T data (typically 150)
- `assembly_instance_produced`: Assembly instance for T-to-O data (typically 100)
- `assembly_data_size_consumed`: O-to-T data size in bytes. Use `0` to autodetect from device
- `assembly_data_size_produced`: T-to-O data size in bytes. Use `0` to autodetect from device
- `rpi_ms`: Requested Packet Interval in milliseconds (10-10000). This is the rate at which I/O packets are exchanged
- `callback`: Function called when T-to-O data is received
- `user_data`: User-defined data passed to callback
- `timeout_ms`: Timeout for Forward Open operation (milliseconds)
- `exclusive_owner`: `true` for PTP (Point-to-Point, exclusive owner), `false` for non-PTP (multicast T-to-O)

**Returns:**
- `ESP_OK`: Connection opened successfully
- `ESP_ERR_INVALID_ARG`: Invalid parameters
- `ESP_ERR_INVALID_STATE`: Scanner not initialized or connection already open
- `ESP_ERR_NO_MEM`: No free connection slots
- `ESP_ERR_NOT_FOUND`: Autodetection failed (if size = 0)
- `ESP_FAIL`: Forward Open failed

**Callback Function:**
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
- Only one connection per IP address is supported
- O-to-T data is automatically sent every RPI
- T-to-O data is received asynchronously via callback
- Connection must be closed before opening a new one
- Autodetection reads assembly Attribute 4 (Data Size) from the device

---

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
- `timeout_ms`: Timeout for Forward Close operation (milliseconds)

**Returns:**
- `ESP_OK`: Connection closed successfully
- `ESP_ERR_INVALID_ARG`: Invalid IP address
- `ESP_ERR_NOT_FOUND`: No connection found for this IP

**Notes:**
- Sends Forward Close request to device
- Stops heartbeat and receive tasks
- Closes TCP and UDP sockets
- Waits for device to release resources if Forward Close fails

---

### `enip_scanner_implicit_write_data()`

Write data to the O-to-T assembly instance. Data is stored in memory and sent in heartbeat packets.

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
- `ESP_ERR_NOT_FOUND`: No connection found for this IP
- `ESP_ERR_NO_MEM`: Memory allocation failed

**Notes:**
- Data is stored in memory and sent automatically every RPI
- Data length must exactly match `assembly_data_size_consumed`
- If data is shorter, remaining bytes are zero-padded
- The heartbeat task also writes explicitly to the assembly instance every RPI

---

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
- `ESP_ERR_NOT_FOUND`: No connection found for this IP

**Notes:**
- Reads data from memory (not from device)
- Returns the data that's currently being sent in heartbeat packets
- Useful for checking what data is being sent

---

## Complete Examples

### Example 1: Basic I/O Monitoring

```c
#include "enip_scanner.h"
#include "lwip/inet.h"
#include "freertos/task.h"

static void t_to_o_callback(const ip4_addr_t *ip_address,
                           uint16_t assembly_instance,
                           const uint8_t *data,
                           uint16_t data_length,
                           void *user_data)
{
    ESP_LOGI("app", "=== T-to-O Data Received ===");
    ESP_LOGI("app", "Assembly: %u, Length: %u bytes", assembly_instance, data_length);
    
    // Parse received data
    if (data_length >= 4) {
        bool status_bit = (data[0] & 0x01) != 0;
        uint16_t sensor_value = data[1] | (data[2] << 8);
        uint8_t error_code = data[3];
        
        ESP_LOGI("app", "Status: %d, Sensor: %u, Error: 0x%02X", 
                 status_bit, sensor_value, error_code);
    }
}

void implicit_io_monitoring_task(void *pvParameters)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    // Open connection
    esp_err_t ret = enip_scanner_implicit_open(
        &device_ip,
        150,    // O-to-T assembly
        100,    // T-to-O assembly
        0,      // Autodetect O-to-T size
        0,      // Autodetect T-to-O size
        100,    // 100ms RPI
        t_to_o_callback,
        NULL,
        5000,
        true    // PTP mode
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE("app", "Failed to open connection");
        vTaskDelete(NULL);
        return;
    }
    
    // Monitor for 60 seconds
    vTaskDelay(pdMS_TO_TICKS(60000));
    
    // Close connection
    enip_scanner_implicit_close(&device_ip, 5000);
    
    vTaskDelete(NULL);
}
```

### Example 2: Control Loop with Feedback

```c
#include "enip_scanner.h"
#include "lwip/inet.h"
#include "freertos/task.h"

typedef struct {
    bool enable;
    uint16_t setpoint;
    uint16_t actual_value;
    bool error;
} control_data_t;

static control_data_t g_control_state = {0};

static void feedback_callback(const ip4_addr_t *ip_address,
                             uint16_t assembly_instance,
                             const uint8_t *data,
                             uint16_t data_length,
                             void *user_data)
{
    control_data_t *state = (control_data_t *)user_data;
    
    if (data_length >= 4) {
        state->actual_value = data[0] | (data[1] << 8);
        state->error = (data[2] & 0x01) != 0;
        state->enable = (data[3] & 0x80) != 0;
    }
}

void control_loop_task(void *pvParameters)
{
    ip4_addr_t device_ip;
    inet_aton("192.168.1.100", &device_ip);
    
    // Open connection
    esp_err_t ret = enip_scanner_implicit_open(
        &device_ip,
        150,
        100,
        40,
        72,
        50,     // 50ms RPI (20 Hz)
        feedback_callback,
        &g_control_state,
        5000,
        true
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE("app", "Failed to open connection");
        vTaskDelete(NULL);
        return;
    }
    
    // Control loop
    uint16_t setpoint = 1000;
    uint8_t output_data[40] = {0};
    
    while (1) {
        // Update setpoint
        output_data[0] = setpoint & 0xFF;
        output_data[1] = (setpoint >> 8) & 0xFF;
        output_data[2] = 0x01;  // Enable bit
        
        // Write control data
        enip_scanner_implicit_write_data(&device_ip, output_data, 40);
        
        // Log feedback
        ESP_LOGI("app", "Setpoint: %u, Actual: %u, Error: %d",
                 setpoint, g_control_state.actual_value, g_control_state.error);
        
        // Adjust setpoint based on feedback
        if (g_control_state.actual_value < setpoint - 10) {
            setpoint -= 5;
        } else if (g_control_state.actual_value > setpoint + 10) {
            setpoint += 5;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    enip_scanner_implicit_close(&device_ip, 5000);
    vTaskDelete(NULL);
}
```

### Example 3: Multiple Devices

```c
#include "enip_scanner.h"
#include "lwip/inet.h"

void device1_callback(const ip4_addr_t *ip_address,
                     uint16_t assembly_instance,
                     const uint8_t *data,
                     uint16_t data_length,
                     void *user_data)
{
    ESP_LOGI("device1", "Received %u bytes", data_length);
}

void device2_callback(const ip4_addr_t *ip_address,
                     uint16_t assembly_instance,
                     const uint8_t *data,
                     uint16_t data_length,
                     void *user_data)
{
    ESP_LOGI("device2", "Received %u bytes", data_length);
}

void multiple_devices_example(void)
{
    ip4_addr_t device1_ip, device2_ip;
    inet_aton("192.168.1.100", &device1_ip);
    inet_aton("192.168.1.101", &device2_ip);
    
    // Open connection to device 1
    enip_scanner_implicit_open(&device1_ip, 150, 100, 40, 72, 100,
                              device1_callback, NULL, 5000, true);
    
    // Open connection to device 2
    enip_scanner_implicit_open(&device2_ip, 150, 100, 40, 72, 100,
                              device2_callback, NULL, 5000, true);
    
    // Both connections run concurrently
    // ...
    
    // Close connections
    enip_scanner_implicit_close(&device1_ip, 5000);
    enip_scanner_implicit_close(&device2_ip, 5000);
}
```

---

## Best Practices

### 1. Connection Management

- **Always close connections** before opening new ones
- **Wait for close to complete** before reconnecting (especially if Forward Close failed)
- **Use appropriate timeouts** for Forward Open/Close operations

### 2. RPI Selection

- **Choose RPI based on requirements**: 10-100ms for high-speed control, 100-1000ms for monitoring
- **Maximum RPI**: 1000ms (enforced by implementation)
- **Consider network load**: Lower RPI = more network traffic

### 3. Data Size Autodetection

- **Use autodetection** (size = 0) when possible
- **Verify sizes** after connection opens
- **Match device configuration**: Ensure assembly instances and sizes match device setup

### 4. Callback Functions

- **Keep callbacks fast**: Don't block or perform heavy operations
- **Copy data if needed**: Data buffer is freed after callback returns
- **Use user_data**: Pass context information via user_data parameter

### 5. Error Handling

- **Check return values**: Always verify function return codes
- **Handle connection failures**: Implement retry logic with delays
- **Monitor connection state**: Watch for timeout warnings in logs

### 6. Thread Safety

- **API is thread-safe**: Can be called from multiple tasks
- **Callback is called from receive task**: Use synchronization if accessing shared data
- **Write data atomically**: Update entire O-to-T buffer in one call

---

## Troubleshooting

### Connection Fails with Ownership Conflict (0x0106)

**Cause:** Another connection already exists or device hasn't released resources.

**Solutions:**
- Close existing connection first
- Wait 6+ seconds before retrying
- Reboot device if problem persists

### Connection Fails with Invalid Connection Parameters (0x0315)

**Cause:** Connection size mismatch or invalid parameters.

**Solutions:**
- Use autodetection (size = 0) to get correct sizes
- Verify assembly instances are correct
- Check device configuration matches your parameters

### No T-to-O Data Received

**Possible Causes:**
- Device not configured to produce T-to-O data
- Wrong assembly instance
- Wrong connection ID (check Forward Open response)
- Network issues

**Solutions:**
- Verify device configuration
- Check assembly instance matches device setup
- Monitor logs for connection ID mismatches
- Verify network connectivity

### Connection Timeout

**Cause:** Device stopped sending T-to-O packets.

**Solutions:**
- Check if O-to-T heartbeats are still being sent
- Verify device is still powered and connected
- Check network connectivity
- Review device logs for errors

### Forward Close Times Out

**Cause:** Device not responding to Forward Close.

**Solutions:**
- Implementation automatically waits for device watchdog timeout
- Connection will be released after timeout period
- Wait before attempting to reconnect

---

## Additional Resources

- **Main README**: [README.md](README.md)
- **API Documentation**: [API_DOCUMENTATION.md](API_DOCUMENTATION.md)
- **EtherNet/IP Specification**: ODVA EtherNet/IP Specification Volume 1
- **Web UI**: Access implicit messaging page at device IP address

---

## License

See LICENSE file in project root.


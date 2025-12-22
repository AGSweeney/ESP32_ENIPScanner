# ESP32 EtherNet/IP Scanner

An ESP32-based EtherNet/IP scanner implementation that provides explicit messaging capabilities for communicating with industrial EtherNet/IP devices. This project enables ESP32 devices to act as EtherNet/IP scanners, discovering devices on the network and reading/writing assembly data.

## Overview

This project implements an EtherNet/IP scanner on the ESP32 platform, specifically designed for the ESP32-P4 development board with Ethernet connectivity. It provides a complete solution for:

- **Device Discovery**: Scan the network for EtherNet/IP devices using UDP broadcast
- **Explicit Messaging**: Read and write assembly data using TCP-based explicit messaging
- **Web Interface**: Built-in web UI for device configuration and monitoring
- **Complete Examples**: Full example code demonstrating I/O mapping between EtherNet/IP assemblies and GPIO pins

## Features

- ✅ EtherNet/IP explicit messaging (TCP port 44818)
- ✅ Device discovery via List Identity requests
- ✅ Assembly read/write operations
- ✅ Session management
- ✅ Web-based user interface
- ✅ Complete GPIO I/O mapping examples
- ✅ DHCP and static IP support (configurable via web UI with NVS persistence)
- ✅ Tag read/write operations

## Hardware Requirements

- **ESP32-P4 Development Board** (e.g., Waveshare ESP32-P4 Dev Kit)
- **Ethernet PHY** (e.g., IP101)
- **Ethernet connection** to your industrial network

### Pin Configuration (Waveshare ESP32-P4)

- MDC GPIO: 31
- MDIO GPIO: 52
- REF_CLK GPIO: 50
- PHY Reset GPIO: 51

## Software Requirements

- **ESP-IDF** v5.0 or later
- **CMake** 3.16 or later
- **Python** 3.6 or later (for ESP-IDF tools)

## Building the Project

1. **Install ESP-IDF** (if not already installed):
   ```bash
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh
   . ./export.sh
   ```

2. **Clone this repository**:
   ```bash
   git clone <repository-url>
   cd ESP32-ENIPScanner
   ```

3. **Configure the project**:
   ```bash
   idf.py menuconfig
   ```
   Configure network settings, GPIO pins, and other options as needed.

4. **Build the project**:
   ```bash
   idf.py build
   ```

5. **Flash to device**:
   ```bash
   idf.py flash
   ```

6. **Monitor serial output**:
   ```bash
   idf.py monitor
   ```

## Project Structure

```
ESP32-ENIPScanner/
├── main/                    # Main application code
│   ├── main.c              # Application entry point
│   └── CMakeLists.txt
├── components/
│   ├── enip_scanner/       # EtherNet/IP scanner component
│   │   ├── enip_scanner.c
│   │   ├── include/
│   │   │   └── enip_scanner.h
│   │   └── API_DOCUMENTATION.md
│   ├── webui/              # Web interface component
│   │   ├── src/
│   │   │   ├── webui.c
│   │   │   ├── webui_api.c
│   │   │   └── webui_html.c
│   │   └── include/
│   └── system_config/      # System configuration management
├── CMakeLists.txt
├── sdkconfig.defaults
└── README.md
```

## Usage

### Basic Example

A complete example demonstrating GPIO I/O mapping is available in the API documentation:

1. **Reading Input Assembly**: Reads assembly instance 100 from the EtherNet/IP device
2. **GPIO Control**: Controls GPIO1 based on input assembly byte 0 bit 1
3. **GPIO Input**: Reads GPIO2 state
4. **Writing Output Assembly**: Writes GPIO2 state to output assembly byte 0 bit 2

See the [Complete Example](components/enip_scanner/API_DOCUMENTATION.md#complete-example) section in the API documentation for the full implementation.

### Configuration

The example code in the API documentation includes configuration defines that you can modify to match your setup:

```c
#define ENIP_DEVICE_IP "172.16.82.155"           // Target EtherNet/IP device IP
#define ENIP_INPUT_ASSEMBLY_INSTANCE 100         // Input assembly instance
#define ENIP_OUTPUT_ASSEMBLY_INSTANCE 150        // Output assembly instance
#define GPIO_OUTPUT_PIN 1                        // GPIO pin for output control
#define GPIO_INPUT_PIN 2                         // GPIO pin for input reading
```

### Web Interface

After the device boots and obtains an IP address, access the web interface at:

```
http://<device-ip>/
```

The web interface provides:
- **Device Discovery** (`/`): Scan network for EtherNet/IP devices and read/write assembly data
- **Read Tag** (`/tags`): Read tags from Micro800 PLCs using symbolic names (e.g., "MyTag", "MyArray[0]")
- **Write Tag** (`/write-tag`): Write tags to Micro800 PLCs (supports BOOL, SINT, INT, DINT, REAL types)
- **Network Configuration** (`/network`): Configure DHCP or static IP settings with NVS persistence (see Network Configuration section below)

![EtherNet/IP Scanner Web Interface](components/enip_scanner/ESP32-ENIPScanner.png)

*Screenshot showing the web interface with device discovery, assembly instance selection, and decimal data editor.*

### API Documentation

Complete API documentation including usage examples is available in [`components/enip_scanner/API_DOCUMENTATION.md`](components/enip_scanner/API_DOCUMENTATION.md).

The API documentation includes:
- Quick start guide
- Complete function reference
- Bit manipulation examples
- Full GPIO I/O mapping example
- Error handling guidelines

## Key Components

### EtherNet/IP Scanner (`enip_scanner`)

The core component providing EtherNet/IP functionality:

- **Device Discovery**: `enip_scanner_scan_devices()`
- **Read Assembly**: `enip_scanner_read_assembly()`
- **Write Assembly**: `enip_scanner_write_assembly()`
- **Session Management**: `enip_scanner_register_session()`, `enip_scanner_unregister_session()`

### Web UI (`webui`)

Provides HTTP-based interface for:
- Device scanning and discovery
- Assembly read/write operations
- Tag read/write operations
- Network configuration (DHCP/Static IP with NVS persistence)
- Real-time device status

## Example: GPIO I/O Mapping

A complete example demonstrating bidirectional I/O mapping is available in the [API Documentation](components/enip_scanner/API_DOCUMENTATION.md#complete-example). The example shows:

- Reading input assembly data and controlling GPIO outputs
- Reading GPIO inputs and writing to output assemblies
- Proper bit manipulation techniques
- Error handling and state management

The example includes a complete FreeRTOS task implementation that can be integrated into your application.

## Network Configuration

The device supports both DHCP and static IP configuration:

- **DHCP**: Enabled by default
- **Static IP**: Configure via web interface at `/network` or programmatically using `system_config` component

### Web UI Configuration

Access the network configuration page at:
```
http://<device-ip>/network
```

The page allows you to:
1. Select between DHCP and Static IP modes
2. Enter static IP configuration (IP address, netmask, gateway, DNS servers)
3. Save settings to NVS (persists across reboots)
4. Current DHCP-assigned values automatically populate when switching to static mode

**Important**: After saving network configuration, restart the device for changes to take effect.

### Programmatic Configuration

You can also configure network settings programmatically using the `system_config` component:

```c
#include "system_config.h"

system_ip_config_t config;
config.use_dhcp = false;  // Use static IP
config.ip_address = ipaddr_addr("192.168.1.100");
config.netmask = ipaddr_addr("255.255.255.0");
config.gateway = ipaddr_addr("192.168.1.1");
config.dns1 = ipaddr_addr("8.8.8.8");
config.dns2 = ipaddr_addr("8.8.4.4");

if (system_ip_config_save(&config)) {
    ESP_LOGI("app", "Network configuration saved");
}
```

## Troubleshooting

### Device Not Found During Scan

- Verify network connectivity
- Check that target devices support EtherNet/IP
- Ensure devices are on the same network segment
- Check firewall settings (UDP port 44818)

### Assembly Read/Write Failures

- Verify assembly instance numbers (common: 100=input, 150=output)
- Check device permissions (some assemblies may be read-only)
- Verify session registration succeeded
- Check timeout values (default: 5000ms)

### Web Interface Not Accessible

- Verify device obtained IP address (check serial monitor)
- Check network connectivity
- Ensure HTTP server started successfully
- Default port: 80

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Author

Adam G. Sweeney <agsweeney@gmail.com>

## Acknowledgments

- ESP-IDF framework by Espressif Systems
- lwIP networking stack
- EtherNet/IP protocol specification (ODVA)

## Third-Party Components and Attribution

This project uses and modifies several third-party components. For complete attribution details, see [ATTRIBUTION.md](ATTRIBUTION.md).

### Summary

- **lwIP TCP/IP Stack**: BSD-style license, originally by Adam Dunkels (SICS), included with ESP-IDF
- **ESP-IDF Framework**: Apache License 2.0, Copyright (c) 2015-2025 Espressif Systems
- **FreeRTOS**: MIT License (modified), included with ESP-IDF
- **EtherNet/IP**: Protocol specification by ODVA; implementation developed independently

**Note**: This project uses the default lwIP and esp_netif components provided by ESP-IDF without modifications.

## References

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [EtherNet/IP Specification](https://www.odva.org/technology-standards/key-technologies/ethernet-ip/)
- [lwIP Documentation](https://www.nongnu.org/lwip/)


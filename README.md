# ESP32 EtherNet/IP Scanner

[![GitHub](https://img.shields.io/badge/GitHub-Repository-blue)](https://github.com/AGSweeney/ESP32_ENIPScanner)

An ESP32-based EtherNet/IP scanner implementation that provides explicit messaging capabilities for communicating with industrial EtherNet/IP devices. This project enables ESP32 devices to act as EtherNet/IP scanners, discovering devices on the network and reading/writing assembly data.

## Overview

This project implements an EtherNet/IP scanner on the ESP32 platform, specifically designed for the ESP32-P4 development board with Ethernet connectivity. It provides a complete solution for communicating with industrial EtherNet/IP devices using explicit messaging.

**Key Features:**
- **Device Discovery**: Scan the network for EtherNet/IP devices using UDP broadcast
- **Assembly I/O**: Read and write assembly data using TCP-based explicit messaging
- **Tag Support**: Read and write tags on Micro800 PLCs using symbolic names (20 CIP data types supported)
- **Motoman Robot Support**: Read/write robot status, I/O signals, variables, and registers via vendor-specific CIP classes
- **Implicit Messaging**: Real-time Class 1 I/O data exchange for time-critical applications
- **Web Interface**: Built-in web UI for device configuration and monitoring
- **Network Configuration**: DHCP and static IP support with NVS persistence
- **Thread-Safe**: All operations protected with mutexes for concurrent access
- **Complete Examples**: Full example code including bidirectional translator for Micro800 ↔ Motoman robots

## Features

### Core Functionality
- ✅ EtherNet/IP explicit messaging (TCP port 44818)
- ✅ EtherNet/IP implicit messaging (UDP port 2222, Class 1 I/O)
- ✅ Device discovery via UDP List Identity requests
- ✅ Assembly read/write operations
- ✅ Assembly instance discovery
- ✅ Session management (automatic)

### Tag Operations (Optional)
- ✅ Tag read/write for Micro800 series PLCs
- ✅ Support for 20 CIP data types (API)
- ✅ 6 data types via web UI (BOOL, SINT, INT, DINT, REAL, STRING)
- ✅ Symbolic tag name support (e.g., "MyTag", "MyArray[0]")

### Motoman Robot Support (Optional)
- ✅ **Complete Support**: All 18 Motoman CIP classes implemented
- ✅ **Testing Status**: Read operations validated on a live Motoman DX200 controller; write operations not yet tested
- ✅ Robot status reading (Class 0x72) - Running, Error, Hold, Alarm, Servo On
- ✅ Alarm reading (Classes 0x70, 0x71) - Current alarm and alarm history
- ✅ Job information (Class 0x73) - Active job name, line number, step, speed override
- ✅ Axis configuration (Class 0x74) - Axis coordinate names
- ✅ Robot position (Class 0x75) - Current robot position with configuration
- ✅ Position deviation (Class 0x76) - Deviation of each axis
- ✅ Torque (Class 0x77) - Torque of each axis
- ✅ I/O signal read/write (Class 0x78) - General Input/Output, Network I/O
- ✅ Register read/write (Class 0x79)
- ✅ Variable access - Byte (B), Integer (I), Double (D), Real (R), String (S) types
- ✅ Position variables - Robot (P), Base (BP), External axis (EX) position types
- ✅ Example translator application (Micro800 ↔ Motoman)

### Web Interface
- ✅ Device discovery and scanning
- ✅ Assembly read/write operations
- ✅ Tag read/write operations
- ✅ Implicit messaging connection management
- ✅ Network configuration (DHCP/Static IP)
- ✅ Real-time device status

### Additional Features
- ✅ Thread-safe operations
- ✅ Memory-safe with comprehensive resource cleanup
- ✅ Detailed error messages and status codes
- ✅ GPIO I/O mapping examples

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
   git clone https://github.com/AGSweeney/ESP32_ENIPScanner.git
   cd ESP32_ENIPScanner
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
ESP32_ENIPScanner/
├── main/                           # Main application code
│   ├── main.c                     # Application entry point
│   ├── CMakeLists.txt
│   └── Kconfig.projbuild
├── components/
│   ├── enip_scanner/              # EtherNet/IP scanner component
│   │   ├── enip_scanner.c         # Core ENIP/CIP protocol implementation
│   │   ├── enip_scanner_implicit.c # Implicit messaging (Class 1 I/O)
│   │   ├── enip_scanner_implicit_internal.h # Internal implicit messaging functions
│   │   ├── enip_scanner_tag.c     # Tag read/write operations
│   │   ├── enip_scanner_tag_data.c # Data type encoder/decoder handlers
│   │   ├── enip_scanner_tag_internal.h # Internal shared functions
│   │   ├── enip_scanner_motoman.c # Motoman CIP class operations (optional)
│   │   ├── enip_scanner_motoman_internal.h # Internal Motoman functions
│   │   ├── include/
│   │   │   └── enip_scanner.h     # Public API header
│   │   ├── CMakeLists.txt
│   │   ├── idf_component.yml
│   │   ├── Kconfig.projbuild
│   │   ├── README.md
│   │   ├── API_DOCUMENTATION.md   # Complete API reference
│   │   ├── IMPLICIT_MESSAGING_API.md # Implicit messaging API guide
│   │   ├── MOTOMAN_CIP_CLASSES.md # Motoman CIP classes reference
│   │   └── TRANSLATOR_DESIGN.md   # Translator design documentation
│   ├── webui/                     # Web interface component
│   │   ├── src/
│   │   │   ├── webui.c            # Web server initialization
│   │   │   ├── webui_api.c        # HTTP API handlers
│   │   │   └── webui_html.c       # HTML content
│   │   ├── include/
│   │   │   ├── webui.h
│   │   │   └── webui_api.h
│   │   └── CMakeLists.txt
│   └── system_config/             # System configuration management
│       ├── system_config.c
│       ├── include/
│       │   └── system_config.h
│       └── CMakeLists.txt
├── examples/                      # Example applications
│   ├── micro800_motoman_translator.c # Bidirectional translator example
│   └── README.md                  # Examples documentation
├── docs/                          # Additional documentation
├── Motoman/                       # Motoman-specific documentation
│   └── 165838-1CD.md             # Motoman manual reference
├── FirmwareImages/                # Firmware image files
├── CMakeLists.txt                 # Root CMake configuration
├── sdkconfig.defaults            # Default ESP-IDF configuration
├── partitions.csv                # Partition table
├── dependencies.lock             # Component dependency lock file
├── LICENSE                       # License file
├── ATTRIBUTION.md               # Third-party attribution
└── README.md                     # This file
```

## Quick Start

### 1. Initialize the Scanner

```c
#include "enip_scanner.h"

void app_main(void)
{
    // Initialize network first (WiFi/Ethernet)
    // ... network initialization code ...
    
    // Initialize the scanner
    esp_err_t ret = enip_scanner_init();
    if (ret != ESP_OK) {
        ESP_LOGE("app", "Failed to initialize scanner");
        return;
    }
    
    ESP_LOGI("app", "EtherNet/IP Scanner initialized");
}
```

### 2. Discover Devices

```c
enip_scanner_device_info_t devices[32];
int count = enip_scanner_scan_devices(devices, 32, 5000);
ESP_LOGI("app", "Found %d device(s)", count);
```

### 3. Read/Write Assembly Data

See the [Component README](components/enip_scanner/README.md) for detailed examples.

## Usage Examples

### GPIO I/O Mapping Example

A complete example demonstrating GPIO I/O mapping is available in the [API Documentation](components/enip_scanner/API_DOCUMENTATION.md#complete-examples). The example shows:

1. **Reading Input Assembly**: Reads assembly instance 100 from the EtherNet/IP device
2. **GPIO Control**: Controls GPIO1 based on input assembly byte 0 bit 1
3. **GPIO Input**: Reads GPIO2 state
4. **Writing Output Assembly**: Writes GPIO2 state to output assembly byte 0 bit 2

**Configuration Example:**
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
- **Write Tag** (`/write-tag`): Write tags to Micro800 PLCs (supports BOOL, SINT, INT, DINT, REAL, STRING types)
- **Network Configuration** (`/network`): Configure DHCP or static IP settings with NVS persistence (see Network Configuration section below)

**Note:** The web UI supports 6 data types for tag writing (BOOL, SINT, INT, DINT, REAL, STRING). The API supports all 20 CIP data types. See the [API Documentation](components/enip_scanner/API_DOCUMENTATION.md) for complete data type support.

![EtherNet/IP Scanner Web Interface](components/enip_scanner/ESP32-ENIPScanner.png)

*Screenshot showing the web interface with device discovery, assembly instance selection, and decimal data editor.*

## Documentation

- **[Component README](components/enip_scanner/README.md)**: Quick start guide and component overview
- **[API Documentation](components/enip_scanner/API_DOCUMENTATION.md)**: Complete API reference with detailed examples
  - Function reference for all API calls
  - Data type encoding examples
  - Bit manipulation techniques
  - Complete GPIO I/O mapping example
  - Error handling guidelines
  - Thread safety information
  - Motoman robot API functions
- **[Motoman CIP Classes](components/enip_scanner/MOTOMAN_CIP_CLASSES.md)**: Documentation for Motoman vendor-specific CIP classes
  - Available CIP classes (0x70-0x81) for explicit messaging
  - Implemented API functions for robot control
  - Robot status, alarms, position, and variable access
  - Reference to Motoman Manual 165838-1CD
- **[Examples](examples/README.md)**: Example code demonstrating component usage
  - Bidirectional translator example: Micro800 PLC ↔ Motoman Robot
  - Example pick-and-place application demonstrating bidirectional translation patterns
  - **Note**: Examples are untested demonstration code - use as reference for your own implementation

## Component Overview

### EtherNet/IP Scanner Component (`enip_scanner`)

The core component providing EtherNet/IP functionality. See [Component README](components/enip_scanner/README.md) for details.

**Main Functions:**
- `enip_scanner_init()` - Initialize the scanner
- `enip_scanner_scan_devices()` - Discover devices on network
- `enip_scanner_read_assembly()` - Read assembly data
- `enip_scanner_write_assembly()` - Write assembly data
- `enip_scanner_read_tag()` - Read tag (if tag support enabled)
- `enip_scanner_write_tag()` - Write tag (if tag support enabled)
- `enip_scanner_motoman_read_status()` - Read robot status (if Motoman support enabled)
- `enip_scanner_motoman_read_io()` / `write_io()` - Read/write I/O signals
- `enip_scanner_motoman_read_variable_*()` / `write_variable_*()` - Access robot variables

### Web UI Component (`webui`)

Provides HTTP-based interface for device management:
- Device scanning and discovery
- Assembly read/write operations
- Tag read/write operations (6 data types)
- Network configuration (DHCP/Static IP with NVS persistence)
- Real-time device status

### System Config Component (`system_config`)

Manages system configuration including network settings with NVS persistence.

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

- **Project Repository**: [ESP32_ENIPScanner on GitHub](https://github.com/AGSweeney/ESP32_ENIPScanner)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [EtherNet/IP Specification](https://www.odva.org/technology-standards/key-technologies/ethernet-ip/)
- [lwIP Documentation](https://www.nongnu.org/lwip/)


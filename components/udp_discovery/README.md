# UDP Discovery Responder Component

This component implements a UDP discovery responder that allows the ESP32-P4 device to be discovered on the network using the [UDPDiscovery](https://github.com/agsweeney1972/UDPDiscovery) protocol.

## Overview

The UDP discovery responder listens on UDP port 50000 for "DISCOVER" messages and responds with device information in the format:
```
SERVER FOUND:<hostname>;IP:<ip_address>
```

This allows network administrators to quickly identify headless devices (like the ESP32-P4) on a local network segment.

## Protocol

- **Port**: 50000 (UDP)
- **Discovery Message**: "DISCOVER" (case-sensitive)
- **Response Format**: `SERVER FOUND:<hostname>;IP:<ip_address>`

## Usage

The responder is automatically started when the device obtains an IP address. No manual configuration is required.

### Example Discovery Response

When a discovery client sends "DISCOVER" to the broadcast address on port 50000, the device responds with:
```
SERVER FOUND:ESP32-ENIPScanner;IP:192.168.1.100
```

## API

### `udp_discovery_start()`

Starts the UDP discovery responder task. Called automatically during network initialization.

**Returns:**
- `ESP_OK` on success
- Error code on failure

### `udp_discovery_stop()`

Stops the UDP discovery responder and cleans up resources.

**Returns:**
- `ESP_OK` on success
- Error code on failure

## Implementation Details

- Uses a FreeRTOS task to handle UDP socket operations
- Automatically retrieves hostname from the network interface
- Gets IP address from the default network interface
- Thread-safe with mutex protection
- Handles socket timeouts to prevent blocking

## Limitations

- Only responds to discovery requests on the same subnet
- Requires the device to have an active network connection
- Hostname is retrieved from the network interface configuration (falls back to "ESP32-ENIPScanner" if not set)

## Compatibility

Compatible with the UDPDiscovery protocol implementation:
- Python client: `udp_discover.py`
- C++ client: `udp_discover.exe` / `udp_discover.cpp`

See [UDPDiscovery repository](https://github.com/agsweeney1972/UDPDiscovery) for discovery client implementations.

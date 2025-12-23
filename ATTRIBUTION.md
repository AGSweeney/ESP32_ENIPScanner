# Third-Party Components and Attribution

[![GitHub](https://img.shields.io/badge/GitHub-Repository-blue)](https://github.com/AGSweeney/ESP32_ENIPScanner)

This document provides attribution for third-party components, libraries, and code used in this project.

## lwIP TCP/IP Stack

**Component**: lwIP (Lightweight IP) - included with ESP-IDF

**Source**: https://savannah.nongnu.org/projects/lwip/

**Original Authors**:
- **Adam Dunkels** <adam@sics.se> - Original developer at Swedish Institute of Computer Science (SICS)
- **Leon Woestenberg** <leon.woestenberg@gmx.net> - Maintainer
- Many other contributors worldwide

**License**: BSD-style license (see individual source files for specific copyright notices)

**Note**: This project uses the default lwIP component provided by ESP-IDF without modifications.

## ESP-IDF Framework

**Component**: ESP-IDF (Espressif IoT Development Framework)

**Source**: https://github.com/espressif/esp-idf

**Copyright**: Copyright (c) 2015-2025 Espressif Systems (Shanghai) Co., Ltd.

**License**: Apache License 2.0

**Components Used**:
- lwIP networking stack (integrated by Espressif)
- FreeRTOS real-time operating system
- ESP HTTP Server (`esp_http_server`)
- ESP Network Interface (`esp_netif`)
- ESP Ethernet Driver (`esp_eth`)
- ESP Event Loop (`esp_event`)
- ESP Logging (`esp_log`)
- NVS Flash (`nvs_flash`)
- Various ESP-IDF drivers and utilities

**License Text** (Apache License 2.0):
```
Copyright 2015-2025 Espressif Systems (Shanghai) Co., Ltd.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

## FreeRTOS

**Component**: FreeRTOS Real-Time Operating System

**Source**: Included with ESP-IDF

**Copyright**: Copyright (C) 2017 Amazon.com, Inc. or its affiliates

**License**: MIT License (modified)

**Note**: FreeRTOS is included as part of ESP-IDF. See ESP-IDF documentation for specific FreeRTOS version and license details.

## EtherNet/IP Protocol Specification

**Reference**: EtherNet/IP is an industrial communication protocol managed by ODVA (Open DeviceNet Vendor Association)

**Specification**: [EtherNet/IP Specification](https://www.odva.org/technology-standards/key-technologies/ethernet-ip/)

**Implementation**: The EtherNet/IP scanner implementation in this project (`components/enip_scanner/`) was developed independently based on the public EtherNet/IP specification published by ODVA. No code was copied from other EtherNet/IP implementations.

**Note**: EtherNet/IP is a trademark of ODVA, Inc. This project is not affiliated with or endorsed by ODVA.

## Project-Specific Code

All other code in this project (excluding third-party components listed above) is:

**Copyright**: Copyright (c) 2025, Adam G. Sweeney <agsweeney@gmail.com>

**License**: MIT License

**Components**:
- `components/enip_scanner/` - EtherNet/IP scanner implementation
- `components/webui/` - Web user interface
- `components/system_config/` - System configuration management
- `main/` - Main application code

## License Compatibility

All third-party components used in this project are compatible with the MIT License used for project-specific code:

- **lwIP**: BSD-style license (compatible with MIT)
- **ESP-IDF**: Apache License 2.0 (compatible with MIT)
- **FreeRTOS**: MIT License (compatible)

## Contact

For questions about third-party component usage or licensing, please contact:
- **Project Maintainer**: Adam G. Sweeney <agsweeney@gmail.com>


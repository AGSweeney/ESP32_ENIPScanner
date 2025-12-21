# Third-Party Components and Attribution

This document provides attribution for third-party components, libraries, and code used in this project.

## lwIP TCP/IP Stack

**Component**: `components/lwip/`

**Source**: lwIP (Lightweight IP) - https://savannah.nongnu.org/projects/lwip/

**Original Authors**:
- **Adam Dunkels** <adam@sics.se> - Original developer at Swedish Institute of Computer Science (SICS)
- **Leon Woestenberg** <leon.woestenberg@gmx.net> - Maintainer
- **Dominik Spies** <kontakt@dspies.de> - ACD (Address Conflict Detection) implementation (2007)
- **Jasper Verschueren** <jasper.verschueren@apart-audio.com> - ACD improvements (2018)
- Many other contributors worldwide

**License**: BSD-style license (see individual source files for specific copyright notices)

**Modifications**: 
- This project includes a modified version of lwIP from ESP-IDF v5.5.1
- Custom modifications to `lwip/src/core/ipv4/acd.c` for RFC 5227 compliance and logging reduction
- See `components/lwip/README.md` and `components/lwip/lwip/src/core/ipv4/acd.c` for detailed modification notes

**Original License Text** (from lwIP ACD):
```
Copyright (c) 2007 Dominik Spies <kontakt@dspies.de>
Copyright (c) 2018 Jasper Verschueren <jasper.verschueren@apart-audio.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## ESP-IDF Framework

**Component**: ESP-IDF (Espressif IoT Development Framework)

**Source**: https://github.com/espressif/esp-idf

**Copyright**: Copyright (c) 2015-2025 Espressif Systems (Shanghai) Co., Ltd.

**License**: Apache License 2.0

**Components Used**:
- lwIP networking stack (integrated and modified by Espressif)
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
- `components/acd_manager/` - ACD manager wrapper (uses lwIP ACD)
- `components/system_config/` - System configuration management
- `main/` - Main application code

## Summary of Modifications

### lwIP Modifications

**File**: `components/lwip/lwip/src/core/ipv4/acd.c`

**Modifications by**: Adam G. Sweeney <agsweeney@gmail.com>

**Changes**:
1. Disabled ACD diagnostic logging to reduce log noise
2. Added ESP-IDF logging integration for ACD probe visibility
3. Added custom timing configuration support via ESP-IDF Kconfig
4. RFC 5227 compliance improvements
5. EtherNet/IP integration enhancements

**Detailed documentation**: See comments in `components/lwip/lwip/src/core/ipv4/acd.c` starting at line 90.

### ACD Manager Component

**File**: `components/acd_manager/acd_manager.c`

**Based on**: lwIP ACD module (via ESP-IDF)

**Modifications**: Application-layer wrapper providing RFC 5227 compliant behavior, retry logic, and callback management.

## License Compatibility

All third-party components used in this project are compatible with the MIT License used for project-specific code:

- **lwIP**: BSD-style license (compatible with MIT)
- **ESP-IDF**: Apache License 2.0 (compatible with MIT)
- **FreeRTOS**: MIT License (compatible)

## Contact

For questions about third-party component usage or licensing, please contact:
- **Project Maintainer**: Adam G. Sweeney <agsweeney@gmail.com>
- **lwIP**: https://savannah.nongnu.org/projects/lwip/
- **ESP-IDF**: https://github.com/espressif/esp-idf
- **ODVA** (EtherNet/IP): https://www.odva.org/


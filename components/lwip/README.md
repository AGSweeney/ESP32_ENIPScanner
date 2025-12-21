# lwIP Component Override

This directory contains a **custom override** of ESP-IDF's lwIP component.

## Why This Override Exists

This project uses a modified version of lwIP with custom changes for:
- **RFC 5227 Compliance**: Address Conflict Detection (ACD) improvements
- **Performance Optimizations**: Task affinity and timing configurations
- **Logging Reduction**: Disabled verbose ACD diagnostic logging

## How It Works

ESP-IDF's component resolution system automatically prioritizes components in the project's `components/` directory over system components. This means:

1. **Project components** (`components/lwip/`) are checked first
2. **System components** (`$IDF_PATH/components/lwip/`) are used as fallback

Since this directory exists, ESP-IDF will use this version instead of the system version.

## Modified Files

- `lwip/src/core/ipv4/acd.c` - Modified with:
  - Disabled ACD diagnostic logging (reduced log noise)
  - RFC 5227 compliant improvements
  - Active IP defense with periodic ARP probes
  - EtherNet/IP conflict reporting integration
  - Custom timing configuration via Kconfig
  - Natural state machine flow (PROBE_WAIT → PROBING → ANNOUNCE_WAIT → ANNOUNCING → ONGOING)
  - `ACD_IP_OK` callback fires after announce phase completes (when in ONGOING state)

## Original Source

This component was copied from ESP-IDF v5.5.1:
- **Source**: `$IDF_PATH/components/lwip/`
- **Date**: Copied for project-specific modifications

### Original lwIP Attribution

lwIP is a small independent implementation of the TCP/IP protocol suite, originally developed by Adam Dunkels at the Swedish Institute of Computer Science (SICS). lwIP is now maintained by a worldwide network of developers.

**lwIP License**: lwIP is provided under a BSD-style license. See individual source files for specific copyright notices.

**Original lwIP Authors**:
- Adam Dunkels <adam@sics.se>
- Leon Woestenberg <leon.woestenberg@gmx.net>
- And many other contributors

**lwIP Website**: https://savannah.nongnu.org/projects/lwip/

### ESP-IDF Attribution

This lwIP component was integrated and modified by Espressif Systems as part of ESP-IDF.

**ESP-IDF License**: Apache License 2.0
**Copyright**: Copyright (c) 2015-2025 Espressif Systems (Shanghai) Co., Ltd.

### Project Modifications

Modifications made by Adam G. Sweeney <agsweeney@gmail.com>:
- RFC 5227 compliance improvements
- ACD diagnostic logging reduction
- Custom timing configuration support
- EtherNet/IP integration enhancements

## Modifications Documentation

See `dependency_modifications/LWIP_MODIFICATIONS.md` for detailed documentation of all changes made to the lwIP stack.

## Important Notes

- **DO NOT** delete the original ESP-IDF component (`$IDF_PATH/components/lwip/`) - other projects may need it
- All modifications are version-controlled with this project
- When upgrading ESP-IDF, you may need to merge changes from the new version


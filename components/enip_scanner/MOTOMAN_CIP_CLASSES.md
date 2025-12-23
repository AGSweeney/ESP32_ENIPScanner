# Motoman Vendor-Specific CIP Classes

[![GitHub](https://img.shields.io/badge/GitHub-Repository-blue)](https://github.com/AGSweeney/ESP32_ENIPScanner)

This document describes the Motoman vendor-specific CIP classes available for explicit messaging with Motoman DX200 robot controllers via EtherNet/IP.

**Reference**: Motoman Manual 165838-1CD, Section 5.2 "Message Communication Using CIP"

## Overview

Motoman robots support vendor-specific CIP classes for explicit messaging that allow reading robot status, alarms, positions, variables, and I/O data. These classes use the standard EtherNet/IP explicit messaging protocol (SendRRData) but with Motoman-specific class numbers and data formats.

## Available CIP Classes

| Class Number | Function | Read/Write | Description |
|--------------|----------|------------|-------------|
| 0x70 | Read a currently occurring alarm | Read Only | Read current alarm information |
| 0x71 | Read an alarm history | Read Only | Read historical alarm data |
| 0x72 | Read the current status | Read Only | Read robot operational status |
| 0x73 | Read the current active job information | Read Only | Read active job details |
| 0x74 | Read the current axis configuration | Read Only | Read axis configuration data |
| 0x75 | Read the current robot position | Read Only | Read current robot position |
| 0x76 | Read the deviation of each axis position | Read Only | Read position deviation data |
| 0x77 | Read the torque of each axis | Read Only | Read axis torque values |
| 0x78 | Read and write IO data | Read/Write | Access robot I/O signals |
| 0x79 | Read and write register data | Read/Write | Access robot registers |
| 0x7A | Read and write a byte-type variable (B) | Read/Write | Access byte variables |
| 0x7B | Read and write an integer-type variable (I) | Read/Write | Access integer variables |
| 0x7C | Read and write a double precision integer-type variable (D) | Read/Write | Access double integer variables |
| 0x7D | Read and write a real-type variable (R) | Read/Write | Access real/float variables |
| 0x8C | Read and write a character-type variable (S) | Read/Write | Access string variables |
| 0x7F | Read and write a robot position-type variable (P) | Read/Write | Access position variables |
| 0x80 | Read and write a base position-type variable (BP) | Read/Write | Access base position variables |
| 0x81 | Read and write an external axis position-type variable (EX) | Read/Write | Access external axis variables |

## Using with EtherNet/IP Scanner Component

The EtherNet/IP Scanner component provides the underlying infrastructure for sending CIP messages via `enip_scanner_read_assembly()` and `enip_scanner_write_assembly()`, but these functions are designed for standard Assembly objects (Class 0x04).

To use Motoman vendor-specific classes, you would need to:

1. **Use low-level CIP messaging** (not currently exposed in the public API)
2. **Extend the component** to add specific functions for these classes
3. **Use raw socket communication** with EtherNet/IP SendRRData commands

### Example: Reading Robot Status (Class 0x72)

The following shows the conceptual structure (actual implementation would require extending the component):

```c
// CIP Path for Class 0x72, Instance 1, Attribute 1
// Path: [0x20 0x72] [0x24 0x01] [0x30 0x01]
// Service: 0x0E (Get_Attribute_Single) or 0x01 (Get_Attribute_All)

// Request Format:
// - Class: 0x72 (2 bytes)
// - Instance: 1 (2 bytes) 
// - Attribute: 1-2 (1 byte)
//   - 1: Data 1 (status bits)
//   - 2: Data 2 (status bits)
// - Service: 0x0E (Get_Attribute_Single) or 0x01 (Get_Attribute_All)

// Response Format (Get_Attribute_All):
// - Data 1 (4 bytes): Status bits
//   - bit 0: Step
//   - bit 1: 1 cycle
//   - bit 2: Auto
//   - bit 3: Running
//   - bit 4: Safety speed operation
//   - bit 5: Teach
//   - bit 6: Play
//   - bit 7: Command remote
// - Data 2 (4 bytes): Additional status bits
//   - bit 0: System-reserved
//   - bit 1: Hold (Programming pendant)
//   - bit 2: Hold (external)
//   - bit 3: Hold (Command)
//   - bit 4: Alarm
//   - bit 5: Error
//   - bit 6: Servo on
//   - bit 7: System-reserved
```

### Example: Reading I/O Data (Class 0x78)

```c
// CIP Path for Class 0x78, Instance (signal number/10), Attribute 1
// Instance ranges:
// - 1-256: General input
// - 1001-1256: General output
// - 2001-2256: External input
// - 2501-2756: Network input (writable)
// - 3001-3256: External output
// - 3501-3756: Network output
// - 4001-4160: Specific input
// - 5001-5200: Specific output
// - 6001-6064: Interface panel input
// - 7001-7999: Auxiliary relay
// - 8001-8064: Control status
// - 8201-8220: Pseudo input

// Service: 0x0E (Get_Attribute_Single) for read
// Service: 0x10 (Set_Attribute_Single) for write
```

## Implementation Notes

1. **CIP Path Format**: Vendor-specific classes use the same CIP path encoding as standard classes:
   - `0x20` = Class segment
   - `0x24` = Instance segment  
   - `0x30` = Attribute segment

2. **Services**: Standard CIP services are used:
   - `0x01` = Get_Attribute_All
   - `0x0E` = Get_Attribute_Single
   - `0x10` = Set_Attribute_Single

3. **EtherNet/IP Protocol**: Uses standard SendRRData (0x006F) command with CIP message payload

4. **Data Formats**: Refer to Motoman Manual 165838-1CD, Section 5.2.1 for detailed request/response formats

## Related Documentation

- **Motoman Manual**: 165838-1CD "EtherNet/IP Communication Function"
- **Component API**: [API_DOCUMENTATION.md](API_DOCUMENTATION.md)
- **Component README**: [README.md](README.md)

## Future Enhancements

Potential additions to the component:
- `enip_scanner_read_motoman_status()` - Read robot status (Class 0x72)
- `enip_scanner_read_motoman_alarm()` - Read alarms (Classes 0x70, 0x71)
- `enip_scanner_read_motoman_position()` - Read position (Class 0x75)
- `enip_scanner_read_motoman_io()` - Read I/O (Class 0x78)
- Generic `enip_scanner_send_cip_message()` - Low-level CIP messaging API

## License

See LICENSE file in project root.


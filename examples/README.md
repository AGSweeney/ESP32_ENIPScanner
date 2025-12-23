# EtherNet/IP Scanner Examples

This directory contains example code demonstrating various uses of the EtherNet/IP Scanner component.

## Available Examples

### Real-World Bidirectional Translator: Micro800 ↔ Motoman DX200 Robot

**File**: `micro800_motoman_translator.c`

**Description**: Production-ready bidirectional translator for a pick-and-place application where a Micro800 PLC controls a Motoman DX200 robot via EtherNet/IP. This example demonstrates real-world usage of Motoman CIP classes and I/O signals.

**Real-World Scenario**:
This example implements a complete translator for a production pick-and-place system:
- PLC sets job number and start command via tags
- ESP32 reads PLC tags and writes to robot I/O signals (CIP Class 0x78)
- ESP32 reads robot status (CIP Class 0x72) and reports back to PLC
- On errors, ESP32 reads alarm information (CIP Class 0x70)
- Edge-triggered commands prevent continuous triggering
- Comprehensive error handling and statistics

**Features**:
- **PLC → Robot**: 
  - Reads job number, start/stop/reset commands from PLC tags
  - Writes to Motoman General Output I/O signals (1001-1004)
  - Edge detection for start/reset commands (prevents continuous triggering)
- **Robot → PLC**: 
  - Reads robot status using Motoman CIP Class 0x72 (Running, Error, Hold, Servo On)
  - Reads I/O signals for job complete status
  - Writes status back to PLC tags for HMI display
  - Reads alarm codes on error conditions
- **Communication Methods**:
  - Motoman CIP Class 0x72: Robot status (Running, Error, Hold, Alarm, Servo On)
  - Motoman CIP Class 0x78: I/O signal read/write (General Input/Output)
  - Motoman CIP Class 0x70: Alarm reading (on error)
  - Optional: Assembly I/O (Class 0x04) for high-speed cyclic I/O
- **Error Handling**:
  - Connection verification on startup
  - Consecutive error counting
  - Comprehensive statistics logging
  - Timeout handling

**Real-World I/O Mapping**:
```
PLC Tags                    ESP32 Translator              Motoman I/O Signals
─────────────────           ─────────────────             ────────────────────
RobotJobNumber (DINT) ──Read──>                    ──Write──> General Output 1004
RobotStartCmd (BOOL)  ──Read──>                    ──Write──> General Output 1001
RobotStopCmd (BOOL)   ──Read──>                    ──Write──> General Output 1002
RobotResetCmd (BOOL)  ──Read──>                    ──Write──> General Output 1003
                                                              ──Read──>
                                                              CIP Class 0x72
                                                              (Status)
                                                              ──Read──>
                                                              General Input 1-4
                                                              ──Read──>
RobotRunning (BOOL)    <──Write──                    <──Read──
RobotError (BOOL)      <──Write──                    <──Read──
RobotJobComplete (BOOL)<──Write──                    <──Read──
RobotCurrentJob (DINT) <──Write──                    <──Read──
```

**Usage**:

1. **Configure Network**:
   ```c
   #define PLC_IP_ADDRESS "192.168.1.100"      // Your Micro800 PLC IP
   #define MOTOMAN_IP_ADDRESS "192.168.1.200"   // Your Motoman robot IP
   ```

2. **Configure I/O Signals** (match your robot I/O mapping):
   ```c
   #define MOTOMAN_IO_START_CMD 1001      // General Output for start
   #define MOTOMAN_IO_STOP_CMD 1002       // General Output for stop
   #define MOTOMAN_IO_RESET_CMD 1003      // General Output for reset
   #define MOTOMAN_IO_JOB_NUMBER 1004    // General Output for job number
   #define MOTOMAN_IO_RUNNING 1           // General Input for running status
   #define MOTOMAN_IO_JOB_COMPLETE 2      // General Input for job complete
   ```

3. **Configure PLC Tags** (must match your PLC program exactly):
   ```c
   #define PLC_TAG_JOB_NUMBER "RobotJobNumber"      // DINT tag
   #define PLC_TAG_START_COMMAND "RobotStartCmd"    // BOOL tag
   #define PLC_TAG_STOP_COMMAND "RobotStopCmd"      // BOOL tag
   #define PLC_TAG_RESET_COMMAND "RobotResetCmd"    // BOOL tag
   ```

4. **Enable Required Features**:
   ```
   idf.py menuconfig
   Component config → EtherNet/IP Scanner Configuration:
     ✓ Enable Allen-Bradley tag support
     ✓ Enable Motoman robot CIP class support
   ```

5. **Build and Flash**:
   ```bash
   idf.py build flash monitor
   ```

**Integration Example**:

```c
#include "micro800_motoman_translator.c"

void app_main(void)
{
    // Initialize network (DHCP or static IP)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Initialize EtherNet/IP scanner
    enip_scanner_init();
    
    // Start translator task
    translator_init();
    
    // Your application code here...
}
```

**PLC Program Example** (Micro800):
```
// Global Variables
RobotJobNumber : DINT;      // Job number to execute (0-255)
RobotStartCmd : BOOL;       // Start command (edge-triggered)
RobotStopCmd : BOOL;        // Stop command
RobotResetCmd : BOOL;       // Reset command

// Status tags (read-only from ESP32)
RobotRunning : BOOL;        // Robot is running
RobotError : BOOL;          // Robot has error
RobotJobComplete : BOOL;    // Job completed
RobotHoldActive : BOOL;     // Hold is active
RobotCurrentJob : DINT;     // Currently executing job
RobotErrorCode : DINT;      // Error code (0 = no error)
RobotAlarmCode : DINT;      // Alarm code (0 = no alarm)
RobotServoOn : BOOL;        // Servo motors enabled
```

**Motoman Robot Configuration**:
1. Configure EtherNet/IP adapter in robot controller
2. Map I/O signals:
   - General Output 1001: Start command
   - General Output 1002: Stop command
   - General Output 1003: Reset command
   - General Output 1004: Job number (0-255)
   - General Input 1: Running status
   - General Input 2: Job complete status
   - General Input 3: Error present
   - General Input 4: Hold active
3. Configure robot job to respond to these I/O signals

**Troubleshooting**:
- **Connection Issues**: Check IP addresses and network connectivity
- **Tag Read Failures**: Verify tag names match PLC program exactly (case-sensitive)
- **I/O Write Failures**: Verify I/O signal numbers match robot configuration
- **Status Not Updating**: Check robot CIP communication is enabled
- **Commands Not Working**: Verify I/O signals are mapped correctly in robot

**See Also**:
- [API Documentation](../components/enip_scanner/API_DOCUMENTATION.md) - Complete API reference
- [Translator Design](../components/enip_scanner/TRANSLATOR_DESIGN.md) - Design considerations
- [Motoman CIP Classes](../components/enip_scanner/MOTOMAN_CIP_CLASSES.md) - CIP class reference

**See Also**:
- [API Documentation](../components/enip_scanner/API_DOCUMENTATION.md) - Complete API reference
- [Translator Design](../components/enip_scanner/TRANSLATOR_DESIGN.md) - Design considerations
- [Motoman CIP Classes](../components/enip_scanner/MOTOMAN_CIP_CLASSES.md) - Advanced features

## Other Examples

Additional examples demonstrating the EtherNet/IP Scanner component usage can be found in the [API Documentation](../components/enip_scanner/API_DOCUMENTATION.md), which includes complete code examples for:

- Device discovery
- Assembly read/write operations
- Tag read/write operations (Micro800 PLCs)
- GPIO I/O mapping
- Error handling patterns

## License

See LICENSE file in project root.


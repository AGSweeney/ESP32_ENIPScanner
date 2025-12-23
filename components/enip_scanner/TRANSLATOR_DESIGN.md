# EtherNet/IP Translator Design: Micro800 Tags ↔ Motoman Robot

[![GitHub](https://img.shields.io/badge/GitHub-Repository-blue)](https://github.com/AGSweeney/ESP32_ENIPScanner)

This document outlines design considerations for creating a translator that bridges communication between Allen-Bradley Micro800 PLCs (tag-based) and Motoman DX200 robot controllers.

## Real-World Example: Pick-and-Place Application

The included `micro800_motoman_translator.c` example demonstrates a translator implementation for a pick-and-place application:

**Note**: This is example code and has not been tested in production environments. Use as a reference for implementing your own translator application.

**Application Flow**:
1. PLC operator selects job number and presses start button
2. ESP32 reads PLC tags and writes job number + start command to robot I/O signals
3. Robot executes job (pick part, place part)
4. ESP32 continuously reads robot status and reports back to PLC
5. On completion or error, ESP32 reads alarm/error codes and updates PLC tags
6. PLC HMI displays robot status, current job, and any errors

**Key Features Demonstrated**:
- Edge-triggered commands (prevents continuous triggering)
- Real-time status monitoring using CIP Class 0x72
- I/O signal control using CIP Class 0x78
- Error handling and alarm reading
- Connection verification on startup
- Statistics and logging

## Architecture Overview

```
┌─────────────┐                    ┌──────────────┐                    ┌─────────────┐
│  Micro800   │                    │   ESP32      │                    │   Motoman   │
│    PLC      │───Tag Read/Write──>│  Translator  │───Assembly/CIP───>│  Controller │
│             │                    │              │                    │             │
│  Tags:      │                    │  Component:  │                    │ Options:    │
│  - MyTag    │                    │  enip_scanner│                    │ - Assembly  │
│  - Counter  │                    │              │                    │   (Class 0x04)│
│  - Status   │                    │              │                    │ - CIP Msgs  │
│             │                    │              │                    │   (0x70-0x81)│
└─────────────┘                    └──────────────┘                    └─────────────┘
```

## Communication Options

### Option 1: Assembly I/O (Class 0x04) - Recommended for I/O Control

**Use Case**: Real-time I/O control, job commands, status feedback

**Advantages**:
- Fast, cyclic communication
- Standard EtherNet/IP protocol
- Well-suited for control signals
- Component already supports this

**Limitations**:
- Requires pre-configuration of assembly instances on robot
- Fixed data structure (bytes)
- For direct access to robot variables/registers, use CIP Messages (Option 2)

**Example Flow**:
```
PLC Tag "RobotStart" (BOOL) → ESP32 → Motoman Assembly Instance 101, Byte 0, Bit 0
PLC Tag "JobNumber" (DINT) → ESP32 → Motoman Assembly Instance 101, Bytes 1-4
Motoman Assembly Instance 102, Byte 0 → ESP32 → PLC Tag "RobotRunning" (BOOL)
```

### Option 2: CIP Message Communication (Classes 0x70-0x81) - Advanced Features ✅ **FULLY IMPLEMENTED**

**Use Case**: Reading robot status, alarms, positions, variables, registers

**Advantages**:
- ✅ **All 18 Motoman CIP classes implemented**
- Access to robot status (Class 0x72) - `enip_scanner_motoman_read_status()`
- Read alarms (Classes 0x70, 0x71) - `enip_scanner_motoman_read_alarm()`, `read_alarm_history()`
- Read robot position (Class 0x75) - `enip_scanner_motoman_read_position()`
- Access robot variables/registers (Classes 0x78-0x81) - Full variable support (B, I, D, R, S, P, BP, EX)
- Read job information (Class 0x73) - `enip_scanner_motoman_read_job_info()`
- Read axis configuration (Class 0x74) - `enip_scanner_motoman_read_axis_config()`
- Read position deviation (Class 0x76) - `enip_scanner_motoman_read_position_deviation()`
- Read torque (Class 0x77) - `enip_scanner_motoman_read_torque()`
- More flexible than assembly I/O

**Limitations**:
- Explicit messaging (slower than cyclic I/O)
- More complex implementation than assembly I/O

**Example Flow**:
```
PLC Tag "ReadRobotStatus" (BOOL) → ESP32 → Motoman Class 0x72 → PLC Tag "RobotStatus" (DINT)
PLC Tag "ReadPosition" (BOOL) → ESP32 → Motoman Class 0x75 → PLC Tag "RobotX" (REAL)
```

### Option 3: Hybrid Approach - Best of Both Worlds

**Use Case**: Real-time control via Assembly I/O + Status/Data via CIP Messages

**Architecture**:
- **Assembly I/O**: Fast cyclic communication for control signals
- **CIP Messages**: On-demand reading of status, alarms, positions

## Design Questions

Before implementing, consider:

### 1. Data Direction
- **Unidirectional**: PLC → Robot only (write commands)
- **Bidirectional**: PLC ↔ Robot (commands + status feedback)
- **Bidirectional with Aggregation**: Multiple PLC tags → Single robot assembly

### 2. Data Types to Translate
- **Control Commands**: Start, Stop, Reset, Job Selection
- **Setpoints**: Position, Speed, Configuration
- **Status Feedback**: Running, Error, Position, Alarms
- **Variables**: Robot variables (B, I, D, R, S, P, BP, EX)

### 3. Communication Method
- **Assembly I/O**: For real-time control (recommended for I/O)
- **CIP Messages**: For status/alarms/positions (✅ **All 18 classes implemented**)
- **Hybrid**: Both methods (recommended for best performance + features)

### 4. Translation Frequency
- **Cyclic**: Continuous polling (e.g., every 100ms)
- **Event-driven**: On PLC tag change
- **On-demand**: Triggered by specific PLC signals

### 5. Error Handling
- **Retry Logic**: How many retries on failure?
- **Timeout Handling**: What to do on timeout?
- **Data Validation**: Validate data before translation?
- **Status Reporting**: Report translation status to PLC?

## Implementation Approaches

### Approach A: Simple Tag-to-Assembly Translator

**Scope**: Basic I/O translation using existing component APIs

**Components**:
- Read tags from Micro800 using `enip_scanner_read_tag()`
- Write to Motoman assemblies using `enip_scanner_write_assembly()`
- Read from Motoman assemblies using `enip_scanner_read_assembly()`
- Write tags to Micro800 using `enip_scanner_write_tag()`

**Pros**:
- Uses existing component APIs
- Quick to implement
- Well-tested code paths

**Cons**:
- Limited to Assembly I/O
- Cannot access advanced robot features (status, alarms, positions) without CIP Messages

### Approach B: Extended Translator with CIP Message Support ✅ **IMPLEMENTED**

**Scope**: Full-featured translator with Motoman CIP class support

**Status**: ✅ **Complete** - All 18 Motoman CIP classes are now implemented in the component.

**Components**:
- All of Approach A, plus:
- ✅ Motoman CIP class support (all 18 classes)
- ✅ `enip_scanner_motoman_read_status()` - Robot status (Class 0x72)
- ✅ `enip_scanner_motoman_read_alarm()` / `read_alarm_history()` - Alarms (Classes 0x70, 0x71)
- ✅ `enip_scanner_motoman_read_job_info()` - Job information (Class 0x73)
- ✅ `enip_scanner_motoman_read_axis_config()` - Axis configuration (Class 0x74)
- ✅ `enip_scanner_motoman_read_position()` - Robot position (Class 0x75)
- ✅ `enip_scanner_motoman_read_position_deviation()` - Position deviation (Class 0x76)
- ✅ `enip_scanner_motoman_read_torque()` - Axis torque (Class 0x77)
- ✅ `enip_scanner_motoman_read_io()` / `write_io()` - I/O signals (Class 0x78)
- ✅ `enip_scanner_motoman_read_register()` / `write_register()` - Registers (Class 0x79)
- ✅ `enip_scanner_motoman_read_variable_*()` / `write_variable_*()` - Variables (Classes 0x7A-0x81)
- ✅ Example translator: `examples/micro800_motoman_translator.c`

**Pros**:
- Full access to robot capabilities
- Can read status, alarms, positions, variables
- More flexible than assembly-only approach
- ✅ **Ready to use** - All APIs implemented

**Cons**:
- More complex implementation than Approach A
- Explicit messaging slower than cyclic I/O

### Approach C: Generic CIP Message API

**Scope**: Low-level CIP messaging API for any vendor-specific classes

**Components**:
- Add generic `enip_scanner_send_cip_message()` function
- Build Motoman-specific wrappers on top
- Reusable for other vendor-specific protocols

**Pros**:
- Most flexible approach
- Reusable for other devices
- Future-proof

**Cons**:
- Most complex to implement
- Requires deep CIP knowledge
- More error-prone

## Recommended Implementation Plan

### Phase 1: Basic Tag-to-Assembly Translator (Approach A)

1. **Create translator task/function**
   - Poll PLC tags at configurable interval
   - Translate tag data to assembly format
   - Write to Motoman assembly instances
   - Read from Motoman assembly instances
   - Write status back to PLC tags

2. **Configuration**
   - PLC IP address
   - Motoman IP address
   - Tag-to-assembly mapping table
   - Assembly instance numbers (discovered or configured)
   - Polling interval

3. **Data Mapping**
   - Define data structures for assembly layouts
   - Map PLC tags to assembly bytes/bits
   - Handle data type conversions

### Phase 2: Enhanced Features (Optional)

1. **Error Handling**
   - Retry logic
   - Error counters
   - Status reporting

2. **Data Validation**
   - Range checking
   - Type validation
   - Safety checks

3. **Performance Monitoring**
   - Translation statistics
   - Response time tracking
   - Error rate monitoring

### Phase 3: CIP Message Support (Approach B) ✅ **COMPLETE**

**Status**: ✅ **All Motoman CIP classes implemented** - Phase 3 is complete and ready to use.

1. **✅ Component API Extended**
   - ✅ All 18 Motoman CIP class functions implemented
   - ✅ See [MOTOMAN_CIP_CLASSES.md](MOTOMAN_CIP_CLASSES.md) for complete list

2. **✅ Status Reading Implemented**
   - ✅ Robot status (Class 0x72) - `enip_scanner_motoman_read_status()`
   - ✅ Alarms (Classes 0x70, 0x71) - `enip_scanner_motoman_read_alarm()`, `read_alarm_history()`
   - ✅ Position (Class 0x75) - `enip_scanner_motoman_read_position()`
   - ✅ Job information (Class 0x73) - `enip_scanner_motoman_read_job_info()`
   - ✅ Axis configuration (Class 0x74) - `enip_scanner_motoman_read_axis_config()`
   - ✅ Position deviation (Class 0x76) - `enip_scanner_motoman_read_position_deviation()`
   - ✅ Torque (Class 0x77) - `enip_scanner_motoman_read_torque()`

3. **✅ Variable Access Implemented**
   - ✅ Robot variables (Classes 0x7A-0x81) - All variable types supported
   - ✅ Registers (Class 0x79) - `enip_scanner_motoman_read_register()`, `write_register()`
   - ✅ I/O data (Class 0x78) - `enip_scanner_motoman_read_io()`, `write_io()`

**Example Implementation**: See `examples/micro800_motoman_translator.c` for a working example using Classes 0x70, 0x72, and 0x78.

## Complete API Reference

### All 18 Motoman CIP Classes - Function Reference

**Alarm Functions:**
- `enip_scanner_motoman_read_alarm(ip, instance, alarm, timeout)` - Class 0x70: Read current alarm
- `enip_scanner_motoman_read_alarm_history(ip, instance, alarm, timeout)` - Class 0x71: Read alarm history

**Status and Information:**
- `enip_scanner_motoman_read_status(ip, status, timeout)` - Class 0x72: Read robot status
- `enip_scanner_motoman_read_job_info(ip, job_info, timeout)` - Class 0x73: Read active job information
- `enip_scanner_motoman_read_axis_config(ip, control_group, config, timeout)` - Class 0x74: Read axis configuration
- `enip_scanner_motoman_read_position(ip, control_group, position, timeout)` - Class 0x75: Read robot position
- `enip_scanner_motoman_read_position_deviation(ip, control_group, deviation, timeout)` - Class 0x76: Read position deviation
- `enip_scanner_motoman_read_torque(ip, control_group, torque, timeout)` - Class 0x77: Read axis torque

**I/O and Registers:**
- `enip_scanner_motoman_read_io(ip, signal_number, value, timeout)` - Class 0x78: Read I/O signal
- `enip_scanner_motoman_write_io(ip, signal_number, value, timeout)` - Class 0x78: Write I/O signal
- `enip_scanner_motoman_read_register(ip, register_number, value, timeout)` - Class 0x79: Read register
- `enip_scanner_motoman_write_register(ip, register_number, value, timeout)` - Class 0x79: Write register

**Variable Access (8 Types):**
- `enip_scanner_motoman_read_variable_b()` / `write_variable_b()` - Class 0x7A: Byte variables (B)
- `enip_scanner_motoman_read_variable_i()` / `write_variable_i()` - Class 0x7B: Integer variables (I)
- `enip_scanner_motoman_read_variable_d()` / `write_variable_d()` - Class 0x7C: Double integer variables (D)
- `enip_scanner_motoman_read_variable_r()` / `write_variable_r()` - Class 0x7D: Real/float variables (R)
- `enip_scanner_motoman_read_variable_s()` / `write_variable_s()` - Class 0x8C: String variables (S)
- `enip_scanner_motoman_read_variable_p()` / `write_variable_p()` - Class 0x7F: Position variables (P)
- `enip_scanner_motoman_read_variable_bp()` / `write_variable_bp()` - Class 0x80: Base position variables (BP)
- `enip_scanner_motoman_read_variable_ex()` / `write_variable_ex()` - Class 0x81: External axis variables (EX)

See [API_DOCUMENTATION.md](API_DOCUMENTATION.md) for complete function signatures and parameters.

## Example Use Cases

### Use Case 1: Job Control (Assembly I/O)
```
PLC Tags:
  - JobNumber (DINT)
  - StartJob (BOOL)
  - StopJob (BOOL)

Motoman Assembly Instance 101:
  - Byte 0: Job Number (0-255)
  - Byte 1, Bit 0: Start command
  - Byte 1, Bit 1: Stop command

Motoman Assembly Instance 102:
  - Byte 0, Bit 0: Job Running
  - Byte 0, Bit 1: Job Complete
  - Byte 1: Error Code
```

### Use Case 2: Job Control (CIP Class 0x73 - Job Information)
```
PLC Tags:
  - ReadJobInfo (BOOL) - Trigger
  - JobName (STRING) - Result
  - JobLineNumber (DINT) - Result
  - JobStep (DINT) - Result
  - SpeedOverride (REAL) - Result

Motoman CIP Class 0x73:
  - Read active job information using enip_scanner_motoman_read_job_info()
  - Returns: Job name (32 chars), line number, step, speed override (0-100%)
  
Example Code:
  enip_scanner_motoman_job_info_t job_info;
  enip_scanner_motoman_read_job_info(&robot_ip, &job_info, 5000);
  // Write job_info.job_name to PLC Tag "JobName"
  // Write job_info.line_number to PLC Tag "JobLineNumber"
  // Write job_info.step to PLC Tag "JobStep"
  // Write job_info.speed_override to PLC Tag "SpeedOverride"
```

### Use Case 3: Position Control (Assembly I/O)
```
PLC Tags:
  - TargetX (REAL)
  - TargetY (REAL)
  - MoveCommand (BOOL)

Motoman Assembly Instance 101:
  - Bytes 0-3: Target X (REAL)
  - Bytes 4-7: Target Y (REAL)
  - Byte 8, Bit 0: Move command

Motoman Assembly Instance 102:
  - Bytes 0-3: Current X (REAL)
  - Bytes 4-7: Current Y (REAL)
  - Byte 8, Bit 0: Move complete
```

### Use Case 4: Position Control (CIP Class 0x75 - Robot Position)
```
PLC Tags:
  - ReadPosition (BOOL) - Trigger
  - RobotX (REAL) - Result
  - RobotY (REAL) - Result
  - RobotZ (REAL) - Result
  - RobotRX (REAL) - Result
  - RobotRY (REAL) - Result
  - RobotRZ (REAL) - Result
  - RobotConfig (DINT) - Result

Motoman CIP Class 0x75:
  - Read current robot position using enip_scanner_motoman_read_position()
  - Returns: X, Y, Z, RX, RY, RZ (all REAL), Configuration (DINT)
  - Control group: 0 = Robot, 1+ = External axes
  
Example Code:
  enip_scanner_motoman_position_t position;
  enip_scanner_motoman_read_position(&robot_ip, 0, &position, 5000);
  // Write position.x, position.y, position.z to PLC Tags
  // Write position.rx, position.ry, position.rz to PLC Tags
  // Write position.config to PLC Tag "RobotConfig"
```

### Use Case 5: Position Variables (CIP Class 0x7F - Position Variable)
```
PLC Tags:
  - ReadPosVar (BOOL) - Trigger
  - WritePosVar (BOOL) - Trigger
  - PosVarNumber (DINT) - Variable number (e.g., 1 for P001)
  - PosVarX (REAL) - Result/Write value
  - PosVarY (REAL) - Result/Write value
  - PosVarZ (REAL) - Result/Write value
  - PosVarRX (REAL) - Result/Write value
  - PosVarRY (REAL) - Result/Write value
  - PosVarRZ (REAL) - Result/Write value
  - PosVarConfig (DINT) - Result/Write value

Motoman CIP Class 0x7F:
  - Read/write position variables (P001, P002, etc.)
  - Variable number: 1 = P001, 2 = P002, etc.
  
Example Code:
  // Read position variable P001
  enip_scanner_motoman_position_t pos_var;
  enip_scanner_motoman_read_variable_p(&robot_ip, 1, &pos_var, 5000);
  // Write pos_var values to PLC Tags
  
  // Write position variable P002
  enip_scanner_motoman_position_t new_pos;
  new_pos.x = 100.0; new_pos.y = 200.0; new_pos.z = 300.0;
  new_pos.rx = 0.0; new_pos.ry = 0.0; new_pos.rz = 0.0;
  new_pos.config = 0;
  enip_scanner_motoman_write_variable_p(&robot_ip, 2, &new_pos, 5000);
```

### Use Case 6: Status Monitoring (CIP Class 0x72)
```
PLC Tags:
  - ReadStatus (BOOL) - Trigger
  - RobotRunning (BOOL) - Result (bit 3)
  - RobotError (BOOL) - Result (bit 5)
  - RobotHold (BOOL) - Result (bit 4)
  - RobotAlarm (BOOL) - Result (bit 7)
  - ServoOn (BOOL) - Result (bit 6)

Motoman CIP Class 0x72:
  - Read robot status using enip_scanner_motoman_read_status()
  - Status bits: data1[3]=Running, data2[5]=Error, data2[4]=Hold, 
                 data2[7]=Alarm, data2[6]=Servo On
  
Example Code:
  enip_scanner_motoman_status_t status;
  enip_scanner_motoman_read_status(&robot_ip, &status, 5000);
  bool running = (status.data1 & 0x08) != 0;  // Bit 3
  bool error = (status.data2 & 0x20) != 0;    // Bit 5
  bool servo_on = (status.data2 & 0x40) != 0;  // Bit 6
  // Write to PLC Tags
```

### Use Case 7: Alarm Reading (CIP Classes 0x70, 0x71)
```
PLC Tags:
  - ReadAlarm (BOOL) - Trigger (current alarm)
  - ReadAlarmHistory (BOOL) - Trigger (historical alarm)
  - AlarmInstance (DINT) - Instance number (0 for current, 1+ for history)
  - AlarmCode (DINT) - Result
  - AlarmData (DINT) - Result
  - AlarmType (DINT) - Result
  - AlarmDateTime (STRING) - Result (16 chars)
  - AlarmString (STRING) - Result (32 chars)

Motoman CIP Classes:
  - Class 0x70: Read current alarm (instance 0)
  - Class 0x71: Read alarm history (instance 1+)
  
Example Code:
  // Read current alarm
  enip_scanner_motoman_alarm_t alarm;
  enip_scanner_motoman_read_alarm(&robot_ip, 0, &alarm, 5000);
  // Write alarm.alarm_code, alarm.alarm_string to PLC Tags
  
  // Read alarm history entry 1
  enip_scanner_motoman_read_alarm_history(&robot_ip, 1, &alarm, 5000);
```

### Use Case 8: I/O Signal Control (CIP Class 0x78)
```
PLC Tags:
  - WriteIOSignal (BOOL) - Trigger
  - ReadIOSignal (BOOL) - Trigger
  - IOSignalNumber (DINT) - Signal number (e.g., 1001-1004 for General Output)
  - IOSignalValue (BOOL) - Value to write / Result
  - IOSignalType (DINT) - 0=General Input, 1=General Output, 2=Network Input, 3=Network Output

Motoman CIP Class 0x78:
  - Read/write I/O signals
  - Signal ranges: General Input (1-999), General Output (1001-1999),
                   Network Input (2001-2999), Network Output (3001-3999)
  
Example Code:
  // Write General Output 1001 (start command)
  uint16_t signal_value = 1;
  enip_scanner_motoman_write_io(&robot_ip, 1001, signal_value, 5000);
  
  // Read General Input 1 (running status)
  uint16_t read_value;
  enip_scanner_motoman_read_io(&robot_ip, 1, &read_value, 5000);
  // Write read_value to PLC Tag
```

### Use Case 9: Register Access (CIP Class 0x79)
```
PLC Tags:
  - ReadRegister (BOOL) - Trigger
  - WriteRegister (BOOL) - Trigger
  - RegisterNumber (DINT) - Register number (e.g., 1 for R001)
  - RegisterValue (DINT) - Value to write / Result

Motoman CIP Class 0x79:
  - Read/write robot registers (R001, R002, etc.)
  - Register number: 1 = R001, 2 = R002, etc.
  
Example Code:
  // Read register R001
  int32_t reg_value;
  enip_scanner_motoman_read_register(&robot_ip, 1, &reg_value, 5000);
  // Write reg_value to PLC Tag
  
  // Write register R002
  int32_t new_value = 100;
  enip_scanner_motoman_write_register(&robot_ip, 2, new_value, 5000);
```

### Use Case 10: Variable Access - All Types

#### Byte Variable (B) - Class 0x7A
```
PLC Tags:
  - VarBNumber (DINT) - Variable number (e.g., 1 for B001)
  - VarBValue (SINT) - Value to write / Result

Example Code:
  uint8_t value;
  enip_scanner_motoman_read_variable_b(&robot_ip, 1, &value, 5000);
  enip_scanner_motoman_write_variable_b(&robot_ip, 1, 100, 5000);
```

#### Integer Variable (I) - Class 0x7B
```
PLC Tags:
  - VarINumber (DINT) - Variable number (e.g., 1 for I001)
  - VarIValue (INT) - Value to write / Result

Example Code:
  int16_t value;
  enip_scanner_motoman_read_variable_i(&robot_ip, 1, &value, 5000);
  enip_scanner_motoman_write_variable_i(&robot_ip, 1, 1000, 5000);
```

#### Double Integer Variable (D) - Class 0x7C
```
PLC Tags:
  - VarDNumber (DINT) - Variable number (e.g., 1 for D001)
  - VarDValue (DINT) - Value to write / Result

Example Code:
  int32_t value;
  enip_scanner_motoman_read_variable_d(&robot_ip, 1, &value, 5000);
  enip_scanner_motoman_write_variable_d(&robot_ip, 1, 100000, 5000);
```

#### Real Variable (R) - Class 0x7D
```
PLC Tags:
  - VarRNumber (DINT) - Variable number (e.g., 1 for R001)
  - VarRValue (REAL) - Value to write / Result

Example Code:
  float value;
  enip_scanner_motoman_read_variable_r(&robot_ip, 1, &value, 5000);
  enip_scanner_motoman_write_variable_r(&robot_ip, 1, 3.14159, 5000);
```

#### String Variable (S) - Class 0x8C
```
PLC Tags:
  - VarSNumber (DINT) - Variable number (e.g., 1 for S001)
  - VarSValue (STRING) - Value to write / Result (max 32 chars)

Example Code:
  char value[33];
  enip_scanner_motoman_read_variable_s(&robot_ip, 1, value, sizeof(value), 5000);
  enip_scanner_motoman_write_variable_s(&robot_ip, 1, "Hello Robot", 5000);
```

#### Base Position Variable (BP) - Class 0x80
```
PLC Tags:
  - VarBPNumber (DINT) - Variable number (e.g., 1 for BP001)
  - VarBPX, VarBPY, VarBPZ (REAL) - Position values
  - VarBPRX, VarBPRY, VarBPRZ (REAL) - Rotation values

Example Code:
  enip_scanner_motoman_base_position_t bp;
  enip_scanner_motoman_read_variable_bp(&robot_ip, 1, &bp, 5000);
  // Access bp.x, bp.y, bp.z, bp.rx, bp.ry, bp.rz
```

#### External Axis Variable (EX) - Class 0x81
```
PLC Tags:
  - VarEXNumber (DINT) - Variable number (e.g., 1 for EX001)
  - VarEXAxis1-8 (REAL) - External axis positions

Example Code:
  enip_scanner_motoman_external_position_t ex;
  enip_scanner_motoman_read_variable_ex(&robot_ip, 1, &ex, 5000);
  // Access ex.axis[0] through ex.axis[7] for 8 external axes
```

### Use Case 11: Axis Configuration (CIP Class 0x74)
```
PLC Tags:
  - ReadAxisConfig (BOOL) - Trigger
  - ControlGroup (DINT) - Control group (0 = Robot, 1+ = External)
  - AxisName1-8 (STRING) - Axis coordinate names

Motoman CIP Class 0x74:
  - Read axis configuration for control group
  - Returns: 8 axis coordinate names (8 chars each)
  
Example Code:
  enip_scanner_motoman_axis_config_t config;
  enip_scanner_motoman_read_axis_config(&robot_ip, 0, &config, 5000);
  // Access config.axis_names[0] through config.axis_names[7]
  // Write to PLC Tags for HMI display
```

### Use Case 12: Position Deviation (CIP Class 0x76)
```
PLC Tags:
  - ReadPositionDeviation (BOOL) - Trigger
  - ControlGroup (DINT) - Control group
  - AxisDeviation1-8 (REAL) - Deviation of each axis

Motoman CIP Class 0x76:
  - Read position deviation for each axis
  - Returns: 8 axis deviations (REAL values)
  
Example Code:
  enip_scanner_motoman_position_deviation_t deviation;
  enip_scanner_motoman_read_position_deviation(&robot_ip, 0, &deviation, 5000);
  // Access deviation.axis_deviation[0] through deviation.axis_deviation[7]
  // Write to PLC Tags for monitoring
```

### Use Case 13: Torque Monitoring (CIP Class 0x77)
```
PLC Tags:
  - ReadTorque (BOOL) - Trigger
  - ControlGroup (DINT) - Control group
  - AxisTorque1-8 (REAL) - Torque of each axis (percentage)

Motoman CIP Class 0x77:
  - Read torque of each axis
  - Returns: 8 axis torque values (percentage when nominal value is 100%)
  
Example Code:
  enip_scanner_motoman_torque_t torque;
  enip_scanner_motoman_read_torque(&robot_ip, 0, &torque, 5000);
  // Access torque.axis_torque[0] through torque.axis_torque[7]
  // Write to PLC Tags for monitoring/overload detection
```

### Use Case 14: Comprehensive Translator (All Features Combined)
```
Complete bidirectional translator using multiple CIP classes:

PLC → Robot:
  - Read job number from PLC Tag → Write to I/O signal (Class 0x78)
  - Read start command from PLC Tag → Write to I/O signal (Class 0x78)
  - Read position setpoint from PLC Tags → Write to position variable (Class 0x7F)

Robot → PLC:
  - Read robot status (Class 0x72) → Write status bits to PLC Tags
  - Read active job info (Class 0x73) → Write job name, line, step to PLC Tags
  - Read current position (Class 0x75) → Write X, Y, Z to PLC Tags
  - Read alarm on error (Class 0x70) → Write alarm code/string to PLC Tags
  - Read I/O signals (Class 0x78) → Write to PLC Tags
  - Read position deviation (Class 0x76) → Write to PLC Tags for monitoring
  - Read torque (Class 0x77) → Write to PLC Tags for overload detection

Example Implementation:
  See examples/micro800_motoman_translator.c for working code
```

## Translation Patterns and Best Practices

### Pattern 1: Edge-Triggered Commands
**Problem**: Continuous polling can cause repeated triggering of robot commands.

**Solution**: Detect edge transitions (false→true) before sending commands.

```c
static bool last_start_cmd = false;
bool current_start_cmd;
enip_scanner_read_tag(&plc_ip, "RobotStartCmd", CIP_DATA_TYPE_BOOL, 
                      &current_start_cmd, sizeof(current_start_cmd), 5000);
if (current_start_cmd && !last_start_cmd) {
    // Edge detected: send command once
    enip_scanner_motoman_write_io(&robot_ip, 1001, 1, 5000);
}
last_start_cmd = current_start_cmd;
```

### Pattern 2: Status Polling with Error Handling
**Problem**: Network errors or robot unavailability should be handled gracefully.

**Solution**: Implement retry logic and error counting.

```c
int consecutive_errors = 0;
const int MAX_ERRORS = 5;

enip_scanner_motoman_status_t status;
esp_err_t ret = enip_scanner_motoman_read_status(&robot_ip, &status, 5000);
if (ret != ESP_OK || !status.success) {
    consecutive_errors++;
    if (consecutive_errors >= MAX_ERRORS) {
        // Report error to PLC
        bool error_flag = true;
        enip_scanner_write_tag(&plc_ip, "TranslatorError", CIP_DATA_TYPE_BOOL,
                              &error_flag, sizeof(error_flag), 5000);
    }
} else {
    consecutive_errors = 0;
    // Process status and write to PLC
}
```

### Pattern 3: Conditional Alarm Reading
**Problem**: Reading alarms continuously is inefficient.

**Solution**: Only read alarms when error condition detected.

```c
enip_scanner_motoman_status_t status;
enip_scanner_motoman_read_status(&robot_ip, &status, 5000);
bool has_error = (status.data2 & 0x20) != 0;  // Error bit

if (has_error) {
    enip_scanner_motoman_alarm_t alarm;
    enip_scanner_motoman_read_alarm(&robot_ip, 0, &alarm, 5000);
    // Write alarm code and string to PLC Tags
    enip_scanner_write_tag(&plc_ip, "AlarmCode", CIP_DATA_TYPE_DINT,
                          &alarm.alarm_code, sizeof(alarm.alarm_code), 5000);
    enip_scanner_write_tag(&plc_ip, "AlarmString", CIP_DATA_TYPE_STRING,
                          alarm.alarm_string, strlen(alarm.alarm_string), 5000);
}
```

### Pattern 4: Variable Instance Calculation
**Problem**: Motoman variables use instance numbers (1 = P001, 2 = P002, etc.).

**Solution**: Convert PLC tag value to instance number.

```c
// PLC Tag "PosVarNumber" contains 1 for P001, 2 for P002, etc.
int32_t var_number;
enip_scanner_read_tag(&plc_ip, "PosVarNumber", CIP_DATA_TYPE_DINT,
                      &var_number, sizeof(var_number), 5000);

// Use var_number directly as instance (1 = P001)
enip_scanner_motoman_position_t pos;
enip_scanner_motoman_read_variable_p(&robot_ip, (uint16_t)var_number, &pos, 5000);
```

### Pattern 5: I/O Signal Number Mapping
**Problem**: Different signal types use different number ranges.

**Solution**: Map PLC tag values to correct signal ranges.

```c
// Signal type mapping:
// 0 = General Input (1-999)
// 1 = General Output (1001-1999)
// 2 = Network Input (2001-2999)
// 3 = Network Output (3001-3999)

int32_t signal_type, signal_index;
enip_scanner_read_tag(&plc_ip, "SignalType", CIP_DATA_TYPE_DINT,
                      &signal_type, sizeof(signal_type), 5000);
enip_scanner_read_tag(&plc_ip, "SignalIndex", CIP_DATA_TYPE_DINT,
                      &signal_index, sizeof(signal_index), 5000);

uint16_t signal_number = signal_index;
if (signal_type == 1) signal_number += 1000;      // General Output
else if (signal_type == 2) signal_number += 2000;  // Network Input
else if (signal_type == 3) signal_number += 3000;   // Network Output

uint16_t value;
enip_scanner_motoman_read_io(&robot_ip, signal_number, &value, 5000);
```

### Pattern 6: Position Data Translation
**Problem**: Position structures contain multiple REAL values.

**Solution**: Read/write individual components to/from PLC Tags.

```c
// Read position from robot
enip_scanner_motoman_position_t position;
enip_scanner_motoman_read_position(&robot_ip, 0, &position, 5000);

// Write individual components to PLC Tags
enip_scanner_write_tag(&plc_ip, "RobotX", CIP_DATA_TYPE_REAL,
                      &position.x, sizeof(position.x), 5000);
enip_scanner_write_tag(&plc_ip, "RobotY", CIP_DATA_TYPE_REAL,
                      &position.y, sizeof(position.y), 5000);
enip_scanner_write_tag(&plc_ip, "RobotZ", CIP_DATA_TYPE_REAL,
                      &position.z, sizeof(position.z), 5000);
// ... repeat for RX, RY, RZ, config
```

### Pattern 7: String Variable Handling
**Problem**: String variables have fixed maximum length (32 chars).

**Solution**: Truncate or pad strings as needed.

```c
char plc_string[33];
enip_scanner_read_tag(&plc_ip, "VarSValue", CIP_DATA_TYPE_STRING,
                      plc_string, sizeof(plc_string), 5000);

// Ensure null-termination and max length
if (strlen(plc_string) > 32) {
    plc_string[32] = '\0';
}

enip_scanner_motoman_write_variable_s(&robot_ip, 1, plc_string, 5000);
```

### Pattern 8: Polling Frequency Optimization
**Problem**: Different data types have different update frequencies.

**Solution**: Use different polling intervals for different data.

```c
// Fast polling (100ms): Status, I/O signals
// Medium polling (500ms): Position, job info
// Slow polling (2000ms): Alarm history, axis config, torque

static uint32_t last_fast_poll = 0;
static uint32_t last_medium_poll = 0;
static uint32_t last_slow_poll = 0;
uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

if (now - last_fast_poll >= 100) {
    // Read status and I/O
    enip_scanner_motoman_read_status(&robot_ip, &status, 5000);
    last_fast_poll = now;
}

if (now - last_medium_poll >= 500) {
    // Read position and job info
    enip_scanner_motoman_read_position(&robot_ip, 0, &position, 5000);
    last_medium_poll = now;
}

if (now - last_slow_poll >= 2000) {
    // Read torque and axis config
    enip_scanner_motoman_read_torque(&robot_ip, 0, &torque, 5000);
    last_slow_poll = now;
}
```

## Next Steps

1. **Define Requirements**
   - What data needs to be translated?
   - What direction(s)? (PLC→Robot, Robot→PLC, or Bidirectional)
   - What communication method(s)? (Assembly I/O, CIP Messages, or Hybrid)
   - What polling frequencies? (Fast status, slow configuration)

2. **Design Data Structures**
   - Assembly layouts (if using Assembly I/O)
   - Tag mappings (PLC tag names ↔ Robot signals/variables)
   - Translation rules (data type conversions, scaling)
   - Error handling strategy (retry logic, error reporting)

3. **Choose Implementation Approach**
   - ✅ **Approach B is now available** - All Motoman CIP classes implemented
   - Start with Approach A for simple I/O-only applications
   - Use Approach B for full robot control (status, alarms, variables)
   - See `examples/micro800_motoman_translator.c` for Approach B example

4. **Select CIP Classes**
   - **Status Monitoring**: Class 0x72 (status), Class 0x73 (job info)
   - **Alarm Handling**: Class 0x70 (current alarm), Class 0x71 (history)
   - **Position Data**: Class 0x75 (current position), Class 0x76 (deviation)
   - **I/O Control**: Class 0x78 (I/O signals)
   - **Variable Access**: Classes 0x7A-0x81 (all variable types)
   - **Registers**: Class 0x79 (registers)
   - **Configuration**: Class 0x74 (axis config), Class 0x77 (torque)

5. **Implement Translation Logic**
   - Create translator task/function
   - Implement polling loops with appropriate frequencies
   - Add edge detection for commands
   - Implement error handling and retry logic
   - Add data validation and range checking

6. **Test Thoroughly**
   - Test each CIP class individually
   - Test bidirectional translation
   - Test error conditions (network failures, robot errors)
   - Test edge cases (boundary values, invalid data)
   - Verify PLC tag mappings match robot configuration

## Complete Code Example

The following example demonstrates a comprehensive translator using multiple Motoman CIP classes:

```c
#include "enip_scanner.h"
#include "lwip/inet.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "translator";

// Configuration
#define PLC_IP_ADDRESS "192.168.1.100"
#define MOTOMAN_IP_ADDRESS "192.168.1.200"
#define POLL_INTERVAL_MS 100

// State tracking
static bool last_start_cmd = false;
static bool last_reset_cmd = false;
static int consecutive_errors = 0;

void translator_task(void *pvParameters)
{
    ip4_addr_t plc_ip, robot_ip;
    inet_aton(PLC_IP_ADDRESS, &plc_ip);
    inet_aton(MOTOMAN_IP_ADDRESS, &robot_ip);
    
    while (1) {
        // 1. Read commands from PLC
        bool start_cmd, stop_cmd, reset_cmd;
        int32_t job_number;
        
        enip_scanner_read_tag(&plc_ip, "RobotStartCmd", CIP_DATA_TYPE_BOOL,
                             &start_cmd, sizeof(start_cmd), 5000);
        enip_scanner_read_tag(&plc_ip, "RobotStopCmd", CIP_DATA_TYPE_BOOL,
                             &stop_cmd, sizeof(stop_cmd), 5000);
        enip_scanner_read_tag(&plc_ip, "RobotResetCmd", CIP_DATA_TYPE_BOOL,
                             &reset_cmd, sizeof(reset_cmd), 5000);
        enip_scanner_read_tag(&plc_ip, "RobotJobNumber", CIP_DATA_TYPE_DINT,
                             &job_number, sizeof(job_number), 5000);
        
        // 2. Edge-triggered command handling
        if (start_cmd && !last_start_cmd) {
            // Write job number first
            uint16_t job_io = (uint16_t)job_number;
            enip_scanner_motoman_write_io(&robot_ip, 1004, job_io, 5000);
            // Then send start command
            enip_scanner_motoman_write_io(&robot_ip, 1001, 1, 5000);
            ESP_LOGI(TAG, "Start command sent, job: %ld", job_number);
        }
        last_start_cmd = start_cmd;
        
        if (stop_cmd) {
            enip_scanner_motoman_write_io(&robot_ip, 1002, 1, 5000);
        }
        
        if (reset_cmd && !last_reset_cmd) {
            enip_scanner_motoman_write_io(&robot_ip, 1003, 1, 5000);
            ESP_LOGI(TAG, "Reset command sent");
        }
        last_reset_cmd = reset_cmd;
        
        // 3. Read robot status (Class 0x72)
        enip_scanner_motoman_status_t status;
        esp_err_t ret = enip_scanner_motoman_read_status(&robot_ip, &status, 5000);
        
        if (ret != ESP_OK || !status.success) {
            consecutive_errors++;
            if (consecutive_errors >= 5) {
                bool error_flag = true;
                enip_scanner_write_tag(&plc_ip, "TranslatorError", CIP_DATA_TYPE_BOOL,
                                      &error_flag, sizeof(error_flag), 5000);
            }
            vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
            continue;
        }
        
        consecutive_errors = 0;
        
        // Parse status bits
        bool running = (status.data1 & 0x08) != 0;  // Bit 3
        bool error = (status.data2 & 0x20) != 0;     // Bit 5
        bool hold = (status.data2 & 0x10) != 0;      // Bit 4
        bool alarm = (status.data2 & 0x80) != 0;    // Bit 7
        bool servo_on = (status.data2 & 0x40) != 0; // Bit 6
        
        // Write status to PLC
        enip_scanner_write_tag(&plc_ip, "RobotRunning", CIP_DATA_TYPE_BOOL,
                              &running, sizeof(running), 5000);
        enip_scanner_write_tag(&plc_ip, "RobotError", CIP_DATA_TYPE_BOOL,
                              &error, sizeof(error), 5000);
        enip_scanner_write_tag(&plc_ip, "RobotHold", CIP_DATA_TYPE_BOOL,
                              &hold, sizeof(hold), 5000);
        enip_scanner_write_tag(&plc_ip, "RobotAlarm", CIP_DATA_TYPE_BOOL,
                              &alarm, sizeof(alarm), 5000);
        enip_scanner_write_tag(&plc_ip, "ServoOn", CIP_DATA_TYPE_BOOL,
                              &servo_on, sizeof(servo_on), 5000);
        
        // 4. Read job information (Class 0x73) - every 500ms
        static uint32_t last_job_read = 0;
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_job_read >= 500) {
            enip_scanner_motoman_job_info_t job_info;
            if (enip_scanner_motoman_read_job_info(&robot_ip, &job_info, 5000) == ESP_OK) {
                enip_scanner_write_tag(&plc_ip, "JobName", CIP_DATA_TYPE_STRING,
                                      job_info.job_name, strlen(job_info.job_name), 5000);
                enip_scanner_write_tag(&plc_ip, "JobLineNumber", CIP_DATA_TYPE_DINT,
                                      &job_info.line_number, sizeof(job_info.line_number), 5000);
                enip_scanner_write_tag(&plc_ip, "JobStep", CIP_DATA_TYPE_DINT,
                                      &job_info.step, sizeof(job_info.step), 5000);
                float speed_override = job_info.speed_override / 100.0f;  // Convert to 0-1.0
                enip_scanner_write_tag(&plc_ip, "SpeedOverride", CIP_DATA_TYPE_REAL,
                                      &speed_override, sizeof(speed_override), 5000);
            }
            last_job_read = now;
        }
        
        // 5. Read alarm on error (Class 0x70)
        if (error || alarm) {
            enip_scanner_motoman_alarm_t alarm_info;
            if (enip_scanner_motoman_read_alarm(&robot_ip, 0, &alarm_info, 5000) == ESP_OK) {
                enip_scanner_write_tag(&plc_ip, "AlarmCode", CIP_DATA_TYPE_DINT,
                                      &alarm_info.alarm_code, sizeof(alarm_info.alarm_code), 5000);
                enip_scanner_write_tag(&plc_ip, "AlarmString", CIP_DATA_TYPE_STRING,
                                      alarm_info.alarm_string, strlen(alarm_info.alarm_string), 5000);
            }
        }
        
        // 6. Read I/O signals (Class 0x78)
        uint16_t running_io, job_complete_io;
        if (enip_scanner_motoman_read_io(&robot_ip, 1, &running_io, 5000) == ESP_OK) {
            bool running_status = (running_io != 0);
            enip_scanner_write_tag(&plc_ip, "RobotRunningIO", CIP_DATA_TYPE_BOOL,
                                  &running_status, sizeof(running_status), 5000);
        }
        if (enip_scanner_motoman_read_io(&robot_ip, 2, &job_complete_io, 5000) == ESP_OK) {
            bool job_complete = (job_complete_io != 0);
            enip_scanner_write_tag(&plc_ip, "JobComplete", CIP_DATA_TYPE_BOOL,
                                  &job_complete, sizeof(job_complete), 5000);
        }
        
        // 7. Read position (Class 0x75) - every 500ms
        static uint32_t last_pos_read = 0;
        if (now - last_pos_read >= 500) {
            enip_scanner_motoman_position_t position;
            if (enip_scanner_motoman_read_position(&robot_ip, 0, &position, 5000) == ESP_OK) {
                enip_scanner_write_tag(&plc_ip, "RobotX", CIP_DATA_TYPE_REAL,
                                      &position.x, sizeof(position.x), 5000);
                enip_scanner_write_tag(&plc_ip, "RobotY", CIP_DATA_TYPE_REAL,
                                      &position.y, sizeof(position.y), 5000);
                enip_scanner_write_tag(&plc_ip, "RobotZ", CIP_DATA_TYPE_REAL,
                                      &position.z, sizeof(position.z), 5000);
            }
            last_pos_read = now;
        }
        
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void app_main(void)
{
    // Initialize network...
    // Initialize scanner
    enip_scanner_init();
    
    // Create translator task
    xTaskCreate(translator_task, "translator", 8192, NULL, 5, NULL);
}
```

This example demonstrates:
- ✅ Edge-triggered commands (start, reset)
- ✅ Status monitoring (Class 0x72)
- ✅ Job information reading (Class 0x73)
- ✅ Alarm reading on error (Class 0x70)
- ✅ I/O signal read/write (Class 0x78)
- ✅ Position reading (Class 0x75)
- ✅ Error handling and retry logic
- ✅ Different polling frequencies for different data
- ✅ Bidirectional translation (PLC ↔ Robot)

## Related Documentation

- **Component API**: [API_DOCUMENTATION.md](API_DOCUMENTATION.md) - Complete API reference with all function signatures
- **Motoman CIP Classes**: [MOTOMAN_CIP_CLASSES.md](MOTOMAN_CIP_CLASSES.md) - Detailed documentation for all 18 CIP classes
- **Motoman Manual**: Motoman/165838-1CD.md - Official Motoman EtherNet/IP communication manual
- **Component README**: [README.md](README.md) - Component overview and quick start guide
- **Examples**: [examples/README.md](../examples/README.md) - Example code documentation

## Questions to Answer

1. **What specific data needs to be translated?**
   - Control commands? Status? Positions? Variables?

2. **What direction(s) of communication?**
   - PLC → Robot only?
   - Robot → PLC only?
   - Bidirectional?

3. **What communication method?**
   - Assembly I/O only? (Simple, fast)
   - CIP Messages only? (✅ Available - All 18 classes implemented)
   - Hybrid? (Recommended - Best performance + features)

4. **What is the priority?**
   - Speed (Assembly I/O)?
   - Features (CIP Messages)?
   - Simplicity (Basic translator first)?

5. **What are the assembly instance numbers?**
   - Known/configured?
   - Need to discover?

6. **What is the data layout?**
   - Known byte/bit mappings?
   - Need to define?

Let's discuss these questions to design the best translator for your needs!


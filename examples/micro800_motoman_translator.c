/*
 * Copyright (c) 2025, Adam G. Sweeney <agsweeney@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file micro800_motoman_translator.c
 * @brief Real-World Bidirectional Translator: Micro800 PLC ↔ Motoman DX200 Robot
 * 
 * EXAMPLE SCENARIO:
 * =================
 * This example demonstrates a bidirectional translator for a pick-and-place application
 * where a Micro800 PLC controls a Motoman DX200 robot via EtherNet/IP.
 * 
 * NOTE: This is example code and has not been tested in production environments.
 * Use as a reference for implementing your own translator application.
 * 
 * Application Flow:
 * 1. PLC sets job number and start command via tags
 * 2. ESP32 reads PLC tags and writes to robot I/O signals (Class 0x78)
 * 3. ESP32 reads robot status (Class 0x72) and job info (Class 0x73)
 * 4. ESP32 writes robot status back to PLC tags for HMI display
 * 5. On errors, ESP32 reads alarm information (Class 0x70) and reports to PLC
 * 
 * Architecture:
 *   Micro800 PLC (Tags) ↔ ESP32 Translator ↔ Motoman DX200 (CIP Classes + I/O)
 * 
 * Communication Methods Used:
 *   - Motoman CIP Class 0x72: Read robot status (Running, Error, Hold, etc.)
 *   - Motoman CIP Class 0x73: Read current job information
 *   - Motoman CIP Class 0x70: Read current alarm (on error)
 *   - Motoman CIP Class 0x78: Read/Write I/O signals (General Output 1001-1256 for control)
 *   - Assembly I/O (Class 0x04): Alternative method for high-speed I/O (if configured)
 * 
 * Real-World I/O Mapping:
 *   - General Output 1001: Start Job command (PLC → Robot)
 *   - General Output 1002: Stop Job command (PLC → Robot)
 *   - General Output 1003: Reset Robot command (PLC → Robot)
 *   - General Output 1004: Job Number (0-255) (PLC → Robot)
 *   - General Input 1: Robot Running status (Robot → PLC)
 *   - General Input 2: Job Complete status (Robot → PLC)
 *   - General Input 3: Error Present (Robot → PLC)
 *   - General Input 4: Hold Active (Robot → PLC)
 * 
 * Configuration:
 *   - Assembly instances are user-configurable (not fixed)
 *   - Use enip_scanner_discover_assemblies() to find available instances
 *   - Configure assembly sizes and instances in Motoman controller
 *   - I/O signals must be configured in robot I/O mapping
 */

#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "enip_scanner.h"

#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT && CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT

static const char *TAG = "translator";

// ============================================================================
// Configuration - Modify these for your setup
// ============================================================================

// Source device (Micro800 PLC)
#define PLC_IP_ADDRESS "192.168.1.100"

// Target device (Motoman DX200 Robot Controller)
#define MOTOMAN_IP_ADDRESS "192.168.1.200"

// Motoman Assembly Instances (optional - for high-speed I/O)
// If not using assemblies, set to 0 and use CIP I/O signals instead
#define MOTOMAN_INPUT_ASSEMBLY 101   // Input assembly (PLC → Robot) - commands/control
#define MOTOMAN_OUTPUT_ASSEMBLY 102  // Output assembly (Robot → PLC) - status/feedback

// Assembly sizes (bytes) - must match robot configuration (if using assemblies)
#define MOTOMAN_INPUT_ASSEMBLY_SIZE 8   // Size of input assembly
#define MOTOMAN_OUTPUT_ASSEMBLY_SIZE 8  // Size of output assembly

// Motoman I/O Signal Numbers (General Output: 1001-1256, General Input: 1-256)
// These map to physical I/O configured in the robot controller
#define MOTOMAN_IO_START_CMD 1001      // General Output: Start job command
#define MOTOMAN_IO_STOP_CMD 1002       // General Output: Stop job command
#define MOTOMAN_IO_RESET_CMD 1003      // General Output: Reset robot command
#define MOTOMAN_IO_JOB_NUMBER 1004    // General Output: Job number (0-255)
#define MOTOMAN_IO_RUNNING 1           // General Input: Robot running status
#define MOTOMAN_IO_JOB_COMPLETE 2      // General Input: Job complete status
#define MOTOMAN_IO_ERROR 3             // General Input: Error present
#define MOTOMAN_IO_HOLD 4              // General Input: Hold active

// PLC Tags - Commands/Control (PLC → Robot)
// These tags must exist in your Micro800 PLC program
#define PLC_TAG_JOB_NUMBER "RobotJobNumber"      // DINT - Job number to execute (0-255)
#define PLC_TAG_START_COMMAND "RobotStartCmd"    // BOOL - Start job command (edge-triggered)
#define PLC_TAG_STOP_COMMAND "RobotStopCmd"      // BOOL - Stop job command
#define PLC_TAG_RESET_COMMAND "RobotResetCmd"    // BOOL - Reset robot command

// PLC Tags - Status/Feedback (Robot → PLC)
#define PLC_TAG_ROBOT_RUNNING "RobotRunning"     // BOOL - Robot is running
#define PLC_TAG_ROBOT_ERROR "RobotError"         // BOOL - Robot has error
#define PLC_TAG_JOB_COMPLETE "RobotJobComplete"  // BOOL - Job completed
#define PLC_TAG_HOLD_ACTIVE "RobotHoldActive"    // BOOL - Hold is active
#define PLC_TAG_CURRENT_JOB "RobotCurrentJob"    // DINT - Currently executing job
#define PLC_TAG_ERROR_CODE "RobotErrorCode"      // DINT - Error code (0 = no error)
#define PLC_TAG_ALARM_CODE "RobotAlarmCode"     // DINT - Alarm code (0 = no alarm)
#define PLC_TAG_SERVO_ON "RobotServoOn"         // BOOL - Servo motors enabled

// Translation configuration
#define TRANSLATION_POLL_INTERVAL_MS 100  // Polling interval (100ms = 10Hz)
#define OPERATION_TIMEOUT_MS 5000         // Timeout for each operation
#define MAX_CONSECUTIVE_ERRORS 5          // Max errors before logging warning

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Robot control command structure (PLC → Robot)
 * 
 * This structure defines the layout of data written to Motoman Input Assembly
 * (if using Assembly I/O instead of CIP I/O signals).
 */
typedef struct __attribute__((packed)) {
    uint8_t job_number;      // Job number (0-255) - Byte 0
    uint8_t control_bits;    // Control command bits - Byte 1
    // Bit 0: Start Job command
    // Bit 1: Stop Job command
    // Bit 2: Reset Robot command
    // Bits 3-7: Reserved
    uint16_t reserved;       // Reserved - Bytes 2-3
    uint32_t reserved2;      // Reserved - Bytes 4-7
} robot_control_t;

/**
 * @brief Robot status structure (Robot → PLC)
 * 
 * This structure defines the layout of data read from Motoman Output Assembly
 * (if using Assembly I/O instead of CIP status reads).
 */
typedef struct __attribute__((packed)) {
    uint8_t status_bits;     // Status bits - Byte 0
    // Bit 0: Job Running
    // Bit 1: Job Complete
    // Bit 2: Error Present
    // Bit 3: Hold Active
    // Bits 4-7: Reserved
    uint8_t current_job;      // Currently executing job - Byte 1
    uint16_t error_code;     // Error code - Bytes 2-3
    uint32_t reserved;       // Reserved - Bytes 4-7
} robot_status_t;

// Compile-time validation
_Static_assert(sizeof(robot_control_t) == MOTOMAN_INPUT_ASSEMBLY_SIZE, 
               "robot_control_t size must match MOTOMAN_INPUT_ASSEMBLY_SIZE");
_Static_assert(sizeof(robot_status_t) == MOTOMAN_OUTPUT_ASSEMBLY_SIZE, 
               "robot_status_t size must match MOTOMAN_OUTPUT_ASSEMBLY_SIZE");

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Read a BOOL tag from PLC
 */
static esp_err_t read_bool_tag(const ip4_addr_t *ip, const char *tag_path, bool *value) {
    enip_scanner_tag_result_t result;
    memset(&result, 0, sizeof(result));
    esp_err_t ret = enip_scanner_read_tag(ip, tag_path, &result, OPERATION_TIMEOUT_MS);
    if (ret == ESP_OK && result.success && result.cip_data_type == CIP_DATA_TYPE_BOOL && result.data_length == 1) {
        *value = (result.data[0] != 0);
    } else {
        ret = ESP_FAIL;
    }
    enip_scanner_free_tag_result(&result);
    return ret;
}

/**
 * @brief Read a DINT tag from PLC
 */
static esp_err_t read_dint_tag(const ip4_addr_t *ip, const char *tag_path, int32_t *value) {
    enip_scanner_tag_result_t result;
    memset(&result, 0, sizeof(result));
    esp_err_t ret = enip_scanner_read_tag(ip, tag_path, &result, OPERATION_TIMEOUT_MS);
    if (ret == ESP_OK && result.success && result.cip_data_type == CIP_DATA_TYPE_DINT && result.data_length == 4) {
        *value = (int32_t)(result.data[0] | (result.data[1] << 8) | (result.data[2] << 16) | (result.data[3] << 24));
    } else {
        ret = ESP_FAIL;
    }
    enip_scanner_free_tag_result(&result);
    return ret;
}

/**
 * @brief Write a BOOL tag to PLC
 */
static esp_err_t write_bool_tag(const ip4_addr_t *ip, const char *tag_path, bool value) {
    uint8_t data = value ? 1 : 0;
    char error_msg[128];
    esp_err_t ret = enip_scanner_write_tag(ip, tag_path, &data, 1, CIP_DATA_TYPE_BOOL, OPERATION_TIMEOUT_MS, error_msg);
    return ret;
}

/**
 * @brief Write a DINT tag to PLC
 */
static esp_err_t write_dint_tag(const ip4_addr_t *ip, const char *tag_path, int32_t value) {
    uint8_t data[4];
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    data[2] = (value >> 16) & 0xFF;
    data[3] = (value >> 24) & 0xFF;
    char error_msg[128];
    esp_err_t ret = enip_scanner_write_tag(ip, tag_path, data, 4, CIP_DATA_TYPE_DINT, OPERATION_TIMEOUT_MS, error_msg);
    return ret;
}

/**
 * @brief Write robot status to PLC tags using Motoman CIP status read
 */
static void write_robot_status_to_plc(const ip4_addr_t *plc_ip, const ip4_addr_t *motoman_ip) {
    // Read robot status using Motoman CIP Class 0x72
    enip_scanner_motoman_status_t status;
    esp_err_t ret = enip_scanner_motoman_read_status(motoman_ip, &status, OPERATION_TIMEOUT_MS);
    
    if (ret == ESP_OK && status.success) {
        // Parse Data 1 status bits (from Motoman manual)
        // Bit 3: Running, Bit 6: Play
        bool running = (status.data1 & 0x08) != 0 || (status.data1 & 0x40) != 0;
        write_bool_tag(plc_ip, PLC_TAG_ROBOT_RUNNING, running);
        
        // Parse Data 2 status bits
        // Bit 1: Hold (Programming pendant)
        // Bit 2: Hold (external)
        // Bit 3: Hold (Command)
        bool hold_active = (status.data2 & 0x0E) != 0;
        write_bool_tag(plc_ip, PLC_TAG_HOLD_ACTIVE, hold_active);
        
        // Bit 4: Alarm
        bool alarm = (status.data2 & 0x10) != 0;
        if (alarm) {
            // Read alarm code using Class 0x70
            // Note: This is a simplified example - actual alarm reading requires parsing Class 0x70 response
            write_dint_tag(plc_ip, PLC_TAG_ALARM_CODE, 1); // Placeholder
        } else {
            write_dint_tag(plc_ip, PLC_TAG_ALARM_CODE, 0);
        }
        
        // Bit 5: Error
        bool error = (status.data2 & 0x20) != 0;
        write_bool_tag(plc_ip, PLC_TAG_ROBOT_ERROR, error);
        if (error) {
            // Read error code - would need to read from robot error register or variable
            write_dint_tag(plc_ip, PLC_TAG_ERROR_CODE, 1); // Placeholder
        } else {
            write_dint_tag(plc_ip, PLC_TAG_ERROR_CODE, 0);
        }
        
        // Bit 6: Servo on
        bool servo_on = (status.data2 & 0x40) != 0;
        write_bool_tag(plc_ip, PLC_TAG_SERVO_ON, servo_on);
        
        // Read current job using I/O signal or variable
        // For this example, we'll use a placeholder - in real implementation,
        // you would read from robot variable or I/O signal
        uint8_t current_job_io = 0;
        char error_msg[128];
        if (enip_scanner_motoman_read_io(motoman_ip, MOTOMAN_IO_JOB_NUMBER, &current_job_io, OPERATION_TIMEOUT_MS, error_msg) == ESP_OK) {
            write_dint_tag(plc_ip, PLC_TAG_CURRENT_JOB, current_job_io);
        }
        
        // Job complete status - typically read from I/O signal
        uint8_t job_complete_io = 0;
        if (enip_scanner_motoman_read_io(motoman_ip, MOTOMAN_IO_JOB_COMPLETE, &job_complete_io, OPERATION_TIMEOUT_MS, error_msg) == ESP_OK) {
            write_bool_tag(plc_ip, PLC_TAG_JOB_COMPLETE, job_complete_io != 0);
        }
    } else {
        ESP_LOGW(TAG, "Failed to read robot status: %s", status.error_message);
    }
}

// ============================================================================
// Main Translator Task
// ============================================================================

/**
 * @brief Main translator task - bidirectional translation loop
 * 
 * Real-world operation:
 * 1. Read PLC command tags (job number, start/stop/reset)
 * 2. Write commands to robot I/O signals (Class 0x78)
 * 3. Read robot status (Class 0x72) and I/O signals
 * 4. Write status back to PLC tags
 * 5. Handle errors and retries
 */
static void translator_task(void *pvParameters) {
    ip4_addr_t plc_ip, motoman_ip;
    inet_aton(PLC_IP_ADDRESS, &plc_ip);
    inet_aton(MOTOMAN_IP_ADDRESS, &motoman_ip);
    
    // Statistics
    uint32_t cycle_count = 0;
    uint32_t read_success_count = 0;
    uint32_t read_fail_count = 0;
    uint32_t write_success_count = 0;
    uint32_t write_fail_count = 0;
    uint32_t consecutive_errors = 0;
    
    // Edge detection for start command (prevents continuous triggering)
    static bool last_start_cmd = false;
    bool current_start_cmd = false;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Motoman Translator Started");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "PLC IP: %s", PLC_IP_ADDRESS);
    ESP_LOGI(TAG, "Motoman IP: %s", MOTOMAN_IP_ADDRESS);
    ESP_LOGI(TAG, "Polling interval: %d ms", TRANSLATION_POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "Using Motoman CIP I/O signals for control");
    
    // Optional: Discover assemblies on startup
    if (MOTOMAN_INPUT_ASSEMBLY > 0) {
        ESP_LOGI(TAG, "Discovering assemblies on Motoman robot...");
        uint16_t discovered_instances[32];
        int instance_count = enip_scanner_discover_assemblies(&motoman_ip, discovered_instances, 32, 2000);
        if (instance_count > 0) {
            ESP_LOGI(TAG, "Found %d assembly instance(s):", instance_count);
            for (int i = 0; i < instance_count; i++) {
                bool writable = enip_scanner_is_assembly_writable(&motoman_ip, discovered_instances[i], 2000);
                ESP_LOGI(TAG, "  Instance %d: %s", discovered_instances[i], writable ? "Writable" : "Read-only");
            }
        } else {
            ESP_LOGW(TAG, "No assemblies discovered - using CIP I/O signals only");
        }
    }
    
    // Verify robot is reachable by reading status
    ESP_LOGI(TAG, "Verifying robot connection...");
    enip_scanner_motoman_status_t test_status;
    if (enip_scanner_motoman_read_status(&motoman_ip, &test_status, 3000) == ESP_OK) {
        ESP_LOGI(TAG, "Robot connection verified - Status Data1: 0x%08lX, Data2: 0x%08lX",
                 (unsigned long)test_status.data1, (unsigned long)test_status.data2);
    } else {
        ESP_LOGE(TAG, "Failed to connect to robot - check IP address and network");
    }
    
    ESP_LOGI(TAG, "Starting translation loop...");
    
    while (1) {
        cycle_count++;
        bool cycle_success = true;
        
        // ====================================================================
        // Step 1: Read control commands from PLC tags (PLC → Robot)
        // ====================================================================
        
        int32_t job_number_dint = 0;
        bool start_cmd = false, stop_cmd = false, reset_cmd = false;
        
        // Read job number
        if (read_dint_tag(&plc_ip, PLC_TAG_JOB_NUMBER, &job_number_dint)) {
            // Clamp to valid range (0-255) for single byte
            if (job_number_dint < 0) job_number_dint = 0;
            else if (job_number_dint > 255) job_number_dint = 255;
        } else {
            cycle_success = false;
        }
        
        // Read command flags
        if (!read_bool_tag(&plc_ip, PLC_TAG_START_COMMAND, &start_cmd)) {
            cycle_success = false;
        }
        if (!read_bool_tag(&plc_ip, PLC_TAG_STOP_COMMAND, &stop_cmd)) {
            cycle_success = false;
        }
        if (!read_bool_tag(&plc_ip, PLC_TAG_RESET_COMMAND, &reset_cmd)) {
            cycle_success = false;
        }
        
        // ====================================================================
        // Step 2: Write control data to Motoman I/O signals (PLC → Robot)
        // ====================================================================
        
        if (cycle_success) {
            char error_msg[128];
            esp_err_t ret;
            
            // Write job number to I/O signal
            uint8_t job_number_byte = (uint8_t)job_number_dint;
            ret = enip_scanner_motoman_write_io(&motoman_ip, MOTOMAN_IO_JOB_NUMBER, job_number_byte, OPERATION_TIMEOUT_MS, error_msg);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to write job number: %s", error_msg);
                write_fail_count++;
            }
            
            // Edge detection for start command (only trigger on rising edge)
            current_start_cmd = start_cmd;
            if (current_start_cmd && !last_start_cmd) {
                // Rising edge - send start command
                ret = enip_scanner_motoman_write_io(&motoman_ip, MOTOMAN_IO_START_CMD, 1, OPERATION_TIMEOUT_MS, error_msg);
                if (ret == ESP_OK) {
                    write_success_count++;
                    ESP_LOGI(TAG, "Cycle %lu: Start command sent (Job=%d)", cycle_count, job_number_byte);
                    vTaskDelay(pdMS_TO_TICKS(50)); // Brief delay
                    // Clear start command
                    enip_scanner_motoman_write_io(&motoman_ip, MOTOMAN_IO_START_CMD, 0, OPERATION_TIMEOUT_MS, error_msg);
                } else {
                    write_fail_count++;
                    ESP_LOGE(TAG, "Failed to write start command: %s", error_msg);
                }
            } else if (!current_start_cmd) {
                // Ensure start command is cleared
                enip_scanner_motoman_write_io(&motoman_ip, MOTOMAN_IO_START_CMD, 0, OPERATION_TIMEOUT_MS, error_msg);
            }
            last_start_cmd = current_start_cmd;
            
            // Write stop command
            if (stop_cmd) {
                ret = enip_scanner_motoman_write_io(&motoman_ip, MOTOMAN_IO_STOP_CMD, 1, OPERATION_TIMEOUT_MS, error_msg);
                if (ret == ESP_OK) {
                    write_success_count++;
                    ESP_LOGI(TAG, "Cycle %lu: Stop command sent", cycle_count);
                } else {
                    write_fail_count++;
                    ESP_LOGE(TAG, "Failed to write stop command: %s", error_msg);
                }
            } else {
                enip_scanner_motoman_write_io(&motoman_ip, MOTOMAN_IO_STOP_CMD, 0, OPERATION_TIMEOUT_MS, error_msg);
            }
            
            // Write reset command (edge-triggered)
            static bool last_reset_cmd = false;
            if (reset_cmd && !last_reset_cmd) {
                ret = enip_scanner_motoman_write_io(&motoman_ip, MOTOMAN_IO_RESET_CMD, 1, OPERATION_TIMEOUT_MS, error_msg);
                if (ret == ESP_OK) {
                    write_success_count++;
                    ESP_LOGI(TAG, "Cycle %lu: Reset command sent", cycle_count);
                    vTaskDelay(pdMS_TO_TICKS(50));
                    enip_scanner_motoman_write_io(&motoman_ip, MOTOMAN_IO_RESET_CMD, 0, OPERATION_TIMEOUT_MS, error_msg);
                } else {
                    write_fail_count++;
                    ESP_LOGE(TAG, "Failed to write reset command: %s", error_msg);
                }
            }
            last_reset_cmd = reset_cmd;
            
        } else {
            read_fail_count++;
            consecutive_errors++;
            if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
                ESP_LOGW(TAG, "Cycle %lu: %lu consecutive PLC read errors", cycle_count, consecutive_errors);
                consecutive_errors = 0;
            }
        }
        
        // ====================================================================
        // Step 3: Read status from Motoman (Robot → PLC)
        // ====================================================================
        
        write_robot_status_to_plc(&plc_ip, &motoman_ip);
        read_success_count++;
        
        // Log statistics periodically
        if (cycle_count % 100 == 0) {
            ESP_LOGI(TAG, "Statistics (Cycle %lu): R_OK:%lu, R_FAIL:%lu, W_OK:%lu, W_FAIL:%lu",
                     cycle_count, read_success_count, read_fail_count, write_success_count, write_fail_count);
        }
        
        vTaskDelay(pdMS_TO_TICKS(TRANSLATION_POLL_INTERVAL_MS));
    }
}

/**
 * @brief Initialize and start the translator
 * 
 * @note Make sure enip_scanner_init() has been called first
 */
void translator_init(void) {
    // Create translator task
    xTaskCreate(translator_task,
                "motoman_translator",
                8192,  // Stack size
                NULL,
                5,     // Priority
                NULL);
    
    ESP_LOGI(TAG, "Translator initialized and task created");
}

#endif // CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT && CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT

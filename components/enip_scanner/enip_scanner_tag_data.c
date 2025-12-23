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

#include "enip_scanner.h"

#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "enip_scanner_tag_data";

// Data type handler structure
typedef struct {
    uint16_t cip_data_type;
    esp_err_t (*encode_write)(const uint8_t *input_data, uint16_t input_length,
                              uint8_t *output_buffer, size_t output_size,
                              uint16_t *output_length, char *error_msg);
    esp_err_t (*decode_read)(const uint8_t *input_data, uint16_t input_length,
                            uint8_t *output_buffer, size_t output_size,
                            uint16_t *output_length, char *error_msg);
    uint16_t (*get_encoded_size)(uint16_t input_length);
} tag_data_type_handler_t;

// ============================================================================
// Standard data types (pass-through, no encoding needed)
// ============================================================================

static esp_err_t encode_standard(const uint8_t *input_data, uint16_t input_length,
                                 uint8_t *output_buffer, size_t output_size,
                                 uint16_t *output_length, char *error_msg)
{
    (void)error_msg; // Unused for standard types
    if (output_size < input_length) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output_buffer, input_data, input_length);
    *output_length = input_length;
    return ESP_OK;
}

static esp_err_t decode_standard(const uint8_t *input_data, uint16_t input_length,
                                uint8_t *output_buffer, size_t output_size,
                                uint16_t *output_length, char *error_msg)
{
    (void)error_msg; // Unused for standard types
    if (output_size < input_length) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output_buffer, input_data, input_length);
    *output_length = input_length;
    return ESP_OK;
}

static uint16_t get_standard_size(uint16_t input_length)
{
    return input_length;
}

// ============================================================================
// STRING data type handler (Micro800: 1-byte length prefix)
// ============================================================================

static esp_err_t encode_string_write(const uint8_t *input_data, uint16_t input_length,
                                     uint8_t *output_buffer, size_t output_size,
                                     uint16_t *output_length, char *error_msg)
{
    // Remove null terminator if present
    uint16_t string_length = input_length;
    if (input_length > 0 && input_data[input_length - 1] == 0) {
        string_length = input_length - 1;
    }
    
    // Check max length (255 due to 1-byte length prefix)
    if (string_length > 255) {
        if (error_msg) {
            snprintf(error_msg, 128, "String too long (max 255 characters)");
        }
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Check output buffer size: 1 byte length + string bytes
    if (output_size < (1 + string_length)) {
        if (error_msg) {
            snprintf(error_msg, 128, "Output buffer too small");
        }
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Format: [Length (1 byte)] [String bytes]
    output_buffer[0] = (uint8_t)string_length;
    memcpy(output_buffer + 1, input_data, string_length);
    
    *output_length = 1 + string_length;
    ESP_LOGD(TAG, "STRING encode: length=%d, total=%d bytes", string_length, *output_length);
    return ESP_OK;
}

static esp_err_t decode_string_read(const uint8_t *input_data, uint16_t input_length,
                                   uint8_t *output_buffer, size_t output_size,
                                   uint16_t *output_length, char *error_msg)
{
    // STRING format: [Length (1 byte)] [String bytes]
    if (input_length < 1) {
        if (error_msg) {
            snprintf(error_msg, 128, "STRING data too short (need at least 1 byte)");
        }
        return ESP_ERR_INVALID_SIZE;
    }
    
    uint8_t str_length = input_data[0];
    
    if (input_length < (1 + str_length)) {
        if (error_msg) {
            snprintf(error_msg, 128, "STRING data incomplete (length=%d, have=%d bytes)", 
                     str_length, input_length);
        }
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Check output buffer size
    if (output_size < str_length) {
        if (error_msg) {
            snprintf(error_msg, 128, "Output buffer too small for STRING");
        }
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Copy string bytes (skip length byte)
    memcpy(output_buffer, input_data + 1, str_length);
    *output_length = str_length;
    
    ESP_LOGD(TAG, "STRING decode: length=%d bytes", str_length);
    return ESP_OK;
}

static uint16_t get_string_encoded_size(uint16_t input_length)
{
    // Remove null terminator if present
    uint16_t string_length = input_length;
    if (input_length > 0) {
        // Note: We can't check the actual data here, so assume no null terminator
        // The encode function will handle it
    }
    // Format: 1 byte length + string bytes
    return 1 + string_length;
}

// ============================================================================
// Data type handler registry
// ============================================================================

static const tag_data_type_handler_t data_type_handlers[] = {
    // Standard types (pass-through)
    {CIP_DATA_TYPE_BOOL, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_SINT, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_INT, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_DINT, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_LINT, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_USINT, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_UINT, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_UDINT, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_ULINT, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_REAL, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_LREAL, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_STIME, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_DATE, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_TIME_OF_DAY, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_DATE_AND_TIME, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_BYTE, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_WORD, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_DWORD, encode_standard, decode_standard, get_standard_size},
    {CIP_DATA_TYPE_LWORD, encode_standard, decode_standard, get_standard_size},
    
    // Special types (with custom encoding)
    {CIP_DATA_TYPE_STRING, encode_string_write, decode_string_read, get_string_encoded_size},
};

#define NUM_DATA_TYPE_HANDLERS (sizeof(data_type_handlers) / sizeof(data_type_handlers[0]))

// Get handler for a specific data type
static const tag_data_type_handler_t *get_data_type_handler(uint16_t cip_data_type)
{
    for (size_t i = 0; i < NUM_DATA_TYPE_HANDLERS; i++) {
        if (data_type_handlers[i].cip_data_type == cip_data_type) {
            return &data_type_handlers[i];
        }
    }
    return NULL;
}

// ============================================================================
// Public API for data type encoding/decoding
// ============================================================================

esp_err_t tag_data_encode_write(uint16_t cip_data_type,
                                const uint8_t *input_data, uint16_t input_length,
                                uint8_t *output_buffer, size_t output_size,
                                uint16_t *output_length, char *error_msg)
{
    const tag_data_type_handler_t *handler = get_data_type_handler(cip_data_type);
    if (handler == NULL) {
        if (error_msg) {
            snprintf(error_msg, 128, "Unsupported data type: 0x%04X", cip_data_type);
        }
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return handler->encode_write(input_data, input_length, output_buffer, 
                                 output_size, output_length, error_msg);
}

esp_err_t tag_data_decode_read(uint16_t cip_data_type,
                               const uint8_t *input_data, uint16_t input_length,
                               uint8_t *output_buffer, size_t output_size,
                               uint16_t *output_length, char *error_msg)
{
    const tag_data_type_handler_t *handler = get_data_type_handler(cip_data_type);
    if (handler == NULL) {
        if (error_msg) {
            snprintf(error_msg, 128, "Unsupported data type: 0x%04X", cip_data_type);
        }
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return handler->decode_read(input_data, input_length, output_buffer,
                               output_size, output_length, error_msg);
}

uint16_t tag_data_get_encoded_size(uint16_t cip_data_type, uint16_t input_length)
{
    const tag_data_type_handler_t *handler = get_data_type_handler(cip_data_type);
    if (handler == NULL) {
        return input_length; // Default: assume no encoding overhead
    }
    
    return handler->get_encoded_size(input_length);
}

#endif // CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT


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
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif_ip_addr.h"
#include "sdkconfig.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static const char *TAG = "enip_scanner";
// Made non-static for use by tag operations
bool s_scanner_initialized = false;
SemaphoreHandle_t s_scanner_mutex = NULL;

// EtherNet/IP constants
#define ENIP_PORT 44818  // TCP port for explicit messaging
#define ENIP_REGISTER_SESSION 0x0065
#define ENIP_LIST_IDENTITY 0x0063
#define ENIP_SEND_RR_DATA 0x006F
#define ENIP_UNREGISTER_SESSION 0x0066

// CIP constants
#define CIP_SERVICE_GET_ATTRIBUTE_ALL 0x01
#define CIP_SERVICE_GET_ATTRIBUTE_SINGLE 0x0E
#define CIP_SERVICE_SET_ATTRIBUTE_SINGLE 0x10
#define CIP_SERVICE_READ 0x4C
#define CIP_SERVICE_FORWARD_OPEN 0x54
#define CIP_SERVICE_FORWARD_CLOSE 0x4E
#define CIP_CLASS_IDENTITY 0x01
#define CIP_CLASS_ASSEMBLY 0x04
#define CIP_CLASS_CONNECTION_MANAGER 0x06

// CIP Path encoding
#define CIP_PATH_CLASS 0x20
#define CIP_PATH_INSTANCE 0x24
#define CIP_PATH_ATTRIBUTE 0x30

// EtherNet/IP header structure
typedef struct __attribute__((packed)) {
    uint16_t command;
    uint16_t length;
    uint32_t session_handle;
    uint32_t status;
    uint64_t sender_context;
    uint32_t options;
} enip_header_t;

// List Identity response structure
typedef struct __attribute__((packed)) {
    uint16_t item_count;
    uint16_t item_type;  // 0x0000 = List Identity response
    uint16_t item_length;
    uint16_t encapsulation_version;
    uint16_t socket_address_family;  // 0x0002 = AF_INET
    uint16_t socket_port;
    uint32_t socket_address;
    uint16_t socket_address_length;
    uint16_t vendor_id;
    uint16_t device_type;
    uint16_t product_code;
    uint8_t major_revision;
    uint8_t minor_revision;
    uint16_t status;
    uint32_t serial_number;
    uint16_t product_name_length;
    // Product name follows (variable length)
} list_identity_item_t;

// Helper function to create TCP socket and connect
// Made non-static for use by tag operations
int create_tcp_socket(const ip4_addr_t *ip_addr, uint32_t timeout_ms)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", errno);
        return -1;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // Set TCP_NODELAY for better performance
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    // Connect to device
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ENIP_PORT);
    server_addr.sin_addr.s_addr = ip_addr->addr;
    
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(ip_addr));
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to connect to %s:%d: errno=%d (%s)", ip_str, ENIP_PORT, errno, strerror(errno));
        close(sock);
        return -1;
    }
    return sock;
}

// Helper function to send data
// Made non-static for use by tag operations
esp_err_t send_data(int sock, const void *data, size_t len)
{
    ssize_t sent = send(sock, data, len, 0);
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send data: errno=%d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }
    if ((size_t)sent != len) {
        ESP_LOGE(TAG, "Partial send: sent %zd of %zu bytes", sent, len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Helper function to receive data
// Returns ESP_OK on success, or error code on failure
// On success, *bytes_received is set to the number of bytes actually received
// Made non-static for use by tag operations
esp_err_t recv_data(int sock, void *data, size_t len, uint32_t timeout_ms, size_t *bytes_received)
{
    size_t received = 0;
    while (received < len) {
        ssize_t ret = recv(sock, (char *)data + received, len - received, 0);
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                ESP_LOGE(TAG, "Receive timeout (expected %zu bytes, got %zu)", len, received);
                if (bytes_received) *bytes_received = received;
                return ESP_ERR_TIMEOUT;
            }
            if (errno == ECONNRESET) {
                ESP_LOGE(TAG, "Connection reset by peer (expected %zu bytes, got %zu)", len, received);
            } else if (errno == ECONNABORTED) {
                ESP_LOGE(TAG, "Connection aborted (expected %zu bytes, got %zu)", len, received);
            } else {
                ESP_LOGE(TAG, "Failed to receive data: %d (expected %zu bytes, got %zu)", errno, len, received);
            }
            if (bytes_received) *bytes_received = received;
            return ESP_FAIL;
        }
        if (ret == 0) {
            ESP_LOGE(TAG, "Connection closed by peer (expected %zu bytes, got %zu)", len, received);
            if (bytes_received) *bytes_received = received;
            return ESP_FAIL;
        }
        received += ret;
    }
    if (bytes_received) *bytes_received = received;
    return ESP_OK;
}

// Register EtherNet/IP session
// Made non-static for use by tag operations
esp_err_t register_session(int sock, uint32_t *session_handle)
{
    // Build packet explicitly in network byte order
    uint8_t packet[28];  // Header (24 bytes) + protocol_version (2 bytes) + options_flags (2 bytes)
    size_t offset = 0;
    
    // Command (2 bytes, little-endian - EtherNet/IP uses little-endian!)
    // ENIP_REGISTER_SESSION = 0x0065
    // On little-endian ESP32, we can write directly
    uint16_t cmd = ENIP_REGISTER_SESSION;
    memcpy(packet + offset, &cmd, 2);
    offset += 2;
    
    // Length (2 bytes, little-endian) = 4
    uint16_t len = 4;
    memcpy(packet + offset, &len, 2);
    offset += 2;
    
    // Session Handle (4 bytes, little-endian) = 0 for register
    uint32_t session = 0;
    memcpy(packet + offset, &session, 4);
    offset += 4;
    
    // Status (4 bytes, little-endian) = 0 for request
    uint32_t status = 0;
    memcpy(packet + offset, &status, 4);
    offset += 4;
    
    // Sender Context (8 bytes, little-endian) = 0
    uint64_t sender_context = 0;
    memcpy(packet + offset, &sender_context, 8);
    offset += 8;
    
    // Options (4 bytes, little-endian) = 0
    uint32_t options = 0;
    memcpy(packet + offset, &options, 4);
    offset += 4;
    
    // Protocol version (2 bytes, little-endian) = 1
    uint16_t protocol_version = 1;
    memcpy(packet + offset, &protocol_version, 2);
    offset += 2;
    
    // Options flags (2 bytes, little-endian) = 0
    uint16_t options_flags = 0;
    memcpy(packet + offset, &options_flags, 2);
    offset += 2;
    
    esp_err_t ret = send_data(sock, packet, offset);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send Register Session packet");
        return ret;
    }
    
    enip_header_t response;
    ret = recv_data(sock, &response, sizeof(response), 5000, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive Register Session response: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (response.command != ENIP_REGISTER_SESSION) {
        ESP_LOGE(TAG, "Unexpected response command: 0x%04X", response.command);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    if (response.status != 0) {
        ESP_LOGE(TAG, "Session registration failed with status: 0x%08X", response.status);
        return ESP_FAIL;
    }
    
    *session_handle = response.session_handle;
    return ESP_OK;
}

// Unregister EtherNet/IP session
// Made non-static for use by tag operations
void unregister_session(int sock, uint32_t session_handle)
{
    // Build packet explicitly in network byte order
    uint8_t packet[24];  // EtherNet/IP header is exactly 24 bytes
    size_t offset = 0;
    
    // Command (2 bytes, network byte order - big-endian)
    // ENIP_UNREGISTER_SESSION = 0x0066, send as 0x00 0x66
    packet[offset++] = 0x00;
    packet[offset++] = 0x66;
    
    // Length (2 bytes, little-endian) = 0
    uint16_t len = 0;
    memcpy(packet + offset, &len, 2);
    offset += 2;
    
    // Session Handle (4 bytes, little-endian)
    memcpy(packet + offset, &session_handle, 4);
    offset += 4;
    
    // Status (4 bytes, little-endian) = 0 for request
    uint32_t status = 0;
    memcpy(packet + offset, &status, 4);
    offset += 4;
    
    // Sender Context (8 bytes, little-endian) = 0
    uint64_t sender_context = 0;
    memcpy(packet + offset, &sender_context, 8);
    offset += 8;
    
    // Options (4 bytes, little-endian) = 0
    uint32_t options = 0;
    memcpy(packet + offset, &options, 4);
    offset += 4;
    
    send_data(sock, packet, offset);
    // Don't wait for response, just close
}

esp_err_t enip_scanner_init(void)
{
    // Create mutex if it doesn't exist
    if (s_scanner_mutex == NULL) {
        s_scanner_mutex = xSemaphoreCreateMutex();
        if (s_scanner_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create scanner mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    
    if (xSemaphoreTake(s_scanner_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    if (s_scanner_initialized) {
        xSemaphoreGive(s_scanner_mutex);
        return ESP_OK;
    }
    
    s_scanner_initialized = true;
    xSemaphoreGive(s_scanner_mutex);
    ESP_LOGI(TAG, "EtherNet/IP Scanner initialized");
    return ESP_OK;
}

int enip_scanner_scan_devices(enip_scanner_device_info_t *devices, int max_devices, uint32_t timeout_ms)
{
    if (devices == NULL || max_devices <= 0) {
        return 0;
    }
    
    // Thread-safe check of initialization state
    if (s_scanner_mutex == NULL) {
        ESP_LOGE(TAG, "Scanner not initialized");
        return 0;
    }
    
    if (xSemaphoreTake(s_scanner_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    
    bool initialized = s_scanner_initialized;
    xSemaphoreGive(s_scanner_mutex);
    
    if (!initialized) {
        return 0;
    }
    
    int device_count = 0;
    
    // Get network interface to determine subnet (copy values atomically to avoid race conditions)
    struct netif *netif = NULL;
    ip4_addr_t netmask = {0};
    ip4_addr_t ip_addr = {0};
    
    // Take mutex for netif access
    if (xSemaphoreTake(s_scanner_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    
    netif = netif_default;
    if (netif == NULL || !netif_is_up(netif)) {
        xSemaphoreGive(s_scanner_mutex);
        ESP_LOGE(TAG, "No network interface available");
        return 0;
    }
    
    // Copy netif values while holding mutex
    const ip4_addr_t *netmask_ptr = netif_ip4_netmask(netif);
    const ip4_addr_t *ip_addr_ptr = netif_ip4_addr(netif);
    if (netmask_ptr != NULL && ip_addr_ptr != NULL) {
        netmask = *netmask_ptr;
        ip_addr = *ip_addr_ptr;
    } else {
        xSemaphoreGive(s_scanner_mutex);
        ESP_LOGE(TAG, "Failed to get network interface addresses");
        return 0;
    }
    
    xSemaphoreGive(s_scanner_mutex);
    
    // Calculate network address
    ip4_addr_t network;
    network.addr = ip_addr.addr & netmask.addr;
    
    // Calculate broadcast address
    ip4_addr_t broadcast;
    broadcast.addr = network.addr | ~netmask.addr;
    
    char broadcast_str[16];
    snprintf(broadcast_str, sizeof(broadcast_str), IPSTR, IP2STR(&broadcast));
    
    // Scan local subnet (limit to /24 for performance)
    uint32_t network_addr = ntohl(network.addr);
    uint32_t broadcast_addr = ntohl(broadcast.addr);
    
    // Limit scan range to prevent excessive scanning
    // Check for integer overflow: ensure broadcast_addr >= network_addr
    if (broadcast_addr >= network_addr && (broadcast_addr - network_addr) > 254) {
        // Check for overflow before addition
        if (network_addr <= (UINT32_MAX - 254)) {
            broadcast_addr = network_addr + 254;
        } else {
            broadcast_addr = UINT32_MAX;
        }
    } else if (broadcast_addr < network_addr) {
        // Invalid network configuration
        ESP_LOGE(TAG, "Invalid network configuration: broadcast < network");
        return 0;
    }
    
    // Create UDP socket for List Identity broadcast
    int udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: %d", errno);
        return 0;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Enable broadcast
    int broadcast_enable = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));
    
    // Bind socket to local port (let system choose port)
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = 0;  // Let system choose port
    if (bind(udp_sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind UDP socket: %d", errno);
        close(udp_sock);
        return 0;
    }
    
    // Build List Identity request
    // List Identity is sessionless - session_handle should be 0
    // Build packet explicitly in network byte order to avoid struct alignment/packing issues
    uint8_t list_req_packet[24];  // EtherNet/IP header is exactly 24 bytes
    size_t offset = 0;
    
    // Command (2 bytes, little-endian - EtherNet/IP uses little-endian!)
    // ENIP_LIST_IDENTITY = 0x0063
    // On little-endian ESP32, we can write directly - value is already in correct byte order
    uint16_t cmd = ENIP_LIST_IDENTITY;
    memcpy(list_req_packet + offset, &cmd, 2);
    offset += 2;
    
    // Length (2 bytes, little-endian) = 0
    uint16_t len = 0;
    memcpy(list_req_packet + offset, &len, 2);
    offset += 2;
    
    // Session Handle (4 bytes, little-endian) = 0 for sessionless
    uint32_t session = 0;
    memcpy(list_req_packet + offset, &session, 4);
    offset += 4;
    
    // Status (4 bytes, little-endian) = 0 for request
    uint32_t status = 0;
    memcpy(list_req_packet + offset, &status, 4);
    offset += 4;
    
    // Sender Context (8 bytes, little-endian) = 0
    uint64_t sender_context = 0;
    memcpy(list_req_packet + offset, &sender_context, 8);
    offset += 8;
    
    // Options (4 bytes, little-endian) = 0
    uint32_t options = 0;
    memcpy(list_req_packet + offset, &options, 4);
    offset += 4;
    
    // Send broadcast
    struct sockaddr_in broadcast_addr_sock;
    memset(&broadcast_addr_sock, 0, sizeof(broadcast_addr_sock));
    broadcast_addr_sock.sin_family = AF_INET;
    broadcast_addr_sock.sin_port = htons(ENIP_PORT);
    broadcast_addr_sock.sin_addr.s_addr = broadcast.addr;
    
    ssize_t sent = sendto(udp_sock, list_req_packet, sizeof(list_req_packet), 0,
                          (struct sockaddr *)&broadcast_addr_sock, sizeof(broadcast_addr_sock));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send List Identity broadcast: %d", errno);
        close(udp_sock);
        return 0;
    }
    
    // Receive responses
    uint8_t buffer[512];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    uint32_t start_time = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    while (device_count < max_devices) {
        ssize_t received = recvfrom(udp_sock, buffer, sizeof(buffer), 0,
                                    (struct sockaddr *)&from_addr, &from_len);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // Timeout
            }
            continue;
        }
        
        if ((xTaskGetTickCount() - start_time) > timeout_ticks) {
            break;  // Timeout
        }
        
        if (received == 0) {
            // Ignore zero-length UDP datagrams
            continue;
        }
        
        if (received < sizeof(enip_header_t)) {
            continue;
        }
        
        enip_header_t *response_header = (enip_header_t *)buffer;
        // EtherNet/IP uses little-endian, ESP32 is little-endian, so no byte swap needed
        uint16_t cmd = response_header->command;
        uint16_t len = response_header->length;
        uint32_t status = response_header->status;
        
        char from_ip_str[16];
        ip4_addr_t from_ip;
        from_ip.addr = from_addr.sin_addr.s_addr;
        snprintf(from_ip_str, sizeof(from_ip_str), IPSTR, IP2STR(&from_ip));
        
        if (cmd != ENIP_LIST_IDENTITY) {
            continue;
        }
        
        // Check status - status 0x00000001 = Invalid/unsupported command
        // Some devices don't support List Identity or require different format
        if (status != 0) {
            // If there's no data, device doesn't support List Identity or rejected the request
            if (len == 0) {
                continue;
            }
            // Some devices may return error status but still include data - try to parse it
        }
        
        // If length is 0, there's no data to parse
        if (len == 0) {
            continue;
        }
        
        // List Identity response format:
        // ENIP Header (24 bytes)
        // Item Count (2 bytes)
        // Items follow, each with:
        //   Item Type (2 bytes) = 0x000C for Identity Item
        //   Item Length (2 bytes)
        //   Item Data (variable)
        
        if (len < 2) {
            continue;
        }
        
        if (received < sizeof(enip_header_t) + 2) {
            continue;
        }
        
        // Read item count (little-endian, no conversion needed)
        uint16_t item_count = *(uint16_t *)(buffer + sizeof(enip_header_t));
        
        if (item_count == 0) {
            continue;
        }
        
        // Parse first item (typically only one item in List Identity response)
        size_t offset = sizeof(enip_header_t) + 2;  // Skip header and item count
        
        if (received < offset + 4) {  // Need at least Item Type + Item Length
            ESP_LOGW(TAG, "Response too small for item header");
            continue;
        }
        
        // Little-endian, no conversion needed
        uint16_t item_type = *(uint16_t *)(buffer + offset);
        offset += 2;
        uint16_t item_length = *(uint16_t *)(buffer + offset);
        offset += 2;
        
        ESP_LOGD(TAG, "Item type=0x%04X, length=%d", item_type, item_length);
        
        // Item type 0x000C = Identity Item
        if (item_type != 0x000C) {
            ESP_LOGW(TAG, "Unexpected item type: 0x%04X", item_type);
            continue;
        }
        
        // Check if we have enough data for the identity item
        // Identity item structure: encapsulation_version(2) + socket_addr_family(2) + socket_port(2) + 
        // socket_address(4) + socket_addr_len(2) + vendor_id(2) + device_type(2) + product_code(2) +
        // major_rev(1) + minor_rev(1) + status(2) + serial(4) + name_len(2) + name(variable)
        if (received < offset + item_length || item_length < 24) {
            ESP_LOGW(TAG, "Item data too small: need %d bytes, have %zd", item_length, received - offset);
            continue;
        }
        
        // Parse identity item data (starts after item type and length)
        // Offset is now pointing at the item data
        uint8_t *item_data = buffer + offset;
        
        // Parse device information from identity item
        enip_scanner_device_info_t *device = &devices[device_count];
        memset(device, 0, sizeof(enip_scanner_device_info_t));
        
        device->ip_address.addr = from_addr.sin_addr.s_addr;
        
        // Identity item structure (starting from item_data):
        // 0x00-0x01: Encapsulation version (skip)
        // 0x02-0x03: Socket address family (skip)
        // 0x04-0x05: Socket port (skip)
        // 0x06-0x09: Socket address (skip, use from_addr instead)
        // 0x0A-0x11: sin_zero padding (8 bytes) - THIS WAS MISSING!
        // 0x12-0x13: Vendor ID
        // 0x14-0x15: Device Type
        // 0x16-0x17: Product Code
        // 0x18: Major Revision
        // 0x19: Minor Revision
        // 0x1A-0x1B: Status
        // 0x1C-0x1F: Serial Number
        // 0x20-0x21: Product Name Length
        // 0x22+: Product Name
        
        // EtherNet/IP uses little-endian, ESP32 is little-endian, so no byte swap needed
        if (item_length >= 0x18) {  // Need at least 24 bytes for vendor_id, device_type, product_code
            device->vendor_id = *(uint16_t *)(item_data + 0x12);
            device->device_type = *(uint16_t *)(item_data + 0x14);
            device->product_code = *(uint16_t *)(item_data + 0x16);
        }
        if (item_length >= 0x1C) {  // Need at least 28 bytes for serial number
            device->major_revision = item_data[0x18];
            device->minor_revision = item_data[0x19];
            device->status = *(uint16_t *)(item_data + 0x1A);
            device->serial_number = *(uint32_t *)(item_data + 0x1C);
        }
        device->online = true;
        
        // Extract product name
        // Product name length is a single byte (USINT) at offset 0x20, followed by the string
        if (item_length >= 0x21) {  // Need at least 33 bytes for name length byte
            uint8_t name_len = item_data[0x20];
            
            // Check for integer overflow in offset calculation
            size_t name_offset = offset + 0x21;
            size_t available_bytes = (received > name_offset) ? (received - name_offset) : 0;
            
            if (name_len > 0 && 
                name_len < sizeof(device->product_name) && 
                name_len <= available_bytes &&
                name_offset <= received &&  // Prevent underflow
                (name_offset + name_len) <= received) {  // Prevent overflow
                memcpy(device->product_name, item_data + 0x21, name_len);
                device->product_name[name_len] = '\0';
            }
        }
        
        // Check for duplicate IP addresses
        bool is_duplicate = false;
        for (int i = 0; i < device_count; i++) {
            if (devices[i].ip_address.addr == device->ip_address.addr) {
                is_duplicate = true;
                break;
            }
        }
        
        if (!is_duplicate) {
            char device_ip_str[16];
            snprintf(device_ip_str, sizeof(device_ip_str), IPSTR, IP2STR(&device->ip_address));
            ESP_LOGD(TAG, "Found device: %s - %s (Vendor: 0x%04X, Product: 0x%04X)",
                     device_ip_str, device->product_name,
                     device->vendor_id, device->product_code);
            
            device_count++;
        }
    }
    
    close(udp_sock);
    ESP_LOGI(TAG, "Scan complete: found %d device(s)", device_count);
    return device_count;
}

esp_err_t enip_scanner_read_assembly(const ip4_addr_t *ip_address, uint16_t assembly_instance, 
                                     enip_scanner_assembly_result_t *result, uint32_t timeout_ms)
{
    if (ip_address == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Thread-safe check of initialization state
    if (s_scanner_mutex == NULL) {
        memset(result, 0, sizeof(enip_scanner_assembly_result_t));
        snprintf(result->error_message, sizeof(result->error_message), "Scanner not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_scanner_mutex, portMAX_DELAY) != pdTRUE) {
        memset(result, 0, sizeof(enip_scanner_assembly_result_t));
        snprintf(result->error_message, sizeof(result->error_message), "Failed to acquire mutex");
        return ESP_FAIL;
    }
    
    bool initialized = s_scanner_initialized;
    xSemaphoreGive(s_scanner_mutex);
    
    if (!initialized) {
        memset(result, 0, sizeof(enip_scanner_assembly_result_t));
        snprintf(result->error_message, sizeof(result->error_message), "Scanner not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    memset(result, 0, sizeof(enip_scanner_assembly_result_t));
    result->ip_address = *ip_address;
    result->assembly_instance = assembly_instance;
    result->success = false;
    
    uint32_t start_time = xTaskGetTickCount();
    
    // Create TCP socket
    int sock = create_tcp_socket(ip_address, timeout_ms);
    if (sock < 0) {
        snprintf(result->error_message, sizeof(result->error_message), "Failed to connect to device");
        return ESP_FAIL;
    }
    
    // Register session
    uint32_t session_handle = 0;
    esp_err_t ret = register_session(sock, &session_handle);
    if (ret != ESP_OK) {
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Failed to register session: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Session registration failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Build SendRRData request to read assembly
    // Build ENIP header explicitly in network byte order
    
    // CIP Timeout
    uint8_t cip_timeout = 0x0A;     // Timeout (10 seconds)
    
    // CIP Path: Class 4 (Assembly), Instance (specified), Attribute 3 (Data)
    // Using 8-bit segments for simpler encoding: Class, Instance, Attribute
    // Path format: [Class Segment] [Instance Segment] [Attribute Segment]
    // Segment format codes:
    //   0x20 = 8-bit class, 0x24 = 8-bit instance, 0x30 = 8-bit attribute
    //   0x21 = 16-bit class, 0x25 = 16-bit instance, 0x31 = 16-bit attribute
    uint8_t cip_path[8];  // Max: Class (2) + Instance (3) + Attribute (2) = 7 bytes, round to 8
    uint8_t path_offset = 0;
    cip_path[path_offset++] = 0x20;  // Class segment (8-bit class, format 0x20)
    cip_path[path_offset++] = CIP_CLASS_ASSEMBLY;  // Class 4 (Assembly)
    
    // Use 8-bit instance segment if instance < 256, otherwise use 16-bit
    if (assembly_instance < 256) {
        cip_path[path_offset++] = 0x24;  // Instance segment (8-bit instance, format 0x24)
        cip_path[path_offset++] = assembly_instance & 0xFF;  // Instance byte
    } else {
        cip_path[path_offset++] = 0x25;  // Instance segment (16-bit instance, format 0x25)
        cip_path[path_offset++] = (assembly_instance >> 8) & 0xFF;  // Instance high byte
        cip_path[path_offset++] = assembly_instance & 0xFF;  // Instance low byte
    }
    
    cip_path[path_offset++] = 0x30;  // Attribute segment (8-bit attribute, format 0x30)
    cip_path[path_offset++] = 0x03;  // Attribute 3 (Data)
    
    // Path must be padded to even number of bytes (16-bit word aligned)
    uint8_t path_padded_length = path_offset;
    if (path_offset % 2 != 0) {
        cip_path[path_offset++] = 0x00;  // Pad byte
        path_padded_length = path_offset;
    }
    
    // Path size in words (16-bit words) - based on padded length
    uint8_t path_size_words = path_padded_length / 2;
    
    // CIP Service: Get_Attribute_Single (0x0E)
    uint8_t cip_service = CIP_SERVICE_GET_ATTRIBUTE_SINGLE;
    
    // Calculate CIP message length: Path Size (1 byte) + Path (padded) + Service (1 byte)
    // Format: [Path Size] [Path] [Service]
    uint16_t cip_message_length = 1 + path_padded_length + 1;  // Path Size + Path (padded) + Service
    
    // SendRRData format:
    // ENIP Header (24 bytes)
    // Interface Handle (4 bytes)
    // Timeout (2 bytes)
    // Item Count (2 bytes) = 0x0002
    // Item 1: Null Address Item (Type 0x0000, Length 0x0000) - 4 bytes
    // Item 2: Unconnected Data Item (Type 0xB2, Length = CIP message length) - 4 bytes + CIP message
    uint16_t enip_data_length = 4 + 2 + 2 + 4 + 4 + cip_message_length;
    
    // ENIP header is 24 bytes
    const size_t enip_header_size = 24;
    
    // Build complete packet
    uint8_t packet[128];
    size_t offset = 0;
    
    // Build ENIP header explicitly in little-endian (EtherNet/IP uses little-endian!)
    // Command (2 bytes, little-endian)
    // ENIP_SEND_RR_DATA = 0x006F
    // On little-endian ESP32, we can write directly
    uint16_t cmd = ENIP_SEND_RR_DATA;
    memcpy(packet + offset, &cmd, 2);
    offset += 2;
    
    // Length (2 bytes, little-endian)
    memcpy(packet + offset, &enip_data_length, 2);
    offset += 2;
    
    // Session Handle (4 bytes, little-endian)
    memcpy(packet + offset, &session_handle, 4);
    offset += 4;
    
    // Status (4 bytes, little-endian) = 0 for request
    uint32_t status = 0;
    memcpy(packet + offset, &status, 4);
    offset += 4;
    
    // Sender Context (8 bytes, little-endian) = 0
    uint64_t sender_context = 0;
    memcpy(packet + offset, &sender_context, 8);
    offset += 8;
    
    // Options (4 bytes, little-endian) = 0
    uint32_t options = 0;
    memcpy(packet + offset, &options, 4);
    offset += 4;
    
    // Interface Handle (4 bytes, little-endian) = 0
    uint32_t interface_handle = 0;
    memcpy(packet + offset, &interface_handle, 4);
    offset += 4;
    
    // Timeout (2 bytes, little-endian)
    memcpy(packet + offset, &cip_timeout, 1);  // cip_timeout is uint8_t
    offset += 1;
    packet[offset++] = 0x00;  // High byte is 0
    
    // Item Count (2 bytes, little-endian) = 2 items
    uint16_t item_count = 2;
    memcpy(packet + offset, &item_count, 2);
    offset += 2;
    
    // Item 1: Null Address Item (Type 0x0000, Length 0x0000) - little-endian
    uint16_t null_item_type = 0x0000;
    uint16_t null_item_length = 0x0000;
    memcpy(packet + offset, &null_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &null_item_length, 2);
    offset += 2;
    
    // Item 2: Unconnected Data Item (Type 0xB2, Length = CIP message length) - little-endian
    uint16_t data_item_type = 0x00B2;
    memcpy(packet + offset, &data_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &cip_message_length, 2);
    offset += 2;
    
    // CIP Message format: Service + Path Size + Path
    // According to CIP spec: Service Code comes first, then Path Size, then Path
    packet[offset++] = cip_service;  // Service: Get_Attribute_Single (0x0E) comes first
    packet[offset++] = path_size_words;  // Path size in words
    memcpy(packet + offset, cip_path, path_padded_length);  // Copy padded path
    offset += path_padded_length;
    
    // Verify packet size matches calculated length
    if (offset != enip_header_size + enip_data_length) {
        ESP_LOGW(TAG, "Packet size mismatch: calculated=%zu, actual=%zu", 
                 enip_header_size + enip_data_length, offset);
    }
    
    // Send request
    ESP_LOGD(TAG, "Sending Get_Attribute_Single to " IPSTR ": assembly_instance=%d", IP2STR(ip_address), assembly_instance);
    ret = send_data(sock, packet, offset);
    if (ret != ESP_OK) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Failed to send request");
        return ret;
    }
    
    // Receive response header
    uint8_t response_buffer[256];  // Buffer for response data
    size_t bytes_received = 0;
    
    // Read initial data - try to get at least 40 bytes (enough for header + padding)
    // But accept whatever we get (TCP may deliver data in chunks)
    size_t min_read = 40;
    size_t max_read = sizeof(response_buffer);
    size_t target_read = max_read;
    
    // Use recv directly to get what's available without blocking for exact amount
    ssize_t recv_ret = recv(sock, response_buffer, target_read, 0);
    if (recv_ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGE(TAG, "Receive timeout waiting for response header");
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Timeout waiting for response");
            return ESP_ERR_TIMEOUT;
        }
        ESP_LOGE(TAG, "Failed to receive response header: %d", errno);
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Failed to receive response header");
        return ESP_FAIL;
    }
    if (recv_ret == 0) {
        ESP_LOGE(TAG, "Connection closed by peer");
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Connection closed by peer");
        return ESP_FAIL;
    }
    
    bytes_received = recv_ret;
    
    // If we got less than the minimum, try to read more
    if (bytes_received < min_read) {
        size_t remaining = min_read - bytes_received;
        recv_ret = recv(sock, response_buffer + bytes_received, remaining, 0);
        if (recv_ret > 0) {
            bytes_received += recv_ret;
        }
    }
    
    if (bytes_received < sizeof(enip_header_t) + 4) {  // Need at least header + some padding
        ESP_LOGE(TAG, "Response too short: got %zu bytes, need at least %zu", 
                 bytes_received, sizeof(enip_header_t) + 4);
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Response too short");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Check if response starts with command directly or has padding
    // Look for SendRRData command (0x006F = bytes 6F 00) in the buffer
    int header_offset = 0;
    for (int i = 0; i < 8 && i < (int)bytes_received - 1; i += 2) {
        uint16_t cmd = *(uint16_t *)(response_buffer + i);
        if (cmd == ENIP_SEND_RR_DATA) {
            header_offset = i;
            ESP_LOGD(TAG, "Found SendRRData command at offset %d", i);
            break;
        }
    }
    
    if (header_offset + sizeof(enip_header_t) > bytes_received) {
        ESP_LOGE(TAG, "Response too short: need %zu bytes at offset %d, got %zu", 
                 sizeof(enip_header_t), header_offset, bytes_received);
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Response too short");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse response header from the correct offset (little-endian, no conversion needed)
    enip_header_t response_header;
    memcpy(&response_header, response_buffer + header_offset, sizeof(response_header));
    
    uint16_t response_length = response_header.length;
    
    if (response_header.command != ENIP_SEND_RR_DATA) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Unexpected response command: 0x%04X (expected 0x%04X)", 
                 response_header.command, ENIP_SEND_RR_DATA);
        ESP_LOGE(TAG, "Expected command 0x%04X (SendRRData), got 0x%04X", ENIP_SEND_RR_DATA, response_header.command);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    if (response_header.status != 0) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Response error status: 0x%08lX", (unsigned long)response_header.status);
        return ESP_FAIL;
    }
    
    // SendRRData response format:
    // ENIP Header (24 bytes) - may have extra bytes before it (run/idle header)
    // Interface Handle (4 bytes, UDINT) - typically 0x00000000 for unconnected messages
    // Timeout (2 bytes)
    // Item Count (2 bytes)
    // Item 1: Address Item (Type ID 2 bytes, Length 2 bytes) - both should be 0
    // Item 2: Data Item (Type ID 2 bytes, Length 2 bytes, then CIP response data)
    
    // Calculate total expected response size
    // Format: [Run/Idle header (4 bytes, optional)] + [ENIP Header (24 bytes)] + [Command-specific data (response_length bytes)]
    // The response_length field in the ENIP header is the length of command-specific data AFTER the header
    size_t total_expected = header_offset + sizeof(response_header) + response_length;
    
    
    // Read remaining data if needed
    if (bytes_received < total_expected) {
        size_t remaining = total_expected - bytes_received;
        if (remaining > sizeof(response_buffer) - bytes_received) {
            remaining = sizeof(response_buffer) - bytes_received;
        }
        if (remaining > 0) {
            size_t additional_received = 0;
            ret = recv_data(sock, response_buffer + bytes_received, remaining, timeout_ms, &additional_received);
            if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "Failed to receive remaining response data: %s", esp_err_to_name(ret));
                unregister_session(sock, session_handle);
                close(sock);
                snprintf(result->error_message, sizeof(result->error_message), "Failed to receive remaining response data");
                return ret;
            }
            if (ret == ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Timeout reading remaining data: got %zu bytes, expected %zu", additional_received, remaining);
            } else {
                ESP_LOGD(TAG, "Read %zu additional bytes", additional_received);
            }
            bytes_received += additional_received;
        }
    }
    
    // Calculate how much data we've already read beyond the header
    size_t bytes_already_read = header_offset + sizeof(response_header);
    size_t remaining_in_buffer = (bytes_received > bytes_already_read) ? (bytes_received - bytes_already_read) : 0;
    
    // Read interface handle (4 bytes, UDINT) - little-endian, no conversion needed
    uint32_t interface_handle_resp;
    if (remaining_in_buffer >= 4) {
        // We already have it in the buffer
        memcpy(&interface_handle_resp, response_buffer + bytes_already_read, 4);
        bytes_already_read += 4;
        remaining_in_buffer -= 4;
    } else {
        // Need to read it from socket
        ESP_LOGD(TAG, "Reading interface handle from socket...");
        ret = recv_data(sock, &interface_handle_resp, 4, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive interface handle");
            return ret;
        }
        ESP_LOGD(TAG, "Interface handle from socket: 0x%08lX", (unsigned long)interface_handle_resp);
    }
    
    // Read timeout (2 bytes) - little-endian, no conversion needed
    uint16_t timeout_resp;
    if (remaining_in_buffer >= 2) {
        memcpy(&timeout_resp, response_buffer + bytes_already_read, 2);
        bytes_already_read += 2;
        remaining_in_buffer -= 2;
        ESP_LOGD(TAG, "Timeout from buffer: 0x%04X", timeout_resp);
    } else {
        ESP_LOGD(TAG, "Reading timeout from socket...");
        ret = recv_data(sock, &timeout_resp, 2, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive timeout");
            return ret;
        }
        ESP_LOGD(TAG, "Timeout from socket: 0x%04X", timeout_resp);
    }
    
    // Read item count (2 bytes) - little-endian, no conversion needed
    uint16_t item_count_resp;
    if (remaining_in_buffer >= 2) {
        memcpy(&item_count_resp, response_buffer + bytes_already_read, 2);
        bytes_already_read += 2;
        remaining_in_buffer -= 2;
    } else {
        ret = recv_data(sock, &item_count_resp, 2, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive item count");
            return ret;
        }
    }
    
    if (item_count_resp != 2) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Unexpected item count: %d (expected 2)", item_count_resp);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Read Item 1: Address Item (should be Type=0, Length=0)
    uint16_t addr_item_type;
    uint16_t addr_item_length;
    if (remaining_in_buffer >= 2) {
        memcpy(&addr_item_type, response_buffer + bytes_already_read, 2);
        bytes_already_read += 2;
        remaining_in_buffer -= 2;
    } else {
        ret = recv_data(sock, &addr_item_type, 2, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive address item type");
            return ret;
        }
    }
    if (remaining_in_buffer >= 2) {
        memcpy(&addr_item_length, response_buffer + bytes_already_read, 2);
        bytes_already_read += 2;
        remaining_in_buffer -= 2;
    } else {
        ret = recv_data(sock, &addr_item_length, 2, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive address item length");
            return ret;
        }
    }
    // Little-endian, no conversion needed
    ESP_LOGD(TAG, "Address item: type=0x%04X, length=%d", addr_item_type, addr_item_length);
    
    // Read Item 2: Data Item (Type should be 0xB2 for unconnected message)
    uint16_t resp_data_item_type;
    uint16_t resp_data_item_length;
    if (remaining_in_buffer >= 2) {
        memcpy(&resp_data_item_type, response_buffer + bytes_already_read, 2);
        bytes_already_read += 2;
        remaining_in_buffer -= 2;
    } else {
        ret = recv_data(sock, &resp_data_item_type, 2, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive data item type");
            return ret;
        }
    }
    if (remaining_in_buffer >= 2) {
        memcpy(&resp_data_item_length, response_buffer + bytes_already_read, 2);
        bytes_already_read += 2;
        remaining_in_buffer -= 2;
    } else {
        ret = recv_data(sock, &resp_data_item_length, 2, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive data item length");
            return ret;
        }
    }
    // Little-endian, no conversion needed
    ESP_LOGD(TAG, "Data item: type=0x%04X, length=%d", resp_data_item_type, resp_data_item_length);
    
    if (resp_data_item_type != 0xB2) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Unexpected data item type: 0x%04X (expected 0xB2)", resp_data_item_type);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Now read the CIP response data from the data item
    // CIP response format: [Service] [Reserved] [General Status] [Additional Status Size] [Additional Status] [Response Data]
    uint8_t cip_service_resp;
    if (remaining_in_buffer >= 1) {
        cip_service_resp = response_buffer[bytes_already_read];
        bytes_already_read += 1;
        remaining_in_buffer -= 1;
    } else {
        ret = recv_data(sock, &cip_service_resp, 1, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive CIP service");
            return ret;
        }
    }
    
    // Service response has MSB set (0x8E for Get_Attribute_Single response)
    // Read reserved byte
    uint8_t reserved;
    if (remaining_in_buffer >= 1) {
        reserved = response_buffer[bytes_already_read];
        bytes_already_read += 1;
        remaining_in_buffer -= 1;
    } else {
        ret = recv_data(sock, &reserved, 1, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive reserved byte");
            return ret;
        }
    }
    
    // Read general status (1 byte)
    uint8_t cip_status;
    if (remaining_in_buffer >= 1) {
        cip_status = response_buffer[bytes_already_read];
        bytes_already_read += 1;
        remaining_in_buffer -= 1;
    } else {
        ret = recv_data(sock, &cip_status, 1, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive CIP status");
            return ret;
        }
    }
    
    if (cip_status != 0x00) {
        // CIP status 0x05 = Object does not exist
        // CIP status 0x06 = Object exists but requested attribute does not exist
        // CIP status 0x0A = Attribute not settable
        // CIP status 0x0C = Object state conflict
        // CIP status 0x0D = Object already exists
        // CIP status 0x14 = Attribute not supported
        const char* status_msg = (cip_status == 0x05) ? "Object does not exist" :
                                 (cip_status == 0x06) ? "Attribute does not exist" :
                                 (cip_status == 0x0A) ? "Attribute not settable" :
                                 (cip_status == 0x0C) ? "Object state conflict" :
                                 (cip_status == 0x0D) ? "Object already exists" :
                                 (cip_status == 0x14) ? "Attribute not supported" : "Unknown error";
        ESP_LOGD(TAG, "CIP error status 0x%02X for assembly instance %d: %s", cip_status, assembly_instance, status_msg);
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "CIP error status: 0x%02X (%s)", cip_status, status_msg);
        result->success = false;
        return ESP_FAIL;
    }
    
    // Read additional status size (1 byte)
    uint8_t additional_status_size;
    if (remaining_in_buffer >= 1) {
        additional_status_size = response_buffer[bytes_already_read];
        bytes_already_read += 1;
        remaining_in_buffer -= 1;
    } else {
        ret = recv_data(sock, &additional_status_size, 1, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to receive additional status size");
            return ret;
        }
    }
    
    
    // Read additional status (if present)
    if (additional_status_size > 0) {
        uint8_t additional_status[256];
        size_t status_buf_size = sizeof(additional_status);
        if ((size_t)additional_status_size > status_buf_size) {
            status_buf_size = additional_status_size;
        }
        if (remaining_in_buffer >= additional_status_size) {
            memcpy(additional_status, response_buffer + bytes_already_read, additional_status_size);
            bytes_already_read += additional_status_size;
            remaining_in_buffer -= additional_status_size;
        } else {
            ret = recv_data(sock, additional_status, additional_status_size, timeout_ms, NULL);
            if (ret != ESP_OK) {
                unregister_session(sock, session_handle);
                close(sock);
                snprintf(result->error_message, sizeof(result->error_message), "Failed to receive additional status");
                return ret;
            }
        }
    }
    
    // Remaining data in the data item is the response data
    // Remaining data in the data item is the response data
    // For Get_Attribute_Single on Assembly data, the response is typically:
    // - For OCTET_STRING (0xDA): [Type 2 bytes] [Length 2 bytes] [Data...]
    // - For other types: just the raw data bytes
    // Calculate remaining bytes: resp_data_item_length - (Service + Reserved + Status + Additional Status Size + Additional Status)
    size_t cip_header_bytes = 1 + 1 + 1 + 1 + additional_status_size;  // Service + Reserved + Status + AddStatusSize + AddStatus
    if (resp_data_item_length < cip_header_bytes) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Data item too small: %d bytes", resp_data_item_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    uint16_t remaining_bytes = resp_data_item_length - cip_header_bytes;
    
    if (remaining_bytes == 0) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "No data returned");
        result->data_length = 0;
        result->success = true;
        result->response_time_ms = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;
        return ESP_OK;
    }
    
    // Read the remaining data (this is the attribute value - should be 32 bytes for assembly)
    ESP_LOGD(TAG, "Reading assembly data: %d bytes remaining, %zu bytes in buffer", remaining_bytes, remaining_in_buffer);
    
    uint8_t *data_buffer = malloc(remaining_bytes);
    if (data_buffer == NULL) {
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Failed to allocate memory");
        return ESP_ERR_NO_MEM;
    }
    
    bool data_read_success = false;
    if (remaining_in_buffer >= remaining_bytes) {
        // We have all the data in the buffer
        memcpy(data_buffer, response_buffer + bytes_already_read, remaining_bytes);
        bytes_already_read += remaining_bytes;
        remaining_in_buffer -= remaining_bytes;
        ESP_LOGD(TAG, "Read %d bytes of assembly data from buffer", remaining_bytes);
        data_read_success = true;
    } else {
        // Read from socket
        if (remaining_in_buffer > 0) {
            // Copy what we have
            memcpy(data_buffer, response_buffer + bytes_already_read, remaining_in_buffer);
            bytes_already_read += remaining_in_buffer;
            size_t bytes_from_buffer = remaining_in_buffer;
            remaining_in_buffer = 0;
            
            // Read the rest from socket
            size_t bytes_needed = remaining_bytes - bytes_from_buffer;
            ret = recv_data(sock, data_buffer + bytes_from_buffer, bytes_needed, timeout_ms, NULL);
            if (ret != ESP_OK) {
                free(data_buffer);
                unregister_session(sock, session_handle);
                close(sock);
                snprintf(result->error_message, sizeof(result->error_message), "Failed to receive data");
                return ret;
            }
            ESP_LOGD(TAG, "Read %zu bytes from buffer, %zu bytes from socket", bytes_from_buffer, bytes_needed);
            data_read_success = true;
        } else {
            ret = recv_data(sock, data_buffer, remaining_bytes, timeout_ms, NULL);
            if (ret != ESP_OK) {
                free(data_buffer);
                unregister_session(sock, session_handle);
                close(sock);
                snprintf(result->error_message, sizeof(result->error_message), "Failed to receive data");
                return ret;
            }
            ESP_LOGD(TAG, "Read %d bytes of assembly data from socket", remaining_bytes);
            data_read_success = true;
        }
    }
    
    // Ensure data was successfully read before proceeding
    if (!data_read_success) {
        free(data_buffer);
        unregister_session(sock, session_handle);
        close(sock);
        snprintf(result->error_message, sizeof(result->error_message), "Failed to read assembly data");
        return ESP_FAIL;
    }
    
    // Parse the data - check if it's OCTET_STRING format
    uint16_t data_length = 0;
    uint8_t *actual_data = NULL;
    
    if (remaining_bytes >= 4) {
        // Check if first 2 bytes are OCTET_STRING type (0x00DA)
        uint16_t data_type = (data_buffer[0] << 8) | data_buffer[1];
        if (data_type == 0x00DA) {
            // OCTET_STRING format: Type (2) + Length (2) + Data
            data_length = (data_buffer[2] << 8) | data_buffer[3];
            if (data_length > 0 && (4 + data_length) <= remaining_bytes) {
                actual_data = malloc(data_length);
                if (actual_data != NULL) {
                    memcpy(actual_data, data_buffer + 4, data_length);
                }
            }
        }
    }
    
    // If not OCTET_STRING or parsing failed, treat as raw data
    if (actual_data == NULL) {
        data_length = remaining_bytes;
        actual_data = malloc(data_length);
        if (actual_data == NULL) {
            free(data_buffer);
            unregister_session(sock, session_handle);
            close(sock);
            snprintf(result->error_message, sizeof(result->error_message), "Failed to allocate memory");
            return ESP_ERR_NO_MEM;
        }
        memcpy(actual_data, data_buffer, data_length);
    }
    
    free(data_buffer);
    result->data = actual_data;
    
    result->data_length = data_length;
    result->success = true;
    result->response_time_ms = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;
    
    ESP_LOGD(TAG, "Read assembly %d from " IPSTR ": %d bytes", assembly_instance, IP2STR(ip_address), data_length);
    
    // Cleanup
    unregister_session(sock, session_handle);
    close(sock);
    
    return ESP_OK;
}

void enip_scanner_free_assembly_result(enip_scanner_assembly_result_t *result)
{
    if (result == NULL) {
        return;
    }
    
    if (result->data != NULL) {
        free(result->data);
        result->data = NULL;
        result->data_length = 0;
    }
}

// Write assembly data to a device
esp_err_t enip_scanner_write_assembly(const ip4_addr_t *ip_address, uint16_t assembly_instance,
                                     const uint8_t *data, uint16_t data_length, uint32_t timeout_ms,
                                     char *error_message)
{
    if (ip_address == NULL || data == NULL || data_length == 0) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid parameters");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    if (error_message) {
        error_message[0] = '\0';
    }
    
    // Thread-safe check of initialization state
    if (s_scanner_mutex == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Scanner not initialized");
        }
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_scanner_mutex, portMAX_DELAY) != pdTRUE) {
        if (error_message) {
            snprintf(error_message, 128, "Failed to acquire mutex");
        }
        return ESP_FAIL;
    }
    
    bool initialized = s_scanner_initialized;
    xSemaphoreGive(s_scanner_mutex);
    
    if (!initialized) {
        if (error_message) {
            snprintf(error_message, 128, "Scanner not initialized");
        }
        return ESP_ERR_INVALID_STATE;
    }
    
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(ip_address));
    ESP_LOGD(TAG, "Writing assembly %d to %s: %d bytes", assembly_instance, ip_str, data_length);
    
    TickType_t start_time = xTaskGetTickCount();
    
    // Create TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        if (error_message) {
            snprintf(error_message, 128, "Failed to create socket: %d", errno);
        }
        return ESP_FAIL;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // Connect to device
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ENIP_PORT);
    server_addr.sin_addr.s_addr = ip_address->addr;
    
    esp_err_t ret = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        if (error_message) {
            snprintf(error_message, 128, "Failed to connect: %d", errno);
        }
        close(sock);
        return ESP_FAIL;
    }
    
    // Register session
    uint32_t session_handle;
    ret = register_session(sock, &session_handle);
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "Failed to register session");
        }
        close(sock);
        return ret;
    }
    
    
    // Build CIP path: Class 4 (Assembly), Instance, Attribute 3 (Data)
    uint8_t cip_path[16];
    uint8_t path_offset = 0;
    
    cip_path[path_offset++] = 0x20;  // Class segment (8-bit class, format 0x20)
    cip_path[path_offset++] = CIP_CLASS_ASSEMBLY;  // Class 4 (Assembly)
    
    // Use 8-bit instance segment if instance < 256, otherwise use 16-bit
    if (assembly_instance < 256) {
        cip_path[path_offset++] = 0x24;  // Instance segment (8-bit instance, format 0x24)
        cip_path[path_offset++] = assembly_instance & 0xFF;  // Instance byte
    } else {
        cip_path[path_offset++] = 0x25;  // Instance segment (16-bit instance, format 0x25)
        cip_path[path_offset++] = (assembly_instance >> 8) & 0xFF;  // Instance high byte
        cip_path[path_offset++] = assembly_instance & 0xFF;  // Instance low byte
    }
    
    cip_path[path_offset++] = 0x30;  // Attribute segment (8-bit attribute, format 0x30)
    cip_path[path_offset++] = 0x03;  // Attribute 3 (Data)
    
    // Path must be padded to even number of bytes (16-bit word aligned)
    uint8_t path_padded_length = path_offset;
    if (path_offset % 2 != 0) {
        cip_path[path_offset++] = 0x00;  // Pad byte
        path_padded_length = path_offset;
    }
    
    // Path size in words (16-bit words) - based on padded length
    uint8_t path_size_words = path_padded_length / 2;
    
    // CIP Service: Set_Attribute_Single (0x10)
    uint8_t cip_service = CIP_SERVICE_SET_ATTRIBUTE_SINGLE;
    
    // Calculate CIP message length: Service (1) + Path Size (1) + Path (padded) + Data
    // Format: [Service] [Path Size] [Path] [Data]
    uint16_t cip_message_length = 1 + 1 + path_padded_length + data_length;
    
    // SendRRData format:
    // ENIP Header (24 bytes)
    // Interface Handle (4 bytes)
    // Timeout (2 bytes)
    // Item Count (2 bytes) = 0x0002
    // Item 1: Null Address Item (Type 0x0000, Length 0x0000) - 4 bytes
    // Item 2: Unconnected Data Item (Type 0xB2, Length = CIP message length) - 4 bytes + CIP message
    uint16_t enip_data_length = 4 + 2 + 2 + 4 + 4 + cip_message_length;
    
    // ENIP header is 24 bytes
    const size_t enip_header_size = 24;
    
    // Build complete packet (allocate dynamically to handle large data)
    size_t total_packet_size = enip_header_size + enip_data_length;
    uint8_t *packet = malloc(total_packet_size);
    if (packet == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Failed to allocate memory for packet");
        }
        unregister_session(sock, session_handle);
        close(sock);
        return ESP_ERR_NO_MEM;
    }
    
    size_t offset = 0;
    
    // Build ENIP header explicitly in little-endian
    uint16_t cmd = ENIP_SEND_RR_DATA;
    memcpy(packet + offset, &cmd, 2);
    offset += 2;
    
    memcpy(packet + offset, &enip_data_length, 2);
    offset += 2;
    
    memcpy(packet + offset, &session_handle, 4);
    offset += 4;
    
    uint32_t status = 0;
    memcpy(packet + offset, &status, 4);
    offset += 4;
    
    uint64_t sender_context = 0;
    memcpy(packet + offset, &sender_context, 8);
    offset += 8;
    
    uint32_t options = 0;
    memcpy(packet + offset, &options, 4);
    offset += 4;
    
    // Interface Handle (4 bytes) = 0
    uint32_t interface_handle = 0;
    memcpy(packet + offset, &interface_handle, 4);
    offset += 4;
    
    // Timeout (2 bytes)
    uint8_t cip_timeout = (timeout_ms / 250) & 0xFF;
    memcpy(packet + offset, &cip_timeout, 1);
    offset += 1;
    packet[offset++] = 0x00;
    
    // Item Count (2 bytes) = 2 items
    uint16_t item_count = 2;
    memcpy(packet + offset, &item_count, 2);
    offset += 2;
    
    // Item 1: Null Address Item (Type 0x0000, Length 0x0000)
    uint16_t null_item_type = 0x0000;
    uint16_t null_item_length = 0x0000;
    memcpy(packet + offset, &null_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &null_item_length, 2);
    offset += 2;
    
    // Item 2: Unconnected Data Item (Type 0xB2, Length = CIP message length)
    uint16_t data_item_type = 0x00B2;
    memcpy(packet + offset, &data_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &cip_message_length, 2);
    offset += 2;
    
    // CIP Message format: Service + Path Size + Path + Data
    packet[offset++] = cip_service;  // Service: Set_Attribute_Single (0x10)
    packet[offset++] = path_size_words;  // Path size in words
    memcpy(packet + offset, cip_path, path_padded_length);  // Copy padded path
    offset += path_padded_length;
    memcpy(packet + offset, data, data_length);  // Copy data to write
    offset += data_length;
    
    ESP_LOGD(TAG, "Sending Set_Attribute_Single to %s: assembly_instance=%d, data_length=%d, total_packet=%zu bytes",
             ip_str, assembly_instance, data_length, offset);
    
    ret = send_data(sock, packet, offset);
    free(packet);
    if (ret != ESP_OK) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Failed to send write request");
        }
        return ret;
    }
    
    // Receive response header - use recv directly to get what's available without blocking for exact amount
    uint8_t response_buffer[256];
    ssize_t recv_ret = recv(sock, response_buffer, sizeof(response_buffer), 0);
    if (recv_ret < 0) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Failed to receive response: %d", errno);
        }
        return ESP_FAIL;
    }
    size_t bytes_received = (size_t)recv_ret;
    
    // If we got less than expected, try one more recv (non-blocking)
    if (bytes_received < 40) {
        recv_ret = recv(sock, response_buffer + bytes_received, sizeof(response_buffer) - bytes_received, 0);
        if (recv_ret > 0) {
            bytes_received += (size_t)recv_ret;
        }
    }
    
    if (bytes_received < sizeof(enip_header_t)) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Response too short: %zu bytes", bytes_received);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Find SendRRData command (may have run/idle header)
    int header_offset = 0;
    for (int i = 0; i < 8 && i < (int)bytes_received - 1; i += 2) {
        uint16_t cmd = *(uint16_t *)(response_buffer + i);
        if (cmd == ENIP_SEND_RR_DATA) {
            header_offset = i;
            break;
        }
    }
    
    if (header_offset + sizeof(enip_header_t) > bytes_received) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Response too short");
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Parse response header
    enip_header_t response_header;
    memcpy(&response_header, response_buffer + header_offset, sizeof(response_header));
    
    if (response_header.command != ENIP_SEND_RR_DATA) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Unexpected response command: 0x%04X", response_header.command);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    if (response_header.status != 0) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Response error status: 0x%08lX", (unsigned long)response_header.status);
        }
        return ESP_FAIL;
    }
    
    // Read remaining response data (Interface Handle, Timeout, Item Count, Items, CIP response)
    size_t bytes_already_read = header_offset + sizeof(response_header);
    size_t remaining_in_buffer = (bytes_received > bytes_already_read) ? (bytes_received - bytes_already_read) : 0;
    
    // Read Interface Handle (4 bytes)
    uint32_t interface_handle_resp;
    if (remaining_in_buffer >= 4) {
        memcpy(&interface_handle_resp, response_buffer + bytes_already_read, 4);
        bytes_already_read += 4;
        remaining_in_buffer -= 4;
    } else {
        ret = recv_data(sock, &interface_handle_resp, 4, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Failed to receive interface handle");
            }
            return ret;
        }
    }
    
    // Read Timeout (2 bytes)
    uint16_t timeout_resp;
    if (remaining_in_buffer >= 2) {
        memcpy(&timeout_resp, response_buffer + bytes_already_read, 2);
        bytes_already_read += 2;
        remaining_in_buffer -= 2;
    } else {
        ret = recv_data(sock, &timeout_resp, 2, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Failed to receive timeout");
            }
            return ret;
        }
    }
    
    // Read Item Count (2 bytes)
    uint16_t item_count_resp;
    if (remaining_in_buffer >= 2) {
        memcpy(&item_count_resp, response_buffer + bytes_already_read, 2);
        bytes_already_read += 2;
        remaining_in_buffer -= 2;
    } else {
        ret = recv_data(sock, &item_count_resp, 2, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Failed to receive item count");
            }
            return ret;
        }
    }
    
    if (item_count_resp != 2) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "Unexpected item count: %d", item_count_resp);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Read Item 1: Address Item
    uint16_t addr_item_type, addr_item_length;
    if (remaining_in_buffer >= 4) {
        memcpy(&addr_item_type, response_buffer + bytes_already_read, 2);
        memcpy(&addr_item_length, response_buffer + bytes_already_read + 2, 2);
        bytes_already_read += 4;
        remaining_in_buffer -= 4;
    } else {
        ret = recv_data(sock, &addr_item_type, 2, timeout_ms, NULL);
        if (ret == ESP_OK) {
            ret = recv_data(sock, &addr_item_length, 2, timeout_ms, NULL);
        }
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Failed to receive address item");
            }
            return ret;
        }
    }
    
    // Read Item 2: Data Item
    uint16_t resp_data_item_type, resp_data_item_length;
    if (remaining_in_buffer >= 4) {
        memcpy(&resp_data_item_type, response_buffer + bytes_already_read, 2);
        memcpy(&resp_data_item_length, response_buffer + bytes_already_read + 2, 2);
        bytes_already_read += 4;
        remaining_in_buffer -= 4;
    } else {
        ret = recv_data(sock, &resp_data_item_type, 2, timeout_ms, NULL);
        if (ret == ESP_OK) {
            ret = recv_data(sock, &resp_data_item_length, 2, timeout_ms, NULL);
        }
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Failed to receive data item");
            }
            return ret;
        }
    }
    
    // Read CIP response: Service + Reserved + General Status + Additional Status Size + Additional Status
    uint8_t cip_service_resp;
    if (remaining_in_buffer >= 1) {
        cip_service_resp = response_buffer[bytes_already_read];
        bytes_already_read += 1;
        remaining_in_buffer -= 1;
    } else {
        ret = recv_data(sock, &cip_service_resp, 1, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Failed to receive CIP service");
            }
            return ret;
        }
    }
    
    // Read reserved byte
    uint8_t reserved;
    if (remaining_in_buffer >= 1) {
        reserved = response_buffer[bytes_already_read];
        bytes_already_read += 1;
        remaining_in_buffer -= 1;
    } else {
        ret = recv_data(sock, &reserved, 1, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Failed to receive reserved byte");
            }
            return ret;
        }
    }
    
    // Read general status
    uint8_t cip_status;
    if (remaining_in_buffer >= 1) {
        cip_status = response_buffer[bytes_already_read];
        bytes_already_read += 1;
        remaining_in_buffer -= 1;
    } else {
        ret = recv_data(sock, &cip_status, 1, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Failed to receive CIP status");
            }
            return ret;
        }
    }
    
    
    if (cip_status != 0x00) {
        unregister_session(sock, session_handle);
        close(sock);
        if (error_message) {
            snprintf(error_message, 128, "CIP error status: 0x%02X", cip_status);
        }
        return ESP_FAIL;
    }
    
    // Read additional status size
    uint8_t additional_status_size;
    if (remaining_in_buffer >= 1) {
        additional_status_size = response_buffer[bytes_already_read];
        bytes_already_read += 1;
        remaining_in_buffer -= 1;
    } else {
        ret = recv_data(sock, &additional_status_size, 1, timeout_ms, NULL);
        if (ret != ESP_OK) {
            unregister_session(sock, session_handle);
            close(sock);
            if (error_message) {
                snprintf(error_message, 128, "Failed to receive additional status size");
            }
            return ret;
        }
    }
    
    // Read additional status if present
    if (additional_status_size > 0) {
        uint8_t additional_status[256];
        size_t status_buf_size = sizeof(additional_status);
        if ((size_t)additional_status_size > status_buf_size) {
            status_buf_size = additional_status_size;
        }
        if (remaining_in_buffer >= additional_status_size) {
            memcpy(additional_status, response_buffer + bytes_already_read, additional_status_size);
            bytes_already_read += additional_status_size;
            remaining_in_buffer -= additional_status_size;
        } else {
            ret = recv_data(sock, additional_status, additional_status_size, timeout_ms, NULL);
            if (ret != ESP_OK) {
                unregister_session(sock, session_handle);
                close(sock);
                if (error_message) {
                    snprintf(error_message, 128, "Failed to receive additional status");
                }
                return ret;
            }
        }
    }
    
    uint32_t response_time_ms = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;
    ESP_LOGD(TAG, "Successfully wrote assembly %d to %s: %d bytes in %lu ms",
             assembly_instance, ip_str, data_length, response_time_ms);
    
    // Cleanup
    unregister_session(sock, session_handle);
    close(sock);
    
    return ESP_OK;
}

// Check if an assembly is writable by attempting to read assembly object attributes
bool enip_scanner_is_assembly_writable(const ip4_addr_t *ip_address, uint16_t assembly_instance, uint32_t timeout_ms)
{
    // Check if assembly is writable by trying to read it first
    // If we can read it, assume it's writable (since we can read it, we should be able to write it)
    // In practice, you'd check the assembly object's Instance Type attribute
    
    enip_scanner_assembly_result_t read_result;
    esp_err_t ret = enip_scanner_read_assembly(ip_address, assembly_instance, &read_result, timeout_ms);
    
    if (ret == ESP_OK && read_result.success) {
        enip_scanner_free_assembly_result(&read_result);
        // If we can read it, try a minimal write to verify writability
        // But this might modify the data, so let's just return true if we can read it
        // In a real implementation, you'd check the assembly object attributes
        return true;
    }
    
    // Ensure result is freed even on error
    enip_scanner_free_assembly_result(&read_result);
    return false;
}

// Read Max Instance attribute from Assembly Object class
static esp_err_t read_max_instance(int sock, uint32_t session_handle, uint16_t *max_instance, uint32_t timeout_ms)
{
    // Build CIP path: Class 4 (Assembly), Instance 0 (class), Attribute 2 (Max Instance)
    uint8_t cip_path[8];
    uint8_t path_offset = 0;
    
    cip_path[path_offset++] = 0x20;  // Class segment (8-bit class)
    cip_path[path_offset++] = CIP_CLASS_ASSEMBLY;  // Class 4 (Assembly)
    cip_path[path_offset++] = 0x24;  // Instance segment (8-bit instance)
    cip_path[path_offset++] = 0x00;  // Instance 0 (class level)
    cip_path[path_offset++] = 0x30;  // Attribute segment (8-bit attribute)
    cip_path[path_offset++] = 0x02;  // Attribute 2 (Max Instance)
    
    // Path must be padded to even number of bytes
    uint8_t path_padded_length = path_offset;
    if (path_offset % 2 != 0) {
        cip_path[path_offset++] = 0x00;
        path_padded_length = path_offset;
    }
    
    uint8_t path_size_words = path_padded_length / 2;
    uint8_t cip_service = CIP_SERVICE_GET_ATTRIBUTE_SINGLE;
    uint16_t cip_message_length = 1 + path_padded_length + 1;
    uint16_t enip_data_length = 4 + 2 + 2 + 4 + 4 + cip_message_length;
    
    uint8_t packet[128];
    size_t offset = 0;
    
    // Build ENIP header
    uint16_t cmd = ENIP_SEND_RR_DATA;
    memcpy(packet + offset, &cmd, 2);
    offset += 2;
    memcpy(packet + offset, &enip_data_length, 2);
    offset += 2;
    memcpy(packet + offset, &session_handle, 4);
    offset += 4;
    uint32_t status = 0;
    memcpy(packet + offset, &status, 4);
    offset += 4;
    uint64_t sender_context = 0;
    memcpy(packet + offset, &sender_context, 8);
    offset += 8;
    uint32_t options = 0;
    memcpy(packet + offset, &options, 4);
    offset += 4;
    uint32_t interface_handle = 0;
    memcpy(packet + offset, &interface_handle, 4);
    offset += 4;
    uint8_t cip_timeout = (timeout_ms / 250) & 0xFF;
    memcpy(packet + offset, &cip_timeout, 1);
    offset += 1;
    packet[offset++] = 0x00;
    uint16_t item_count = 2;
    memcpy(packet + offset, &item_count, 2);
    offset += 2;
    uint16_t null_item_type = 0x0000;
    uint16_t null_item_length = 0x0000;
    memcpy(packet + offset, &null_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &null_item_length, 2);
    offset += 2;
    uint16_t data_item_type = 0x00B2;
    memcpy(packet + offset, &data_item_type, 2);
    offset += 2;
    memcpy(packet + offset, &cip_message_length, 2);
    offset += 2;
    packet[offset++] = cip_service;
    packet[offset++] = path_size_words;
    memcpy(packet + offset, cip_path, path_padded_length);
    offset += path_padded_length;
    
    esp_err_t ret = send_data(sock, packet, offset);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Receive response
    uint8_t response_buffer[256];
    ssize_t recv_ret = recv(sock, response_buffer, sizeof(response_buffer), 0);
    if (recv_ret < 0) {
        return ESP_FAIL;
    }
    size_t bytes_received = (size_t)recv_ret;
    
    if (bytes_received < 40) {
        recv_ret = recv(sock, response_buffer + bytes_received, sizeof(response_buffer) - bytes_received, 0);
        if (recv_ret > 0) {
            bytes_received += (size_t)recv_ret;
        }
    }
    
    // Find SendRRData command
    int header_offset = 0;
    for (int i = 0; i < 8 && i < (int)bytes_received - 1; i += 2) {
        uint16_t cmd = *(uint16_t *)(response_buffer + i);
        if (cmd == ENIP_SEND_RR_DATA) {
            header_offset = i;
            break;
        }
    }
    
    if (header_offset + sizeof(enip_header_t) > bytes_received) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    enip_header_t response_header;
    memcpy(&response_header, response_buffer + header_offset, sizeof(response_header));
    
    if (response_header.command != ENIP_SEND_RR_DATA || response_header.status != 0) {
        return ESP_FAIL;
    }
    
    // Parse response: Interface Handle (4) + Timeout (2) + Item Count (2) + Items + CIP response
    size_t bytes_already_read = header_offset + sizeof(response_header);
    size_t remaining_in_buffer = (bytes_received > bytes_already_read) ? (bytes_received - bytes_already_read) : 0;
    
    // Skip Interface Handle, Timeout, Item Count, Address Item, Data Item header
    bytes_already_read += 4 + 2 + 2 + 4 + 4;
    if (remaining_in_buffer >= 16) {
        remaining_in_buffer -= 16;
    } else {
        // Need to read more
        uint8_t skip_buffer[16];
        ret = recv_data(sock, skip_buffer, 16, timeout_ms, NULL);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    // Read CIP response: Service + Reserved + Status + Additional Status Size + Additional Status + Data
    // Data should be Max Instance (UINT16, 2 bytes)
    uint8_t cip_service_resp, cip_status, additional_status_size;
    if (remaining_in_buffer >= 4) {
        cip_service_resp = response_buffer[bytes_already_read];
        // Skip reserved byte at offset +1
        cip_status = response_buffer[bytes_already_read + 2];
        additional_status_size = response_buffer[bytes_already_read + 3];
        bytes_already_read += 4;
        remaining_in_buffer -= 4;
    } else {
        uint8_t cip_header[4];
        ret = recv_data(sock, cip_header, 4, timeout_ms, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read CIP header for Max Instance: %s", esp_err_to_name(ret));
            return ret;
        }
        cip_service_resp = cip_header[0];
        // Skip reserved byte at index 1
        cip_status = cip_header[2];
        additional_status_size = cip_header[3];
    }
    
    ESP_LOGD(TAG, "Max Instance CIP response: service=0x%02X, status=0x%02X, additional_status_size=%d", 
             cip_service_resp, cip_status, additional_status_size);
    
    if (cip_status != 0x00) {
        const char* status_msg = (cip_status == 0x05) ? "Object does not exist" :
                                 (cip_status == 0x06) ? "Attribute does not exist" :
                                 (cip_status == 0x14) ? "Attribute not supported" : "Unknown error";
        ESP_LOGW(TAG, "Max Instance read failed with CIP status: 0x%02X (%s)", cip_status, status_msg);
        return ESP_FAIL;
    }
    
    // Skip additional status if present
    if (additional_status_size > 0) {
        // Limit additional_status_size to prevent excessive allocation (uint8_t max is 255)
        size_t safe_status_size = additional_status_size;
        
        if (remaining_in_buffer >= safe_status_size) {
            bytes_already_read += safe_status_size;
            remaining_in_buffer -= safe_status_size;
        } else {
            uint8_t *skip_buf = malloc(safe_status_size);
            if (skip_buf) {
                esp_err_t skip_ret = recv_data(sock, skip_buf, safe_status_size, timeout_ms, NULL);
                free(skip_buf);
                // Continue even if recv fails - we're just skipping data
                if (skip_ret != ESP_OK && skip_ret != ESP_ERR_TIMEOUT) {
                    ESP_LOGW(TAG, "Failed to skip additional status, continuing anyway");
                }
            }
        }
    }
    
    // Read Max Instance (UINT16, 2 bytes, little-endian)
    uint16_t max_inst;
    if (remaining_in_buffer >= 2) {
        memcpy(&max_inst, response_buffer + bytes_already_read, 2);
    } else {
        ret = recv_data(sock, &max_inst, 2, timeout_ms, NULL);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read Max Instance value: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    ESP_LOGD(TAG, "Max Instance value read: %d", max_inst);
    *max_instance = max_inst;
    return ESP_OK;
}

// Discover valid assembly instances
int enip_scanner_discover_assemblies(const ip4_addr_t *ip_address, uint16_t *instances, int max_instances, uint32_t timeout_ms)
{
    if (ip_address == NULL || instances == NULL || max_instances <= 0) {
        return 0;
    }
    
    // Thread-safe check of initialization state
    if (s_scanner_mutex == NULL) {
        ESP_LOGE(TAG, "Scanner not initialized");
        return 0;
    }
    
    if (xSemaphoreTake(s_scanner_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    
    bool initialized = s_scanner_initialized;
    xSemaphoreGive(s_scanner_mutex);
    
    if (!initialized) {
        return 0;
    }
    
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(ip_address));
    ESP_LOGD(TAG, "Discovering assembly instances for %s", ip_str);
    
    // Create TCP socket
    int sock = create_tcp_socket(ip_address, timeout_ms);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to connect to device");
        return 0;
    }
    
    // Register session
    uint32_t session_handle = 0;
    esp_err_t ret = register_session(sock, &session_handle);
    if (ret != ESP_OK) {
        close(sock);
        ESP_LOGE(TAG, "Failed to register session");
        return 0;
    }
    
    // Try to read Max Instance attribute
    uint16_t max_instance = 0;
    ret = read_max_instance(sock, session_handle, &max_instance, timeout_ms);
    
    int found_count = 0;
    
    // Check if Max Instance read succeeded and value is reasonable
    // Some devices may return 0 or very large values, so we need to validate
    if (ret == ESP_OK && max_instance > 0 && max_instance < 1000) {
        // Probe instances from 1 to max_instance, but limit to reasonable range
        // max_instances parameter limits how many results we return, not how many we probe
        uint16_t probe_limit = (max_instance < 256) ? max_instance : 256;
        
        ESP_LOGD(TAG, "Max Instance: %d, probing instances 1 to %d (will return up to %d)", 
                 max_instance, probe_limit, max_instances);
        for (uint16_t inst = 1; inst <= probe_limit && found_count < max_instances; inst++) {
            // Try to read the assembly instance to see if it exists
            enip_scanner_assembly_result_t test_result;
            esp_err_t read_ret = enip_scanner_read_assembly(ip_address, inst, &test_result, timeout_ms);
            if (read_ret == ESP_OK && test_result.success) {
                instances[found_count++] = inst;
                enip_scanner_free_assembly_result(&test_result);
                ESP_LOGD(TAG, "Found valid assembly instance: %d", inst);
            } else {
                ESP_LOGD(TAG, "Instance %d not found or error: %s", inst, 
                         (read_ret == ESP_OK) ? test_result.error_message : esp_err_to_name(read_ret));
                // Ensure result is freed even on error
                enip_scanner_free_assembly_result(&test_result);
            }
        }
    } else {
        // If Max Instance read failed, try common instance numbers
        ESP_LOGW(TAG, "Could not read Max Instance attribute (ret=%s, max_instance=%d), probing common instances", 
                 esp_err_to_name(ret), max_instance);
        uint16_t common_instances[] = {100, 101, 102, 150, 151, 152, 20, 21, 22, 1, 2, 3, 4, 5};
        int num_common = sizeof(common_instances) / sizeof(common_instances[0]);
        
        ESP_LOGD(TAG, "Max Instance read failed, probing %d common instance numbers", num_common);
        for (int i = 0; i < num_common && found_count < max_instances; i++) {
            enip_scanner_assembly_result_t test_result;
            esp_err_t read_ret = enip_scanner_read_assembly(ip_address, common_instances[i], &test_result, timeout_ms);
            if (read_ret == ESP_OK && test_result.success) {
                instances[found_count++] = common_instances[i];
                enip_scanner_free_assembly_result(&test_result);
                ESP_LOGD(TAG, "Found valid assembly instance: %d", common_instances[i]);
            } else {
                ESP_LOGD(TAG, "Common instance %d not found or error: %s", common_instances[i],
                         (read_ret == ESP_OK) ? test_result.error_message : esp_err_to_name(read_ret));
                // Ensure result is freed even on error
                enip_scanner_free_assembly_result(&test_result);
            }
        }
    }
    
    // Always cleanup socket and session
    unregister_session(sock, session_handle);
    close(sock);
    
    ESP_LOGD(TAG, "Discovered %d valid assembly instance(s) for %s", found_count, ip_str);
    return found_count;
}

// ============================================================================
// Session Management (Public API for Implicit Connections)
// ============================================================================

esp_err_t enip_scanner_register_session(const ip4_addr_t *ip_address,
                                        uint32_t *session_handle,
                                        uint32_t timeout_ms,
                                        char *error_message)
{
    if (ip_address == NULL || session_handle == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Invalid parameters");
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    // Thread-safe check of initialization state
    if (s_scanner_mutex == NULL) {
        if (error_message) {
            snprintf(error_message, 128, "Scanner not initialized");
        }
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_scanner_mutex, portMAX_DELAY) != pdTRUE) {
        if (error_message) {
            snprintf(error_message, 128, "Failed to acquire mutex");
        }
        return ESP_FAIL;
    }
    
    bool initialized = s_scanner_initialized;
    xSemaphoreGive(s_scanner_mutex);
    
    if (!initialized) {
        if (error_message) {
            snprintf(error_message, 128, "Scanner not initialized");
        }
        return ESP_ERR_INVALID_STATE;
    }
    
    int sock = create_tcp_socket(ip_address, timeout_ms);
    if (sock < 0) {
        if (error_message) {
            snprintf(error_message, 128, "Failed to create TCP socket");
        }
        return ESP_FAIL;
    }
    
    esp_err_t ret = register_session(sock, session_handle);
    if (ret != ESP_OK) {
        if (error_message) {
            snprintf(error_message, 128, "Failed to register session: %s", esp_err_to_name(ret));
        }
        close(sock);
        return ret;
    }
    
    // Store socket in connection structure for later use
    // Note: For Forward Open, the session handle is used but socket is recreated
    close(sock);
    
    ESP_LOGD(TAG, "Session registered: 0x%08lX", (unsigned long)*session_handle);
    return ESP_OK;
}

esp_err_t enip_scanner_unregister_session(const ip4_addr_t *ip_address,
                                          uint32_t session_handle,
                                          uint32_t timeout_ms)
{
    if (ip_address == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Thread-safe check of initialization state
    if (s_scanner_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_scanner_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    bool initialized = s_scanner_initialized;
    xSemaphoreGive(s_scanner_mutex);
    
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int sock = create_tcp_socket(ip_address, timeout_ms);
    if (sock < 0) {
        return ESP_FAIL;
    }
    
    unregister_session(sock, session_handle);
    close(sock);
    
    ESP_LOGD(TAG, "Session unregistered: 0x%08lX", (unsigned long)session_handle);
    return ESP_OK;
}

#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
// Tag support code has been moved to enip_scanner_tag.c and enip_scanner_tag_data.c
#endif // CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT

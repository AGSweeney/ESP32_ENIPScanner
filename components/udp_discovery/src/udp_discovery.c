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
 * @file udp_discovery.c
 * @brief UDP Discovery Responder implementation
 * 
 * Implements a UDP responder that listens for "DISCOVER" messages on port 50000
 * and responds with device information in the format:
 * "SERVER FOUND:<hostname>;IP:<ip_address>"
 * 
 * Compatible with UDPDiscovery protocol:
 * https://github.com/agsweeney1972/UDPDiscovery
 */

#include "udp_discovery.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/ip4_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <errno.h>

static const char *TAG = "udp_discovery";

#define DISCOVERY_PORT 50000
#define DISCOVERY_MAGIC "DISCOVER"
#define RESPONSE_FORMAT "SERVER FOUND:%s;IP:%s"

static TaskHandle_t s_discovery_task_handle = NULL;
static bool s_discovery_running = false;
static SemaphoreHandle_t s_discovery_mutex = NULL;

/**
 * @brief Get the device hostname
 * 
 * @param hostname Buffer to store hostname (must be at least 64 bytes)
 * @param hostname_len Size of hostname buffer
 * @return ESP_OK on success
 */
static esp_err_t get_hostname(char *hostname, size_t hostname_len)
{
    if (hostname == NULL || hostname_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Get hostname from netif
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (netif == NULL) {
        // Fallback to default hostname
        strncpy(hostname, "ESP32-ENIPScanner", hostname_len - 1);
        hostname[hostname_len - 1] = '\0';
        return ESP_OK;
    }

    const char *netif_hostname = NULL;
    esp_err_t ret = esp_netif_get_hostname(netif, &netif_hostname);
    if (ret == ESP_OK && netif_hostname != NULL && strlen(netif_hostname) > 0) {
        strncpy(hostname, netif_hostname, hostname_len - 1);
        hostname[hostname_len - 1] = '\0';
    } else {
        // Fallback to default hostname
        strncpy(hostname, "ESP32-ENIPScanner", hostname_len - 1);
        hostname[hostname_len - 1] = '\0';
    }

    return ESP_OK;
}

/**
 * @brief Get the device IP address as a string
 * 
 * @param ip_str Buffer to store IP address string (must be at least 16 bytes)
 * @param ip_str_len Size of ip_str buffer
 * @return ESP_OK on success
 */
static esp_err_t get_ip_address(char *ip_str, size_t ip_str_len)
{
    if (ip_str == NULL || ip_str_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *netif = esp_netif_get_default_netif();
    if (netif == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }

    // Convert IP address to string
    ip4_addr_t ip_addr;
    ip_addr.addr = ip_info.ip.addr;
    snprintf(ip_str, ip_str_len, IPSTR, IP2STR(&ip_addr));

    return ESP_OK;
}

/**
 * @brief UDP discovery responder task
 * 
 * Listens for "DISCOVER" messages and responds with device information
 */
static void udp_discovery_task(void *pvParameters)
{
    int sock = -1;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char recv_buffer[128];
    char response_buffer[256];
    char hostname[64];
    char ip_str[16];

    ESP_LOGI(TAG, "Starting UDP discovery responder on port %d", DISCOVERY_PORT);

    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", errno);
        s_discovery_running = false;
        s_discovery_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Set socket options
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Enable broadcast reception
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    // Set receive timeout (1 second)
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Bind socket to port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(DISCOVERY_PORT);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket to port %d: %d", DISCOVERY_PORT, errno);
        close(sock);
        s_discovery_running = false;
        s_discovery_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP discovery responder listening on port %d", DISCOVERY_PORT);

    // Get hostname and IP address once
    if (get_hostname(hostname, sizeof(hostname)) != ESP_OK) {
        strncpy(hostname, "ESP32-ENIPScanner", sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    }

    // Main receive loop
    while (s_discovery_running) {
        // Receive discovery packet
        ssize_t recv_len = recvfrom(sock, recv_buffer, sizeof(recv_buffer) - 1, 0,
                                     (struct sockaddr *)&client_addr, &client_addr_len);

        if (recv_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout or no data, continue
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            ESP_LOGW(TAG, "recvfrom error: %d", errno);
            continue;
        }

        // Null-terminate received data
        recv_buffer[recv_len] = '\0';

        // Check if it's a discovery message
        if (strncmp(recv_buffer, DISCOVERY_MAGIC, strlen(DISCOVERY_MAGIC)) == 0) {
            // Get current IP address (in case it changed)
            if (get_ip_address(ip_str, sizeof(ip_str)) == ESP_OK) {
                // Format response
                snprintf(response_buffer, sizeof(response_buffer), RESPONSE_FORMAT, hostname, ip_str);

                // Send response
                ssize_t send_len = sendto(sock, response_buffer, strlen(response_buffer), 0,
                                          (struct sockaddr *)&client_addr, client_addr_len);

                if (send_len < 0) {
                    ESP_LOGW(TAG, "Failed to send response: %d", errno);
                } else {
                    // Convert in_addr to string manually (IP2STR expects ip4_addr_t, not in_addr)
                    uint32_t addr = ntohl(client_addr.sin_addr.s_addr);
                    char client_ip[16];
                    snprintf(client_ip, sizeof(client_ip), "%u.%u.%u.%u",
                             (unsigned int)((addr >> 24) & 0xFF),
                             (unsigned int)((addr >> 16) & 0xFF),
                             (unsigned int)((addr >> 8) & 0xFF),
                             (unsigned int)(addr & 0xFF));
                    ESP_LOGI(TAG, "Responded to discovery from %s:%d", client_ip, ntohs(client_addr.sin_port));
                }
            } else {
                ESP_LOGW(TAG, "Failed to get IP address, skipping response");
            }
        }
    }

    // Cleanup
    close(sock);
    ESP_LOGI(TAG, "UDP discovery responder stopped");
    s_discovery_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t udp_discovery_start(void)
{
    if (s_discovery_mutex == NULL) {
        s_discovery_mutex = xSemaphoreCreateMutex();
        if (s_discovery_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_discovery_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_discovery_running) {
        xSemaphoreGive(s_discovery_mutex);
        ESP_LOGW(TAG, "UDP discovery responder already running");
        return ESP_OK;
    }

    s_discovery_running = true;

    // Create task
    BaseType_t ret = xTaskCreate(
        udp_discovery_task,
        "udp_discovery",
        4096,  // Stack size
        NULL,
        5,     // Priority
        &s_discovery_task_handle
    );

    xSemaphoreGive(s_discovery_mutex);

    if (ret != pdPASS) {
        s_discovery_running = false;
        ESP_LOGE(TAG, "Failed to create UDP discovery task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UDP discovery responder started");
    return ESP_OK;
}

esp_err_t udp_discovery_stop(void)
{
    if (s_discovery_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_discovery_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_discovery_running) {
        xSemaphoreGive(s_discovery_mutex);
        return ESP_OK;
    }

    s_discovery_running = false;
    xSemaphoreGive(s_discovery_mutex);

    // Wait for task to finish (with timeout)
    if (s_discovery_task_handle != NULL) {
        // Give task time to exit
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // If still running, delete it
        if (s_discovery_task_handle != NULL) {
            vTaskDelete(s_discovery_task_handle);
            s_discovery_task_handle = NULL;
        }
    }

    ESP_LOGI(TAG, "UDP discovery responder stopped");
    return ESP_OK;
}

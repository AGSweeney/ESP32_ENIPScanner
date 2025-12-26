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
 * @file main.c
 * @brief Main application entry point for ESP32-P4 EtherNet/IP device
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/netif.h"
#include "lwip/err.h"
#include "lwip/tcpip.h"
#include "lwip/ip4_addr.h"
#include "lwip/netifapi.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "system_config.h"
#include "enip_scanner.h"
#include "webui.h"

static const char *TAG = "main";
static struct netif *s_netif = NULL;
static SemaphoreHandle_t s_netif_mutex = NULL;
static bool s_services_initialized = false;
esp_eth_handle_t s_eth_handle = NULL;


// FreeRTOS idle hook (required if CONFIG_FREERTOS_USE_IDLE_HOOK is enabled)
void vApplicationIdleHook(void) {
    // Empty hook - can be used for low-power mode or other idle tasks
}


// Configure network interface based on settings from NVS
static void configure_netif(esp_netif_t *netif) {
    if (netif == NULL) {
        return;
    }
    
    system_ip_config_t config;
    bool loaded = system_ip_config_load(&config);
    
    if (!loaded) {
        // No saved configuration, use DHCP defaults
        system_ip_config_get_defaults(&config);
        ESP_LOGI(TAG, "Using default DHCP configuration");
    }
    
    if (config.use_dhcp) {
        // Configure for DHCP
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
        ESP_ERROR_CHECK(esp_netif_dhcpc_start(netif));
        ESP_LOGI(TAG, "Network configured for DHCP");
    } else {
        // Configure for static IP
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
        
        esp_netif_ip_info_t ip_info;
        
        // Set IP addresses directly from config (already in network byte order)
        ip_info.ip.addr = config.ip_address;
        ip_info.netmask.addr = config.netmask;
        ip_info.gw.addr = config.gateway;
        
        ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
        
        // Configure DNS servers if provided
        if (config.dns1 != 0) {
            ip4_addr_t dns1_addr;
            dns1_addr.addr = config.dns1;
            esp_netif_dns_info_t dns_info = {0};
            dns_info.ip.u_addr.ip4.addr = dns1_addr.addr;
            dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            esp_err_t ret = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to set DNS1: %s", esp_err_to_name(ret));
            }
        }
        
        if (config.dns2 != 0) {
            ip4_addr_t dns2_addr;
            dns2_addr.addr = config.dns2;
            esp_netif_dns_info_t dns_info = {0};
            dns_info.ip.u_addr.ip4.addr = dns2_addr.addr;
            dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            esp_err_t ret = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to set DNS2: %s", esp_err_to_name(ret));
            }
        }
        
        char ip_str[16], netmask_str[16], gw_str[16];
        ip4_addr_t ip_log, netmask_log, gw_log;
        ip_log.addr = ip_info.ip.addr;
        netmask_log.addr = ip_info.netmask.addr;
        gw_log.addr = ip_info.gw.addr;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_log));
        snprintf(netmask_str, sizeof(netmask_str), IPSTR, IP2STR(&netmask_log));
        snprintf(gw_str, sizeof(gw_str), IPSTR, IP2STR(&gw_log));
        ESP_LOGI(TAG, "Network configured with static IP: %s, Netmask: %s, Gateway: %s", 
                 ip_str, netmask_str, gw_str);
    }
}

static void ethernet_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
    esp_netif_t *eth_netif = (esp_netif_t *)arg;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
               mac_addr[0], mac_addr[1], mac_addr[2],
               mac_addr[3], mac_addr[4], mac_addr[5]);
        ESP_ERROR_CHECK(esp_netif_set_mac(eth_netif, mac_addr));
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        s_services_initialized = false;  // Allow re-initialization when link comes back up
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;
    
    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    
    // Create mutex on first call if needed
    if (s_netif_mutex == NULL) {
        s_netif_mutex = xSemaphoreCreateMutex();
        if (s_netif_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create netif mutex");
            return;
        }
    }
    
    // Take mutex to protect s_netif access
    if (xSemaphoreTake(s_netif_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take netif mutex");
        return;
    }
    
    if (s_netif == NULL) {
        for (struct netif *netif = netif_list; netif != NULL; netif = netif->next) {
            if (netif_is_up(netif) && netif_is_link_up(netif)) {
                s_netif = netif;
                break;
            }
        }
    }
    
    struct netif *netif_to_use = s_netif;
    xSemaphoreGive(s_netif_mutex);
    
    if (netif_to_use != NULL) {
        // Initialize services only once (IP_EVENT_ETH_GOT_IP can fire multiple times)
        // Check and set flag atomically to prevent race condition
        bool should_init = false;
        if (!s_services_initialized) {
            s_services_initialized = true;  // Set immediately to prevent double initialization
            should_init = true;
        }
        
        if (should_init) {
            // Initialize EtherNet/IP scanner
            esp_err_t scanner_ret = enip_scanner_init();
            if (scanner_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to initialize EtherNet/IP scanner: %s", esp_err_to_name(scanner_ret));
            }
            
            // Initialize Web UI (disable for testing connection close/reopen)
            // Set to 0 to disable web UI and test connection behavior in isolation
            #define ENABLE_WEBUI_FOR_TESTING 1
            #if ENABLE_WEBUI_FOR_TESTING
            esp_err_t webui_ret = webui_init();
            if (webui_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to initialize Web UI: %s", esp_err_to_name(webui_ret));
            }
            #else
            ESP_LOGI(TAG, "Web UI disabled for testing");
            #endif
            
            ESP_LOGI(TAG, "All services initialized");
        } else {
            ESP_LOGD(TAG, "Services already initialized, skipping re-initialization");
        }
    } else {
        ESP_LOGE(TAG, "Failed to find netif");
    }
}

void app_main(void)
{
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    ESP_ERROR_CHECK(esp_netif_set_default_netif(eth_netif));

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, 
                                               &ethernet_event_handler, eth_netif));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, 
                                               &got_ip_event_handler, eth_netif));

    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    
    // Waveshare ESP32-P4 Dev Kit Ethernet pin configuration
    phy_config.phy_addr = 1;  // Default PHY address
    phy_config.reset_gpio_num = 51;  // Power control pin for Waveshare ESP32-P4

    esp32_emac_config.smi_gpio.mdc_num = 31;  // MDC GPIO for Waveshare ESP32-P4
    esp32_emac_config.smi_gpio.mdio_num = 52;  // MDIO GPIO for Waveshare ESP32-P4
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;  // External clock mode
    esp32_emac_config.clock_config.rmii.clock_gpio = 50;  // REF_CLK GPIO for Waveshare ESP32-P4

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    s_eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &s_eth_handle));

    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(s_eth_handle);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, glue));

    configure_netif(eth_netif);
    
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));
}



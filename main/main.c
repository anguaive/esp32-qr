/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <string.h>
#include "nvs_flash.h"

#define WIFI_SSID "i40tk-student-uj"
#define WIFI_PSK "TKlab2022"
#define WIFI_RECONNECT_TIMEOUT 5000

static const char *TAG = "esp32-qr";

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connecting WiFi (to %s)", WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT
               && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected. Waiting.");
        system_event_sta_disconnected_t *event =
                (system_event_sta_disconnected_t *) event_data;
        ESP_LOGI(TAG, "Connection attempt to '%s' failed. Reason: %d", WIFI_SSID, event->reason);

        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_TIMEOUT));
        ESP_LOGI(TAG, "WiFi disconnected. Reconnecting.");
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            const char *ret_text = esp_err_to_name(ret);
            ESP_LOGI(TAG, "Result %s", ret_text);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Connected to WiFi with IP address: " IPSTR,
                 IP2STR(&event->ip_info.ip));
    }
}

void app_main(void) {
    esp_log_level_set("spi_master", ESP_LOG_INFO);
    esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_INFO);
    esp_log_level_set("sdmmc_common", ESP_LOG_INFO);
    esp_log_level_set("sdmmc_sd", ESP_LOG_INFO);
    esp_log_level_set("httpd_txrx", ESP_LOG_INFO);

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(
            esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {0};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    strcpy((char *) wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *) wifi_config.sta.password, WIFI_PSK);
    // We have to explicitly set to not check MAC address, otherwise sometimes cannot find AP
    wifi_config.sta.bssid_set = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    vTaskDelay(portMAX_DELAY);
}

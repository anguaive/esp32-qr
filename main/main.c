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
#include "esp_http_server.h"
#include "esp_camera.h"
#include "qr.h"

#define WIFI_SSID "i40tk-student-uj"
#define WIFI_PSK "TKlab2022"
#define WIFI_RECONNECT_TIMEOUT 5000

static const char *TAG = "esp32-qr";
static esp_ip4_addr_t ip;

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

static camera_config_t camera_config = {
        .pin_pwdn  = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sscb_sda = CAM_PIN_SIOD,
        .pin_sscb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
        .xclk_freq_hz = 10000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_GRAYSCALE,
        .frame_size = FRAMESIZE_QVGA,

        .jpeg_quality = 10, // 0-63 lower number means higher quality
        .fb_count = 1, // if more than one, i2s runs in continuous mode. Use only with JPEG
        .grab_mode = CAMERA_GRAB_LATEST
};

static esp_err_t wildcard_handler(httpd_req_t *req) {
    esp_err_t result = httpd_resp_send_404(req);
    return result;
}

static esp_err_t pgm_handler(httpd_req_t *req) {
    camera_fb_t *frame = esp_camera_fb_get();

    char *pgm_header;
    asprintf(&pgm_header, "P5 %d %d %d\n", frame->width, frame->height, 255);

    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"image.pgm\"");

    httpd_resp_send_chunk(req, pgm_header, strlen(pgm_header));
    httpd_resp_send_chunk(req, (char*)frame->buf, frame->len);
    httpd_resp_send_chunk(req, NULL, 0);

    free(pgm_header);

    esp_camera_fb_return(frame);

    return ESP_OK;
}

static const httpd_uri_t qr_stream = {
        .uri = "/qr_stream",
        .method = HTTP_GET,
        .handler = qr_stream_handler
};

static const httpd_uri_t qr = {
        .uri = "/qr",
        .method = HTTP_GET,
        .handler = qr_handler
};

static const httpd_uri_t preset_qr = {
        .uri = "/preset_qr",
        .method = HTTP_GET,
        .handler = preset_qr_handler
};

static const httpd_uri_t pgm = {
        .uri = "/pgm",
        .method = HTTP_GET,
        .handler = pgm_handler
};

static const httpd_uri_t preset_pgm = {
        .uri = "/preset_pgm",
        .method = HTTP_GET,
        .handler = preset_pgm_handler
};

static const httpd_uri_t wildcard = {
        .uri = "*",
        .method = HTTP_GET,
        .handler = wildcard_handler
};

static void httpd_register_uri_handler_with_msg(httpd_handle_t handle, const httpd_uri_t *uri, const char *msg) {
    httpd_register_uri_handler(handle, uri);
    ESP_LOGI(TAG, "Open http://"IPSTR"%s for %s", IP2STR(&ip), uri->uri, msg);
}

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
        ip = event->ip_info.ip;
        ESP_LOGI(TAG, "Connected to WiFi with IP address: " IPSTR,
                 IP2STR(&ip));

        httpd_handle_t httpd_handle;
        httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
        httpd_config.stack_size = 64000;
        httpd_config.uri_match_fn = httpd_uri_match_wildcard;

        if(httpd_start(&httpd_handle, &httpd_config) == ESP_OK) {
            httpd_register_uri_handler_with_msg(httpd_handle, &qr, "reading QR code from next frame");
            httpd_register_uri_handler_with_msg(httpd_handle, &preset_qr, "reading QR code from the preset PGM image");
            httpd_register_uri_handler_with_msg(httpd_handle, &qr_stream, "reading QR continuously until a successful read");
            httpd_register_uri_handler_with_msg(httpd_handle, &pgm, "a single PGM image");
            httpd_register_uri_handler_with_msg(httpd_handle, &preset_pgm, "the preset PGM image");
            httpd_register_uri_handler(httpd_handle, &wildcard);
        }
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

    ESP_ERROR_CHECK(esp_camera_init(&camera_config));
 }

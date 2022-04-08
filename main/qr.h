#ifndef ESP32_QR_QR_H
#define ESP32_QR_QR_H

#include "esp_http_server.h"

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t width;
    size_t height;
} pgm_t;

esp_err_t qr_handler(httpd_req_t *req);
esp_err_t qr_stream_handler(httpd_req_t *req);
esp_err_t preset_qr_handler(httpd_req_t *req);
esp_err_t preset_pgm_handler(httpd_req_t *req);

#endif //ESP32_QR_QR_H

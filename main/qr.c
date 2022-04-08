#include "qr.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "quirc.h"
#include "esp_log.h"

typedef void (*qr_handler_t)(void*, int, struct quirc_data*);

static const char *TAG = "QUIRC";
static const char *NO_QR_RESP = "Failed to locate QR\n";

extern const uint8_t image_pgm_start[]   asm("_binary_image_pgm_start");
extern const uint8_t image_pgm_end[]   asm("_binary_image_pgm_end");

static int parse_pgm(pgm_t *pgm, const uint8_t *image, size_t image_len) {
    char *header_end = strchr((char*)image, '\n');
    if(!header_end) return -1;

    size_t header_len = header_end - (char*)image;
    char header[header_len + 1];
    strncpy(header, (char*)image, header_len);
    header[header_len] = '\0';

    int n_scanned = sscanf(header, "P5 %zu %zu %*d", &pgm->width, &pgm->height);
    if (n_scanned != 2) return -1;

    pgm->buf = (uint8_t*)header_end + 1;
    pgm->len = image_len - (header_end - (char*)image) - 1;

    return 0;
}

static struct quirc *qr_init(uint8_t *buf, size_t buf_len, size_t w, size_t h, int *num_codes) {
    struct quirc *qr;
    uint8_t *image;

    ESP_LOGD(TAG, "Creating quirc object");
    qr = quirc_new();

    if(!qr) {
        ESP_LOGE(TAG, "Unable to allocate memory quirc object");
        return NULL;
    }

    ESP_LOGD(TAG, "Resizing quirc image");
    if (quirc_resize(qr, w, h)) {
        ESP_LOGE(TAG, "Unable to allocate memory for quirc image");
        quirc_destroy(qr);
        return NULL;
    }

    ESP_LOGD(TAG, "Beginning quirc processing");
    image = quirc_begin(qr, NULL, NULL);

    ESP_LOGD(TAG, "Copying frame to quirc image location");
    memcpy(image, buf, buf_len);

    quirc_end(qr);

    *num_codes = quirc_count(qr);

    return qr;
}

static int qr_read(struct quirc *qr, int code_idx, struct quirc_data *data) {
    struct quirc_code code;
    quirc_decode_error_t err;

    quirc_extract(qr, code_idx, &code);

    err = quirc_decode(&code, data);
    if (err) {
        ESP_LOGE(TAG, "QR decode failed: %s", quirc_strerror(err));
        return -1;
    }

    return 0;
}

static void qr_deinit(struct quirc *qr) {
    quirc_destroy(qr);
}

static void qr_on_success_httpd(void *arg, int idx, struct quirc_data *data) {
    httpd_req_t *req = (httpd_req_t*)arg;
    char *response;

    // NOTE(rg): skip first 3 bytes (byte order mark)
    asprintf(&response, "Content from QR (i: %d): '%.*s'",
             idx, data->payload_len-3, (char*)data->payload+3);

    ESP_LOGI(TAG, "%s", response);
    httpd_resp_send(req, response, strlen(response));

    free(response);
}

static void qr_on_failure_httpd(void *arg, int idx, struct quirc_data *data) {
    // NOOP
}

static int qr_read_all(httpd_req_t *req, struct quirc *qr, int num_codes,
        qr_handler_t on_success, qr_handler_t on_failure) {
    int success = 0;
    for (int i = 0; i < num_codes; i++) {
        struct quirc_data data;

        if (qr_read(qr, i, &data) == 0) {
            on_success(req, i, &data);
        } else {
            on_failure(req, i, &data);
            success = -1;
        }
    }

    return success;
}

esp_err_t qr_handler(httpd_req_t *req) {
    camera_fb_t *frame = esp_camera_fb_get();
    int num_codes = 0;

    httpd_resp_set_type(req, "text/plain");

    struct quirc *qr = qr_init(frame->buf, frame->len, frame->width, frame->height, &num_codes);

    esp_camera_fb_return(frame);

    if (!qr || num_codes == 0) {
        httpd_resp_send(req, NO_QR_RESP, strlen(NO_QR_RESP));
        qr_deinit(qr);
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Found %d QR code(s)", num_codes);

    qr_read_all(req, qr, num_codes,
                qr_on_success_httpd,
                qr_on_failure_httpd);

    qr_deinit(qr);
    return ESP_OK;
}

esp_err_t qr_stream_handler(httpd_req_t *req) {
    int frame = 0;

    httpd_resp_set_type(req, "text/plain");

    while (true) {
        ESP_LOGI(TAG, "Attempting to read QR from frame no. %d", ++frame);
        camera_fb_t *frame = esp_camera_fb_get();
        int num_codes = 0;

        struct quirc *qr = qr_init(frame->buf, frame->len, frame->width, frame->height, &num_codes);

        esp_camera_fb_return(frame);

        if (!qr || num_codes == 0) {
            ESP_LOGD(TAG, "Failed to locate QR. Continuing");
            qr_deinit(qr);
            continue;
        }

        ESP_LOGD(TAG, "Found %d QR code(s)", num_codes);

        if(qr_read_all(req, qr, num_codes,
                    qr_on_success_httpd,
                    qr_on_failure_httpd) != 0)
            continue;

        qr_deinit(qr);
        return ESP_OK;
    }
}

esp_err_t preset_qr_handler(httpd_req_t *req) {
    pgm_t pgm;
    int num_codes;

    httpd_resp_set_type(req, "text/plain");

    parse_pgm(&pgm, image_pgm_start, image_pgm_end - image_pgm_start);
    struct quirc *qr = qr_init(pgm.buf, pgm.len, pgm.width, pgm.height, &num_codes);

    if (!qr || num_codes == 0) {
        httpd_resp_send(req, NO_QR_RESP, strlen(NO_QR_RESP));
        return ESP_OK;
    }

    qr_read_all(req, qr, num_codes,
                qr_on_success_httpd,
                qr_on_failure_httpd);

    qr_deinit(qr);

    return ESP_OK;
}

esp_err_t preset_pgm_handler(httpd_req_t *req) {
    pgm_t pgm;
    parse_pgm(&pgm, image_pgm_start, image_pgm_end - image_pgm_start);

    char *pgm_header;
    asprintf(&pgm_header, "P5 %d %d %d\n", pgm.width, pgm.height, 255);

    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"image.pgm\"");

    httpd_resp_send_chunk(req, pgm_header, strlen(pgm_header));
    httpd_resp_send_chunk(req, (char*)pgm.buf, pgm.len);
    httpd_resp_send_chunk(req, NULL, 0);

    free(pgm_header);

    return ESP_OK;
}

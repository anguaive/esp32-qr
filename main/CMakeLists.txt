idf_component_register(SRCS "main.c" "qr.c"
        INCLUDE_DIRS ""
        PRIV_REQUIRES "nvs_flash"
        REQUIRES "esp_http_server" "esp32-camera" "quirc")

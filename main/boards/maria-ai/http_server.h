#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_http_server.h"

// Pornește serverul HTTP
httpd_handle_t start_webserver(void);

// Oprește serverul HTTP
void stop_webserver(httpd_handle_t server);

// Înregistrează handler-ul pentru /
esp_err_t register_index_handler(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
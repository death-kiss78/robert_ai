#pragma once
#include <esp_err.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t api_sd_list(httpd_req_t* req);
esp_err_t api_sd_delete(httpd_req_t* req);
esp_err_t api_sd_mkdir(httpd_req_t* req);
esp_err_t api_sd_rename(httpd_req_t* req);
esp_err_t api_sd_move(httpd_req_t* req);
esp_err_t api_sd_download(httpd_req_t* req);
esp_err_t api_sd_upload(httpd_req_t* req);
esp_err_t api_sd_reset(httpd_req_t* req);

void sdcard_register_routes(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

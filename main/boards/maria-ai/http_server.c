// http_server.c

#include "http_server.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "filemanager.h"
// #include "chat.h"

static const char* TAG = "http_server";
static const char* base_path = "/sdcard";

/******************************************************************
 *  MIME TYPE DETECTION
 ******************************************************************/
static const char* get_mime_type(const char* filepath) {
    const char* ext = strrchr(filepath, '.');
    if (!ext)
        return "text/plain";
    ext++;

    if (!strcasecmp(ext, "html"))
        return "text/html; charset=utf-8";
    if (!strcasecmp(ext, "htm"))
        return "text/html; charset=utf-8";
    if (!strcasecmp(ext, "css"))
        return "text/css";
    if (!strcasecmp(ext, "js"))
        return "application/javascript";
    if (!strcasecmp(ext, "json"))
        return "application/json";
    if (!strcasecmp(ext, "png"))
        return "image/png";
    if (!strcasecmp(ext, "jpg"))
        return "image/jpeg";
    if (!strcasecmp(ext, "jpeg"))
        return "image/jpeg";
    if (!strcasecmp(ext, "gif"))
        return "image/gif";
    if (!strcasecmp(ext, "svg"))
        return "image/svg+xml";
    if (!strcasecmp(ext, "ico"))
        return "image/x-icon";
    if (!strcasecmp(ext, "txt"))
        return "text/plain";
    if (!strcasecmp(ext, "xml"))
        return "application/xml";

    return "application/octet-stream";
}

/******************************************************************
 *  HANDLER: /
 ******************************************************************/
static esp_err_t index_handler(httpd_req_t* req) {
    char filepath[512];

    strlcpy(filepath, base_path, sizeof(filepath));
    strlcat(filepath, "/index.html", sizeof(filepath));

    ESP_LOGI(TAG, "Serving index: %s", filepath);

    FILE* fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "index.html not found at %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_mime_type(filepath));

    char buffer[1024];
    size_t read_bytes;

    while ((read_bytes = fread(buffer, 1, sizeof(buffer), fd)) > 0) {
        httpd_resp_send_chunk(req, buffer, read_bytes);
    }

    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/******************************************************************
 *  HANDLER: wildcard
 ******************************************************************/
static esp_err_t static_file_handler(httpd_req_t* req) {
    if (strcmp(req->uri, "/") == 0) {
        return index_handler(req);
    }

    char filepath[512];

    strlcpy(filepath, base_path, sizeof(filepath));
    strlcat(filepath, req->uri, sizeof(filepath));

    ESP_LOGI(TAG, "Serving file: %s", filepath);

    FILE* fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGW(TAG, "File not found: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_mime_type(filepath));

    char buffer[1024];
    size_t read_bytes;

    while ((read_bytes = fread(buffer, 1, sizeof(buffer), fd)) > 0) {
        httpd_resp_send_chunk(req, buffer, read_bytes);
    }

    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/******************************************************************
 *  URI definitions
 ******************************************************************/
static const httpd_uri_t index_uri = {
    .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL};

static const httpd_uri_t static_uri = {
    .uri = "/*", .method = HTTP_GET, .handler = static_file_handler, .user_ctx = NULL};

/******************************************************************
 *  Start server
 ******************************************************************/
httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;

    // IMPORTANT: increase HTTPD task stack size
    config.stack_size = 16384;

    ESP_LOGI(TAG, "Starting HTTP server...");

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering handlers...");
        httpd_register_uri_handler(server, &index_uri);
        sdcard_register_routes(server);                   // API SD routes
                                                          //		chat_register_routes(server);
        httpd_register_uri_handler(server, &static_uri);  // static files

        return server;
    }

    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

/******************************************************************
 *  Stop server
 ******************************************************************/
void stop_webserver(httpd_handle_t server) {
    if (server) {
        ESP_LOGI(TAG, "Stopping HTTP server...");
        httpd_stop(server);
    }
}

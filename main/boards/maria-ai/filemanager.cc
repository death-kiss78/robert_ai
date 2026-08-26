// filemanager.c

#include "filemanager.h"

extern "C" {
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
}

#define FM_BUF 8192

static void url_decode(const char* src, char* dst, size_t dstSize) {
    size_t si = 0, di = 0;
    while (src[si] && di + 1 < dstSize) {
        if (src[si] == '%' && src[si + 1] && src[si + 2]) {
            char hex[3] = {src[si + 1], src[si + 2], 0};
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 3;
        } else if (src[si] == '+') {
            dst[di++] = ' ';
            si++;
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = 0;
}

static bool fs_delete_recursive(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return false;

    if (!S_ISDIR(st.st_mode))
        return unlink(path) == 0;

    DIR* dir = opendir(path);
    if (!dir)
        return false;

    struct dirent* ent;
    static char child[FM_BUF];

    while ((ent = readdir(dir)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;

        strlcpy(child, path, sizeof(child));
        strlcat(child, "/", sizeof(child));
        strlcat(child, ent->d_name, sizeof(child));

        struct stat st2;
        if (stat(child, &st2) != 0)
            continue;

        if (S_ISDIR(st2.st_mode))
            fs_delete_recursive(child);
        else
            unlink(child);
    }

    closedir(dir);
    return rmdir(path) == 0;
}

esp_err_t api_sd_list(httpd_req_t* req) {
    char query[256];
    char relPath[256] = "/";
    char decoded[256];
    static char base[FM_BUF];

    strlcpy(base, "/sdcard", sizeof(base));

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "path", relPath, sizeof(relPath)) == ESP_OK) {
            url_decode(relPath, decoded, sizeof(decoded));
            strlcpy(relPath, decoded, sizeof(relPath));
        }
    }

    if (strcmp(relPath, "/") != 0)
        strlcat(base, relPath, sizeof(base));

    DIR* dir = opendir(base);
    if (!dir) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "[]");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "[");

    struct dirent* ent;
    bool first = true;

    static char full[FM_BUF];
    static char json[FM_BUF];

    while ((ent = readdir(dir)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;

        strlcpy(full, base, sizeof(full));
        strlcat(full, "/", sizeof(full));
        strlcat(full, ent->d_name, sizeof(full));

        struct stat st;
        if (stat(full, &st) != 0)
            continue;

        if (!first)
            httpd_resp_sendstr_chunk(req, ",");
        first = false;

        strlcpy(json, "{\"name\":\"", sizeof(json));
        strlcat(json, ent->d_name, sizeof(json));
        strlcat(json, "\",\"type\":\"", sizeof(json));

        if (S_ISDIR(st.st_mode)) {
            strlcat(json, "dir\"}", sizeof(json));
        } else {
            char sizeBuf[32];
            sprintf(sizeBuf, "%lu", (unsigned long)st.st_size);

            strlcat(json, "file\",\"size\":", sizeof(json));
            strlcat(json, sizeBuf, sizeof(json));
            strlcat(json, "}", sizeof(json));
        }

        httpd_resp_sendstr_chunk(req, json);
    }

    closedir(dir);
    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_sendstr_chunk(req, NULL);
}

esp_err_t api_sd_delete(httpd_req_t* req) {
    char query[256];
    char relPath[256];
    char decoded[256];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "path", relPath, sizeof(relPath)) != ESP_OK)
        return httpd_resp_sendstr(req, "Missing path");

    url_decode(relPath, decoded, sizeof(decoded));

    static char full[FM_BUF];
    strlcpy(full, "/sdcard", sizeof(full));
    strlcat(full, decoded, sizeof(full));

    bool ok = fs_delete_recursive(full);
    return httpd_resp_sendstr(req, ok ? "OK" : "FAIL");
}

esp_err_t api_sd_mkdir(httpd_req_t* req) {
    char query[256];
    char relPath[256];
    char decoded[256];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "path", relPath, sizeof(relPath)) != ESP_OK)
        return httpd_resp_sendstr(req, "Missing path");

    url_decode(relPath, decoded, sizeof(decoded));

    static char full[FM_BUF];
    strlcpy(full, "/sdcard", sizeof(full));
    strlcat(full, decoded, sizeof(full));

    int rc = mkdir(full, 0755);
    return httpd_resp_sendstr(req, (rc == 0) ? "OK" : "FAIL");
}

esp_err_t api_sd_rename(httpd_req_t* req) {
    char query[256];
    char oldRel[256], newRel[256];
    char oldDec[256], newDec[256];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
        return httpd_resp_sendstr(req, "Missing args");

    if (httpd_query_key_value(query, "old", oldRel, sizeof(oldRel)) != ESP_OK ||
        httpd_query_key_value(query, "new", newRel, sizeof(newRel)) != ESP_OK)
        return httpd_resp_sendstr(req, "Missing args");

    url_decode(oldRel, oldDec, sizeof(oldDec));
    url_decode(newRel, newDec, sizeof(newDec));

    static char oldFull[FM_BUF];
    static char newFull[FM_BUF];

    strlcpy(oldFull, "/sdcard", sizeof(oldFull));
    strlcat(oldFull, oldDec, sizeof(oldFull));

    strlcpy(newFull, "/sdcard", sizeof(newFull));
    strlcat(newFull, newDec, sizeof(newFull));

    int rc = rename(oldFull, newFull);
    return httpd_resp_sendstr(req, (rc == 0) ? "OK" : "FAIL");
}

esp_err_t api_sd_move(httpd_req_t* req) { return api_sd_rename(req); }

esp_err_t api_sd_download(httpd_req_t* req) {
    char query[256];
    char relPath[256];
    char decoded[256];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "file", relPath, sizeof(relPath)) != ESP_OK)
        return httpd_resp_sendstr(req, "Missing file");

    url_decode(relPath, decoded, sizeof(decoded));

    static char full[FM_BUF];
    strlcpy(full, "/sdcard", sizeof(full));
    strlcat(full, decoded, sizeof(full));

    FILE* f = fopen(full, "rb");
    if (!f)
        return httpd_resp_sendstr(req, "File not found");

    const char* fname = strrchr(decoded, '/');
    fname = fname ? fname + 1 : decoded;

    char disp[256] = "attachment; filename=\"";
    strlcat(disp, fname, sizeof(disp));
    strlcat(disp, "\"", sizeof(disp));

    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    httpd_resp_set_type(req, "application/octet-stream");

    char buf[1024];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t api_sd_upload(httpd_req_t* req) {
    char query[256];
    char relPath[256];
    char name[256];
    char decodedPath[256];
    char decodedName[256];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
        return httpd_resp_sendstr(req, "Missing args");

    if (httpd_query_key_value(query, "path", relPath, sizeof(relPath)) != ESP_OK ||
        httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK)
        return httpd_resp_sendstr(req, "Missing args");

    url_decode(relPath, decodedPath, sizeof(decodedPath));
    url_decode(name, decodedName, sizeof(decodedName));

    static char full[FM_BUF];
    strlcpy(full, "/sdcard", sizeof(full));
    strlcat(full, decodedPath, sizeof(full));
    strlcat(full, "/", sizeof(full));
    strlcat(full, decodedName, sizeof(full));

    FILE* f = fopen(full, "wb");
    if (!f)
        return httpd_resp_sendstr(req, "ERR");

    char buf[1024];
    int received;

    while ((received = httpd_req_recv(req, buf, sizeof(buf))) > 0)
        fwrite(buf, 1, received, f);

    fclose(f);
    return httpd_resp_sendstr(req, "OK");
}

esp_err_t api_sd_reset(httpd_req_t* req) {
    httpd_resp_sendstr(req, "OK");
    esp_restart();
    return ESP_OK;
}

static const httpd_uri_t sd_list_uri = {"/sd/list", HTTP_GET, api_sd_list, NULL};
static const httpd_uri_t sd_delete_uri = {"/sd/delete", HTTP_GET, api_sd_delete, NULL};
static const httpd_uri_t sd_mkdir_uri = {"/sd/mkdir", HTTP_GET, api_sd_mkdir, NULL};
static const httpd_uri_t sd_rename_uri = {"/sd/rename", HTTP_GET, api_sd_rename, NULL};
static const httpd_uri_t sd_move_uri = {"/sd/move", HTTP_GET, api_sd_move, NULL};
static const httpd_uri_t sd_download_uri = {"/sd/download", HTTP_GET, api_sd_download, NULL};
static const httpd_uri_t sd_upload_uri = {"/sd/upload", HTTP_POST, api_sd_upload, NULL};
static const httpd_uri_t sd_reset_uri = {"/sd/reset", HTTP_GET, api_sd_reset, NULL};

void sdcard_register_routes(httpd_handle_t server) {
    httpd_register_uri_handler(server, &sd_list_uri);
    httpd_register_uri_handler(server, &sd_delete_uri);
    httpd_register_uri_handler(server, &sd_mkdir_uri);
    httpd_register_uri_handler(server, &sd_rename_uri);
    httpd_register_uri_handler(server, &sd_move_uri);
    httpd_register_uri_handler(server, &sd_download_uri);
    httpd_register_uri_handler(server, &sd_upload_uri);
    httpd_register_uri_handler(server, &sd_reset_uri);
}

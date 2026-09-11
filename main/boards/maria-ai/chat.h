#ifndef _CHAT_H_
#define _CHAT_H_

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void chat_register_routes(httpd_handle_t server);
void chat_append_message(const char* msg);

#ifdef __cplusplus
}
#endif

#endif

#include "chat.h"
#include "esp_http_server.h"
#include "application.h"
#include "protocol.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string>

static const char *TAG = "chat_api";

// ultimul răspuns AI
static std::string last_ai_reply;

// interceptăm răspunsurile AI din Application
static void ChatMessageCallback(const char* role, const std::string& text) {
    if (role && strcmp(role, "assistant") == 0) {
        last_ai_reply = text;
    }
}

// handler pentru POST /chat
static esp_err_t chat_handler(httpd_req_t *req)
{
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf));
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }
    buf[len] = 0;

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *msg = cJSON_GetObjectItem(root, "text");
    if (!cJSON_IsString(msg)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing text");
        return ESP_FAIL;
    }

    std::string text = msg->valuestring;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "HTTP chat received: %s", text.c_str());

    auto& app = Application::GetInstance();
    Protocol* protocol = app.GetProtocol();

    // 🔥 IMPORTANT: trebuie să existe o sesiune audio deschisă
    if (!protocol->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Audio channel not open, opening now...");
        protocol->OpenAudioChannel();
    }

    // 🔥 Trimitem textul ca listen/detect/text
    protocol->SendListenDetect(text);

    // răspunsul AI este capturat în last_ai_reply prin callback

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "reply", last_ai_reply.c_str());
    char *out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));
    free(out);

    return ESP_OK;
}

// înregistrăm ruta /chat
void chat_register_routes(httpd_handle_t server)
{
    Application::GetInstance().RegisterChatMessageCallback(ChatMessageCallback);

    static const httpd_uri_t chat_uri = {
        .uri = "/chat",
        .method = HTTP_POST,
        .handler = chat_handler,
        .user_ctx = NULL
    };

    ESP_LOGI(TAG, "Registering HTTP chat route");
    httpd_register_uri_handler(server, &chat_uri);
}

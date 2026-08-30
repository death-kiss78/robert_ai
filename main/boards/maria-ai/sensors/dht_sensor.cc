#include "dht_sensor.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include "board.h"
#include "display/lcd_display.h"

static const char* TAG = "DHT_SENSOR";

void DhtSensor::BackgroundTask(void* arg) {
    DhtSensor* self = static_cast<DhtSensor*>(arg);

    while (true) {

        if (!self->enabled_) {
            vTaskDelay(pdMS_TO_TICKS(500));   // nu citim dacă nu e permis
            continue;
        }

        bool ok = self->dht_.ReadData(3);

        if (ok) {
            int temp = self->dht_.GetTemperature();
            int hum = self->dht_.GetHumidity();

            ESP_LOGI(TAG, "Periodic reading -> Temp: %d°C, Humidity: %d%%", temp, hum);

            char msg[64];
            snprintf(msg, sizeof(msg), "Temp: %d°C  Hum: %d%%", temp, hum);

            Board* board = &Board::GetInstance();
            Display* display = board->GetDisplay();

            if (display) {
                display->ShowNotification(msg);
            }

        } else {
            ESP_LOGW(TAG, "Periodic reading failed");
        }

        vTaskDelay(pdMS_TO_TICKS(60000));  // 1 minut
    }
}

DhtSensor::DhtSensor(gpio_num_t pin)
    : dht_(pin)
{
    auto& mcp = McpServer::GetInstance();

    // TOOL PRINCIPAL
    mcp.AddTool("self.sensor.dht.read",
                "Read temperature and humidity from DHT sensor",
                PropertyList(),
                [this](const PropertyList&) -> ReturnValue {

                    bool ok = dht_.ReadData(3);

                    if (!ok) {
                        if (dht_.IsDataFresh(30000)) {
                            char buf[128];
                            snprintf(buf, sizeof(buf),
                                     "{\"cached\": true, \"age_ms\": %lu, "
                                     "\"temperature\": %d, \"humidity\": %d}",
                                     (unsigned long)dht_.GetDataFreshness(),
                                     dht_.GetTemperature(),
                                     dht_.GetHumidity());
                            return buf;
                        }

                        return "{\"error\": \"Failed to read DHT sensor\"}";
                    }

                    char buf[64];
                    snprintf(buf, sizeof(buf),
                             "{\"temperature\": %d, \"humidity\": %d}",
                             dht_.GetTemperature(),
                             dht_.GetHumidity());

                    return buf;
                });

    // TOOL DE TEST
    mcp.AddTool("self.sensor.dht.test",
                "Test if the DHT sensor is working properly",
                PropertyList(),
                [this](const PropertyList&) -> ReturnValue {

                    ESP_LOGI(TAG, "Testing DHT sensor...");

                    bool ok = dht_.ReadData(3);

                    if (ok) {
                        int temp = dht_.GetTemperature();
                        int hum  = dht_.GetHumidity();

                        ESP_LOGI(TAG,
                                 "DHT test success! Temp: %d°C, Humidity: %d%%",
                                 temp, hum);

                        char buf[128];
                        snprintf(buf, sizeof(buf),
                                 "{\"ok\": true, \"temperature\": %d, \"humidity\": %d}",
                                 temp, hum);
                        return buf;
                    }

                    ESP_LOGE(TAG, "DHT test failed!");

                    return "{\"ok\": false, \"error\": \"Sensor test failed. Check wiring and GPIO.\"}";
                });

    // NU PORNIM TASKUL AICI
    // Task-ul se pornește din MariaAi, o singură dată.
}

void DhtSensor::Start() {
    enabled_ = true;
}

void DhtSensor::Stop() {
    enabled_ = false;
}

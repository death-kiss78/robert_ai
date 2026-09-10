#include "dht_sensor.h"
#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_timer.h>

static const char* TAG = "DHT_SENSOR";

static inline void dht_delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

DhtSensor::DhtSensor(gpio_num_t pin)
    : pin_(pin)
{
    // Configurare pin DHT22
    gpio_config_t cfg = {};
    cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    cfg.pin_bit_mask = (1ULL << pin_);
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&cfg);

    ESP_LOGI(TAG, "DHT22 sensor initialized on GPIO %d", pin);

    // EXACT ca LampController: tool MCP în constructor
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.sensor.dht.read", "Read DHT22 sensor", PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            float t, h;
            if (!Read(t, h)) {
                return "{\"error\": true}";
            }
            char buf[64];
            snprintf(buf, sizeof(buf),
                "{\"temperature\": %.1f, \"humidity\": %.1f}",
                t, h);
            return buf;
        }
    );
}

bool DhtSensor::Read(float& temperature, float& humidity)
{
    uint8_t data[5] = {0};

    // START SIGNAL (DHT22)
    gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(pin_, 0);
    dht_delay_us(1000);   // 1ms LOW
    gpio_set_level(pin_, 1);
    dht_delay_us(30);

    gpio_set_direction(pin_, GPIO_MODE_INPUT);

    int timeout = 0;

    // WAIT FOR RESPONSE
    while (gpio_get_level(pin_) == 1) {
        if (++timeout > 200) return false;
        dht_delay_us(1);
    }

    timeout = 0;
    while (gpio_get_level(pin_) == 0) {
        if (++timeout > 200) return false;
        dht_delay_us(1);
    }

    timeout = 0;
    while (gpio_get_level(pin_) == 1) {
        if (++timeout > 200) return false;
        dht_delay_us(1);
    }

    // READ 40 BITS
    for (int i = 0; i < 40; i++) {

        timeout = 0;
        while (gpio_get_level(pin_) == 0) {
            if (++timeout > 200) return false;
            dht_delay_us(1);
        }

        int high_time = 0;
        while (gpio_get_level(pin_) == 1) {
            high_time++;
            dht_delay_us(1);
            if (high_time > 200) break;
        }

        data[i / 8] <<= 1;
        if (high_time > 40)
            data[i / 8] |= 1;
    }

    // CHECKSUM
    uint8_t sum = data[0] + data[1] + data[2] + data[3];
    if (sum != data[4]) {
        ESP_LOGW(TAG, "Checksum error: %d != %d", sum, data[4]);
        return false;
    }

    // DHT22 format:
    uint16_t raw_h = (data[0] << 8) | data[1];
    uint16_t raw_t = (data[2] << 8) | data[3];

    humidity = raw_h / 10.0f;

    if (raw_t & 0x8000) {
        raw_t &= 0x7FFF;
        temperature = -(raw_t / 10.0f);
    } else {
        temperature = raw_t / 10.0f;
    }

    ESP_LOGI(TAG, "DHT22 read OK: Temp=%.1f°C Hum=%.1f%%", temperature, humidity);
    return true;
}

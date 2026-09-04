#include "led_rainbow.h"
#include "application.h"
#include <esp_log.h>

#define TAG "SingleLedRainbow"

#define DEFAULT_BRIGHTNESS 10
#define HIGH_BRIGHTNESS 25
#define LOW_BRIGHTNESS 2

#define BLINK_INFINITE -1

// HSV → RGB
static void hsvToRgb(uint16_t h, uint8_t s, uint8_t v,
                     uint8_t &r, uint8_t &g, uint8_t &b)
{
    float hh = h / 60.0f;
    int i = (int)hh;
    float ff = hh - i;
    float p = v * (1.0f - s / 255.0f);
    float q = v * (1.0f - (s / 255.0f) * ff);
    float t = v * (1.0f - (s / 255.0f) * (1.0f - ff));

    switch (i) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

SingleLedRainbow::SingleLedRainbow(gpio_num_t gpio) {
    if (gpio == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "SingleLedRainbow initialized with GPIO_NUM_NC, LED will not function");
        return;
    }

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio;
    strip_config.max_leds = 1;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.led_model = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000;

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_));
    led_strip_clear(led_strip_);

    esp_timer_create_args_t blink_timer_args = {
        .callback = [](void *arg) {
            auto led = static_cast<SingleLedRainbow*>(arg);
            led->OnBlinkTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "blink_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&blink_timer_args, &blink_timer_));
}

SingleLedRainbow::~SingleLedRainbow() {
    if (blink_timer_ != nullptr) {
        esp_timer_stop(blink_timer_);
    }
    if (led_strip_ != nullptr) {
        led_strip_del(led_strip_);
    }
}

void SingleLedRainbow::SetColor(uint8_t r, uint8_t g, uint8_t b) {
    r_ = r;
    g_ = g;
    b_ = b;
}

void SingleLedRainbow::TurnOn() {
    if (led_strip_ == nullptr) return;

    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);
    led_strip_set_pixel(led_strip_, 0, r_, g_, b_);
    led_strip_refresh(led_strip_);
}

void SingleLedRainbow::TurnOff() {
    if (led_strip_ == nullptr) return;

    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);
    led_strip_clear(led_strip_);
}

void SingleLedRainbow::BlinkOnce() {
    Blink(1, 100);
}

void SingleLedRainbow::Blink(int times, int interval_ms) {
    StartBlinkTask(times, interval_ms);
}

void SingleLedRainbow::StartContinuousBlink(int interval_ms) {
    StartBlinkTask(BLINK_INFINITE, interval_ms);
}

void SingleLedRainbow::StartBlinkTask(int times, int interval_ms) {
    if (led_strip_ == nullptr) return;

    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);

    blink_counter_ = times * 2;
    blink_interval_ms_ = interval_ms;
    esp_timer_start_periodic(blink_timer_, interval_ms * 1000);
}

void SingleLedRainbow::StartRainbow() {
    if (led_strip_ == nullptr) return;

    std::lock_guard<std::mutex> lock(mutex_);
    esp_timer_stop(blink_timer_);

    blink_counter_ = BLINK_INFINITE;
    blink_interval_ms_ = 50;

    esp_timer_start_periodic(blink_timer_, blink_interval_ms_ * 1000);
}

void SingleLedRainbow::OnBlinkTimer() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (blink_counter_ == BLINK_INFINITE) {
        uint8_t r, g, b;
        hsvToRgb(rainbow_hue_, 255, HIGH_BRIGHTNESS, r, g, b);
        rainbow_hue_ = (rainbow_hue_ + 3) % 360;

        led_strip_set_pixel(led_strip_, 0, r, g, b);
        led_strip_refresh(led_strip_);
        return;
    }

    blink_counter_--;
    if (blink_counter_ & 1) {
        led_strip_set_pixel(led_strip_, 0, r_, g_, b_);
        led_strip_refresh(led_strip_);
    } else {
        led_strip_clear(led_strip_);
        if (blink_counter_ == 0) {
            esp_timer_stop(blink_timer_);
        }
    }
}

void SingleLedRainbow::OnStateChanged() {
    auto& app = Application::GetInstance();
    auto device_state = app.GetDeviceState();

    switch (device_state) {
        case kDeviceStateStarting:
            SetColor(0, 0, DEFAULT_BRIGHTNESS);
            StartContinuousBlink(100);
            break;

        case kDeviceStateWifiConfiguring:
            SetColor(0, 0, DEFAULT_BRIGHTNESS);
            StartContinuousBlink(500);
            break;

        case kDeviceStateIdle:
            StartRainbow();
            break;

        case kDeviceStateConnecting:
            SetColor(HIGH_BRIGHTNESS, 0, 0);
            TurnOn();
            break;

        case kDeviceStateListening:
        case kDeviceStateAudioTesting:
            SetColor(0, 0, HIGH_BRIGHTNESS);
            TurnOn();
            break;

        case kDeviceStateSpeaking:
            SetColor(0, HIGH_BRIGHTNESS, 0);
            TurnOn();
            break;

        case kDeviceStateUpgrading:
            SetColor(0, DEFAULT_BRIGHTNESS, 0);
            StartContinuousBlink(100);
            break;

        case kDeviceStateActivating:
            SetColor(0, DEFAULT_BRIGHTNESS, 0);
            StartContinuousBlink(500);
            break;

        default:
            ESP_LOGW(TAG, "Unknown led strip event: %d", device_state);
            return;
    }
}

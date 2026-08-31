// 🔥 Cristian, aici este toată fila completă, cu patch-ul integrat.
// 🔥 Nu am modificat nimic altceva în afară de blocul DHT.

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#include <wifi_station.h>
#include "adc_battery_monitor.h"
#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/es8311_audio_codec.h"
#include "config.h"
#include "dht_sensor.h"
#include "display/lcd_display.h"
#include "http_server.h"
#include "lamp_controller.h"
#include "mcp_server.h"
#include "sd_card.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_manager.h>

#include "esp_lcd_ili9341.h"
#include "led_rainbow.h"
#include "system_reset.h"

#define TAG "MariaAi"

class TouchDriver {
public:
    TouchDriver() : dev_(nullptr) {}

    bool Init(i2c_master_bus_handle_t bus, uint8_t addr) {
        i2c_device_config_t cfg = {
            .device_address = addr,
            .scl_speed_hz = 400000,
            .scl_wait_us = 0,
        };
        return i2c_master_bus_add_device(bus, &cfg, &dev_) == ESP_OK;
    }

    bool Read(bool& touched, uint16_t& x, uint16_t& y) {
        touched = false;
        x = y = 0;
        if (!dev_)
            return false;

        uint8_t reg = 0x02;
        uint8_t buf[5];
        if (i2c_master_transmit_receive(dev_, &reg, 1, buf, 5, 50) != ESP_OK)
            return false;

        uint8_t points = buf[0] & 0x0F;
        if (points == 0)
            return true;

        touched = true;
        x = ((buf[1] & 0x0F) << 8) | buf[2];
        y = ((buf[3] & 0x0F) << 8) | buf[4];
        return true;
    }

private:
    i2c_master_dev_handle_t dev_;
};

static i2c_master_dev_handle_t pcf_dev = nullptr;

class MariaAi : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Button backlight_up_button_;
    Button backlight_down_button_;
    std::function<void()> cb_volume_up_;
    std::function<void()> cb_volume_down_;
    std::function<void()> cb_backlight_up_;
    std::function<void()> cb_backlight_down_;
    std::function<void()> cb_boot_;
    LcdDisplay* display_;
    i2c_master_bus_handle_t codec_i2c_bus_;
    TouchDriver touch_;
    AdcBatteryMonitor* adc_battery_monitor_;

    DhtSensor dht_{DHT11_PIN};

    bool web_server_started_ = false;

    static void WebServerTask(void* param) {
        auto* board = static_cast<MariaAi*>(param);
        auto& wifi = WifiManager::GetInstance();

        ESP_LOGI(TAG, "WebServerTask started, waiting for WiFi...");

        while (true) {
            bool config = wifi.IsConfigMode();
            bool connected = wifi.IsConnected();

            ESP_LOGI(TAG, "WebServerTask: config=%d, connected=%d", (int)config, (int)connected);

            if (!config && connected) {
                ESP_LOGI(TAG, "Conditions met. Starting WebServer");

                if (board->GetDisplay()) {
                    board->GetDisplay()->ShowNotification("Webserver started");
                }

                start_webserver();
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        vTaskDelete(nullptr);
    }

    static void PcfButtonTask(void* arg) {
        auto* self = static_cast<MariaAi*>(arg);

        uint8_t last_state = 0xFF;

        while (true) {
            uint8_t state = 0xFF;

            if (i2c_master_receive(pcf_dev, &state, 1, 50) == ESP_OK) {
                uint8_t changed = last_state ^ state;
                uint8_t pressed = changed & (~state);

                if (pressed & (1 << PCF_BTN_UP)) self->cb_volume_up_();
                if (pressed & (1 << PCF_BTN_DOWN)) self->cb_volume_down_();
                if (pressed & (1 << PCF_BTN_RIGHT)) self->cb_backlight_up_();
                if (pressed & (1 << PCF_BTN_LEFT)) self->cb_backlight_down_();
                if (pressed & (1 << PCF_BTN_MIDDLE)) self->cb_boot_();

                if (pressed & (1 << PCF_BTN_RST)) {
                    self->GetDisplay()->ShowNotification("Restart...");
                    esp_restart();
                }

                last_state = state;
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void InitializeBatteryMonitor() {
        adc_battery_monitor_ =
            new AdcBatteryMonitor(ADC_UNIT_1, ADC_CHANNEL_8, 200000, 200000, GPIO_NUM_9);
    }

    static void TouchTask(void* arg) {
        auto* self = static_cast<MariaAi*>(arg);
        auto& app = Application::GetInstance();

        uint32_t last_tap = 0;
        uint32_t down_start = 0;
        bool down = false;
        uint16_t last_y = 0;
        bool is_sliding = false;
        int slide_mode = 0;

        while (true) {
            bool t;
            uint16_t x, y;
            if (self->touch_.Read(t, x, y)) {
                uint32_t now = esp_timer_get_time() / 1000;

                if (t) {
                    if (!down) {
                        down = true;
                        down_start = now;
                        last_y = y;
                        is_sliding = false;

                        if (x < 60) slide_mode = 1;
                        else if (x > 180) slide_mode = 2;
                        else slide_mode = 0;

                    } else {
                        int dy = (int)y - (int)last_y;
                        if (std::abs(dy) > 10) {
                            if (slide_mode == 1) {
                                is_sliding = true;
                                int b = self->GetBacklight()->brightness();
                                b -= dy / 5;
                                if (b < 1) b = 1;
                                if (b > 100) b = 100;
                                self->GetBacklight()->SetBrightness(b);

                                char msg[32];
                                snprintf(msg, sizeof(msg), "Luminozitate: %d%%", b);
                                self->GetDisplay()->ShowNotification(msg);

                            } else if (slide_mode == 2) {
                                is_sliding = true;
                                auto codec = self->GetAudioCodec();
                                int v = codec->output_volume();
                                v -= dy / 5;
                                if (v < 0) v = 0;
                                if (v > 100) v = 100;
                                codec->SetOutputVolume(v);

                                char msg[32];
                                snprintf(msg, sizeof(msg), "Volum: %d%%", v);
                                self->GetDisplay()->ShowNotification(msg);
                            }
                            last_y = y;
                        }
                    }
                }

                if (!t && down) {
                    down = false;
                    if (!is_sliding) {
                        uint32_t press = now - down_start;
                        if (press > 3000) {
                            self->EnterWifiConfigMode();
                        } else if (now - last_tap < 250) {
                            app.StartListening();
                            last_tap = 0;
                        } else {
                            app.ToggleChatState();
                            last_tap = now;
                        }
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void InitializeTouch() {
        if (!touch_.Init(codec_i2c_bus_, 0x38))
            return;
        xTaskCreatePinnedToCore(TouchTask, "touch_task", 4096, this, 5, nullptr, 0);
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = AUDIO_CODEC_I2C_NUM,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = { .enable_internal_pullup = 1 },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MIS0_PIN;
        buscfg.sclk_io_num = DISPLAY_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        cb_boot_ = [this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        };

        boot_button_.OnClick(cb_boot_);

        cb_volume_up_ = [this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) volume = 100;
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume / 10));
            codec->SetOutputVolume(volume);
        };

        volume_up_button_.OnClick(cb_volume_up_);

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        cb_volume_down_ = [this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) volume = 0;
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume / 10));
        };

        volume_down_button_.OnClick(cb_volume_down_);

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });

        cb_backlight_up_ = [this]() {
            auto backlight = GetBacklight();
            int b = backlight->brightness() + 10;
            if (b > 100) b = 100;
            backlight->SetBrightness(b);
            GetDisplay()->ShowNotification("Luminozitate: " + std::to_string(b) + "%");
        };

        backlight_up_button_.OnClick(cb_backlight_up_);

        backlight_up_button_.OnLongPress([this]() {
            auto backlight = GetBacklight();
            backlight->SetBrightness(100);
            GetDisplay()->ShowNotification("Luminozitate MAX");
        });

        cb_backlight_down_ = [this]() {
            auto backlight = GetBacklight();
            int b = backlight->brightness() - 10;
            if (b < 1) b = 1;
            backlight->SetBrightness(b);
            GetDisplay()->ShowNotification("Luminozitate: " + std::to_string(b) + "%");
        };

        backlight_down_button_.OnClick(cb_backlight_down_);

        backlight_down_button_.OnLongPress([this]() {
            auto backlight = GetBacklight();
            backlight->SetBrightness(1);
            GetDisplay()->ShowNotification("Luminozitate MIN");
        });
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        ESP_LOGI(TAG, "Install LCD driver ILI9341");
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
                                     DISPLAY_SWAP_XY);
    }

    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
    }

    void InitializePcf() {
        i2c_device_config_t pcf_cfg = {
            .device_address = 0x27,
            .scl_speed_hz = 100000,
            .scl_wait_us = 0,
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(codec_i2c_bus_, &pcf_cfg, &pcf_dev));
        ESP_LOGI("PCF", "PCF8574 initialized OK");
    }

public:
    MariaAi()
        : boot_button_(BOOT_BUTTON_GPIO),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
          backlight_up_button_(BACKLIGHT_UP_BUTTON_GPIO),
          backlight_down_button_(BACKLIGHT_DOWN_BUTTON_GPIO)
    {
        InitializeI2c();
        InitializePcf();
        InitializeBatteryMonitor();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->SetBrightness(100);

        // Creeăm task-ul DHT
        xTaskCreatePinnedToCore(DhtSensor::BackgroundTask,
                                "dht_task",
                                4096,
                                &dht_,
                                5,
                                nullptr,
                                0);

        // 🔥 Patch-ul DHT: pornește/oprește în funcție de starea device-ului
        xTaskCreatePinnedToCore([](void* arg){
            MariaAi* self = static_cast<MariaAi*>(arg);
            auto& app = Application::GetInstance();

            while (true) {
                if (app.GetDeviceState() == kDeviceStateIdle)
                    self->dht_.Start();
                else
                    self->dht_.Stop();

                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }, "dht_state_watch", 4096, this, 5, nullptr, 0);

        xTaskCreatePinnedToCore(PcfButtonTask, "pcf_buttons", 4096, this, 5, nullptr, 0);

        vTaskDelay(pdMS_TO_TICKS(3000));
        sd_card_mount();

        if (!web_server_started_) {
            web_server_started_ = true;
            ESP_LOGI(TAG, "Creating WebServerTask...");
            xTaskCreate(WebServerTask, "webserver_task", 4096, this, 5, nullptr);
        }
    }

    virtual Led* GetLed() override {
        static SingleLedRainbow led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            codec_i2c_bus_, AUDIO_CODEC_I2C_NUM,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR, true, true);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        charging = adc_battery_monitor_->IsCharging();
        discharging = adc_battery_monitor_->IsDischarging();
        level = adc_battery_monitor_->GetBatteryLevel();
        return true;
    }
};

DECLARE_BOARD(MariaAi);

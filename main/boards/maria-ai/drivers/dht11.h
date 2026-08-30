#ifndef __DHT11_H__
#define __DHT11_H__

#include <inttypes.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace xiaozhi {

class DHT11 {
public:
    // Constructorul necesită specificarea pinilor GPIO conectați la DHT11.
    explicit DHT11(gpio_num_t pin);

    // Citește temperatura și umiditatea, returnează dacă citirea a fost reușită
    // Dacă este setat retry_count > 0, va reîncerca automat în caz de eșec.
    bool ReadData(uint8_t retry_count = 3);

    // Obține cea mai recentă valoare citită a umidității (parte întreagă)
    uint8_t GetHumidity() const { return humidity_; }

    // Obține cea mai recentă valoare a temperaturii citită (partea întreagă)
    uint8_t GetTemperature() const { return temperature_; }

    // Obține numărul de citiri reușite
    uint32_t GetSuccessCount() const { return success_count_; }

    // Obține numărul de erori de citire
    uint32_t GetFailCount() const { return fail_count_; }

    // Obține prospețimea datelor (milisecunde de la ultima citire reușită).
    uint32_t GetDataFreshness() const;

    // Datele sunt proaspete (mai puțin decât numărul specificat de milisecunde)?
    bool IsDataFresh(uint32_t max_age_ms = 30000) const;

private:
    // Inițializează DHT11
    void Init();

    // Se așteaptă schimbarea stării pinului, cu detectarea timeout-ului
    esp_err_t WaitPinState(uint32_t timeout_us, int expected_pin_state);

    // Citește datele
    esp_err_t DataRead();

    // Pinii GPIO conectați la DHT11
    gpio_num_t pin_;

    // Valoare umidității stocată (parte întreagă)
    uint8_t humidity_ = 0;

    // Valoarea temperaturii stocate (partea întreagă)
    uint8_t temperature_ = 0;

    // Număr de succese
    uint32_t success_count_ = 0;

    // Număr de erori
    uint32_t fail_count_ = 0;

    // Citirea datelor brute
    uint8_t buffer_[5] = {0};

    // Timpul scurs de la ultima citire reușită (în microsecunde)
    int64_t last_read_time_ = 0;

    // Interval minim de citire (microsecunde) - 4 secunde
    // Crescut la 4 secunde pentru a oferi DHT11 un timp de recuperare mai lung
    static const int64_t MIN_READ_INTERVAL_US = 4000000;
};

}  // namespace xiaozhi

#endif  // __DHT11_H__

#include "dht11.h"
#include <string.h>
#include "esp_log.h"

namespace xiaozhi {

static const char* TAG = "DHT11";

DHT11::DHT11(gpio_num_t pin) : pin_(pin) { Init(); }

void DHT11::Init() {
    // Configurarea pentru modul de ieșire open-drain necesită o rezistență externă de tracțiune
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT_OD;  // Mod de ieșire cu drenaj deschis
    io_conf.pin_bit_mask = (1ULL << pin_);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;  // Activează rezistența internă de tracțiune
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    // Setat la nivel înalt
    gpio_set_level(pin_, 1);

    // Se așteaptă ca DHT11 să se pornească și să se stabilizeze
    vTaskDelay(1200 / portTICK_PERIOD_MS);

    // Trimite un semnal de pornire „fals”, apoi elimină rezultatul pentru a te asigura că DHT11
    // este într-o stare stabilă.
    gpio_set_level(pin_, 0);
    vTaskDelay(25 / portTICK_PERIOD_MS);  // Mențineți un nivel scăzut timp de cel puțin 18ms
    gpio_set_level(pin_, 1);
    vTaskDelay(50 / portTICK_PERIOD_MS);  // Acordă-i lui DHT11 suficient timp de recuperare

    ESP_LOGI(TAG, "DHT11 initialized on GPIO %d with open-drain mode", pin_);
}

esp_err_t DHT11::WaitPinState(uint32_t timeout_us, int expected_pin_state) {
    int64_t start_time = esp_timer_get_time();
    while (esp_timer_get_time() - start_time <= timeout_us) {
        if (gpio_get_level(pin_) == expected_pin_state)
            return ESP_OK;
        esp_rom_delay_us(1);
    }
    return ESP_FAIL;
}

esp_err_t DHT11::DataRead() {
    esp_err_t result = ESP_FAIL;
    memset(buffer_, 0, sizeof(buffer_));

    // Resetează complet starea GPIO
    // Mai întâi eliberează GPIO-ul, readucându-l la starea implicită
    gpio_reset_pin(pin_);
    vTaskDelay(20 /
               portTICK_PERIOD_MS);  // Mărit la 20ms pentru a permite mai mult timp de stabilizare

    // Reconfigurare la modul de ieșire open-drain
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT_OD;
    io_conf.pin_bit_mask = (1ULL << pin_);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    // Setează la modul de ieșire și menține nivelul ridicat
    gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(pin_, 1);
    vTaskDelay(200 / portTICK_PERIOD_MS);  // Crescut la 200ms, oferind DHT11 mai mult timp pentru a
                                           // se stabiliza.

    // 1. Gazda trimite un semnal de pornire
    gpio_set_level(pin_, 0);
    vTaskDelay(25 / portTICK_PERIOD_MS);  // Mențineți nivelul scăzut timp de cel puțin 18ms,
                                          // creșterea lui la 25ms îl va face mai stabil.
    gpio_set_level(pin_, 1);
    esp_rom_delay_us(40);  // Crescut la 40us, permițând mai mult timp pentru stabilizare.

    // 2. Comutați la modul de introducere a datelor și așteptați răspunsul DHT11.
    gpio_set_direction(pin_, GPIO_MODE_INPUT);

    // 3. Așteptați răspunsul DHT11 (nivel scăzut)
    result = WaitPinState(100, 0);  // Creșterea timeout-ului la 100us
    if (result == ESP_FAIL) {
        /*         ESP_LOGE(TAG, "Phase A Fail, DHT11 not responding with LOW"); */
        return ESP_FAIL;
    }

    // 4. Așteptați ca DHT11 să devină high (răspunsul la nivel scăzut de 80us se termină)
    result = WaitPinState(100, 1);  // Creșterea timeout-ului la 100us
    if (result == ESP_FAIL) {
        /*         ESP_LOGE(TAG, "Phase B Fail, DHT11 not responding with HIGH"); */
        return ESP_FAIL;
    }

    // 5. Așteptați ca DHT11 să ajungă la un nivel scăzut (nivelul maxim de 80us este pe cale să se
    // termine)
    result = WaitPinState(100, 0);  // Creșterea timeout-ului la 100us
    if (result == ESP_FAIL) {
        /*         ESP_LOGE(TAG, "Phase C Fail, DHT11 not starting data transmission"); */
        return ESP_FAIL;
    }

    // 6. Citește 40 biți de date (5 octeți)
    for (int i = 0; i < 5; i++) {
        uint8_t byte_value = 0;
        for (int j = 0; j < 8; j++) {
            // Se așteaptă pornirea biților de date (nivel înalt)
            result = WaitPinState(100, 1);  // Creșterea timeout-ului la 100us
            if (result == ESP_FAIL) {
                /*                 ESP_LOGE(TAG, "Bit %d.%d start timeout", i, j); */
                // Restaurează starea pinului și apoi returnează
                gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
                gpio_set_level(pin_, 1);
                return ESP_FAIL;
            }

            // Întârziere de 30µs, apoi verificare nivel.
            // Dacă este încă ridicat, atunci este 1; altfel, este 0.
            esp_rom_delay_us(
                40);  // Mărit la 40us pentru a permite mai mult timp pentru stabilizare
            if (gpio_get_level(pin_) == 1) {
                byte_value = (byte_value << 1) | 1;
            } else {
                byte_value = byte_value << 1;
            }

            // Așteptăm finalizarea acestui bit (nivel scăzut)
            result = WaitPinState(100, 0);  // Creșterea timeout-ului la 100us
            if (result == ESP_FAIL) {
                /*                 ESP_LOGE(TAG, "Bit %d.%d end timeout", i, j); */
                // Restaurează starea pinului și apoi returnează
                gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
                gpio_set_level(pin_, 1);
                return ESP_FAIL;
            }
        }
        buffer_[i] = byte_value;
    }

    // 7. Validarea datelor
    uint8_t checksum = buffer_[0] + buffer_[1] + buffer_[2] + buffer_[3];
    if (checksum != buffer_[4]) {
        /*         ESP_LOGE(TAG, "Checksum error: calc=0x%02x, recv=0x%02x", checksum, buffer_[4]);
                ESP_LOGE(TAG, "Raw data: %02x %02x %02x %02x %02x",
                        buffer_[0], buffer_[1], buffer_[2], buffer_[3], buffer_[4]); */
        return ESP_FAIL;
    }

    // 8. Actualizați datele de temperatură și umiditate
    // humidity_ = buffer_[0];     // Partea întreagă a umidității
    // temperature_ = buffer_[2];  // Partea întreagă a temperaturii

    uint16_t raw_h = (buffer_[0] << 8) | buffer_[1];
    uint16_t raw_t = (buffer_[2] << 8) | buffer_[3];

    humidity_ = raw_h / 10.0f;
    temperature_ = raw_t / 10.0f;

    // 9. Înregistrați momentul citirii reușite.
    last_read_time_ = esp_timer_get_time();
    success_count_++;

    // 10. Restaurați starea pinului
    gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(pin_, 1);

    ESP_LOGI(TAG, "DHT11 read success: Temperature=%d°C, Humidity=%d%%", temperature_, humidity_);

    return ESP_OK;
}

bool DHT11::ReadData(uint8_t retry_count) {
    ESP_LOGI(TAG, "Starting DHT11 reading with retry_count=%d...", retry_count);

    // Verifică dacă este îndeplinit intervalul minim de citire
    int64_t current_time = esp_timer_get_time();
    if (last_read_time_ > 0 && (current_time - last_read_time_) < MIN_READ_INTERVAL_US) {
        int wait_ms = (MIN_READ_INTERVAL_US - (current_time - last_read_time_)) / 1000;
        ESP_LOGW(TAG, "Reading too frequent! Last read was %lld us ago, waiting %d ms...",
                 (current_time - last_read_time_), wait_ms);

        // Așteptați până când se atinge intervalul minim
        vTaskDelay((wait_ms + 100) /
                   portTICK_PERIOD_MS);  // Adăugați încă 100ms ca marjă de siguranță
    }

    // Așteptați puțin înainte de citire pentru a vă asigura că DHT11 este într-o stare stabilă.
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Încercare de citire; reîncercați dacă eșuează.
    bool success = false;
    for (uint8_t i = 0; i <= retry_count; i++) {
        if (i > 0) {
            ESP_LOGI(TAG, "Retry %d/%d after delay...", i, retry_count);
            // Fiecare reîncercare crește timpul de așteptare, oferind DHT11 mai mult timp de
            // recuperare.
            vTaskDelay((1000 + i * 500) / portTICK_PERIOD_MS);
        }

        esp_err_t result = DataRead();
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Reading successful on %s attempt: Temperature=%d°C, Humidity=%d%%",
                     i == 0 ? "first" : "retry", temperature_, humidity_);
            success = true;
            break;
        }

        // Resetează complet starea pinului după fiecare eșec
        gpio_reset_pin(pin_);
        vTaskDelay(50 / portTICK_PERIOD_MS);  // Mărit la 50ms
    }

    if (!success) {
        /*         ESP_LOGE(TAG, "Failed to read DHT11 after %d retries", retry_count); */
        fail_count_++;
    }

    return success;
}

uint32_t DHT11::GetDataFreshness() const {
    if (last_read_time_ == 0) {
        return UINT32_MAX;  // Indică faptul că datele nu au fost niciodată citite cu succes.
    }

    int64_t current_time = esp_timer_get_time();
    int64_t elapsed_us = current_time - last_read_time_;

    // Conversia în milisecunde și asigurarea că nu există depășire
    uint32_t elapsed_ms = (elapsed_us / 1000);
    if (elapsed_ms > UINT32_MAX) {
        return UINT32_MAX;
    }

    return elapsed_ms;
}

bool DHT11::IsDataFresh(uint32_t max_age_ms) const { return GetDataFreshness() <= max_age_ms; }

}  // namespace xiaozhi

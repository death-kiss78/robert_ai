// sd_card.c

#include "sd_card.h"
#include "config.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char* TAG = "sd_card";

esp_err_t sd_card_mount(void) {
    ESP_LOGI(TAG, "Initializing SD card (SDMMC)...");

    // --- MOUNT CONFIG ---
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, .max_files = 5, .allocation_unit_size = 16 * 1024};

    sdmmc_card_t* card = NULL;

    // --- HOST CONFIG ---
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_4BIT;         // Freenove are 4 linii
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // Frecvență maximă

    // --- SLOT CONFIG ---
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // Setăm pinii Freenove (ESP32-S3 suportă direct remaparea SDMMC)
    slot_config.clk = SD_MMC_CLK;
    slot_config.cmd = SD_MMC_CMD;
    slot_config.d0 = SD_MMC_D0;
    slot_config.d1 = SD_MMC_D1;
    slot_config.d2 = SD_MMC_D2;
    slot_config.d3 = SD_MMC_D3;

    ESP_LOGI(TAG, "Mounting filesystem...");

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SDMMC mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SDMMC mounted OK");
    sdmmc_card_print_info(stdout, card);

    return ESP_OK;
}

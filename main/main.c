#include <sys/time.h>

#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "max17043.h"
#include "pms5003.h"
#include "bme280.h"
#include "sph0645.h"

#include "i2c.h"
#include "uart.h"
#include "i2s.h"

#include "wlan.h"
#include "http.h"
#include "http_data_handler.h"
#include "http_config_handler.h"
#include "http_about_handler.h"

static const char *TAG = "main";

void app_main(void)
{
    // create default event loop
    esp_event_loop_create_default();

    // init non-volatile storage
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "erasing nvs flash");
        nvs_flash_erase();
        if (nvs_flash_init() != ESP_OK)
            esp_restart();
    }

    // start serial communications
    i2c_start();
    uart_start();
    i2s_init();

    pms5003_reset();
    const pms5003_config_t pms_config = PMS5003_PASSIVE_ASLEEP;
    pms5003_set_config(&pms_config);

    max17043_reset();
    const max17043_config_t max_config = MAX17043_DEFAULT_CONFIG;
    max17043_set_config(&max_config);

    bme280_reset();
    const bme280_config_t bme_config = BME280_WEATHER_MONITORING;
    bme280_set_config(&bme_config);

    sph0645_reset();
    const sph0645_config_t sph_config = SPH0645_DEFAULT_CONFIG;
    sph0645_set_config(&sph_config);

    // start wifi and http server
    wlan_start();
    http_start();

    // register http handlers
    http_register_handler("/", HTTP_GET, &http_data_handler, KEEP_SPH0645_DATA);
    http_register_handler("/", HTTP_POST, &http_data_handler, CLEAR_SPH0645_DATA);
    http_register_handler("/", HTTP_PUT, &http_config_handler, NULL);
    http_register_handler("/about", HTTP_GET, &http_about_handler, NULL);
}

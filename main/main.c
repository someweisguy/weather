#include "esp_system.h"
#include "esp_log.h"
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
#include "http_handlers.h"
#include "mqtt.h"
#include "mqtt_handlers.h"

#define MQTT_BROKER "mqtt://192.168.0.2"
#define USE_HTTP

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

    wlan_start();

#ifdef USE_HTTP
    // start http and register handlers
    ESP_LOGI(TAG, "starting http server");
    http_start();
    http_register_handler("/", HTTP_GET, &http_data_handler, (void *)0); // keep data
    http_register_handler("/", HTTP_POST, &http_data_handler, (void *)1); // clear data
    http_register_handler("/", HTTP_PUT, &http_config_handler, NULL);
    http_register_handler("/about", HTTP_GET, &http_about_handler, NULL);
#endif

#ifdef MQTT_BROKER
    // start mqtt and register handlers
    ESP_LOGI(TAG, "starting mqtt client");
    mqtt_start(MQTT_BROKER);
    mqtt_subscribe("weather/data", 1, &mqtt_data_handler);
    mqtt_subscribe("weather/config", 1, &mqtt_config_handler);
    mqtt_subscribe("weather/about", 1, &mqtt_about_handler);
#endif

}

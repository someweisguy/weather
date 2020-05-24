/*
 * main.h
 *
 *  Created on: May 17, 2020
 *      Author: Mitch
 */

#ifndef MAIN_H_
#define MAIN_H_

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include <cstdio>
#include "sdkconfig.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sleep.h"

#include "i2c.h"
#include "uart.h"
#include "ds3231.h"
#include "sdcard.h"

#include "wlan.h"
#include "http.h"
#include "mqtt.h"

#include "helpers.h"

#include "Sensor.h"
#include "BME280.h"
#include "PMS5003.h"

#define SENSOR_READY_MS 		30 * 1000 // 30 seconds
#define LOG_FILE_MAX_SIZE_BYTES 100 * 1024 // 100 KB

#define SDCARD_MOUNT_POINT  "/sdcard"
#define LOG_FILE_PATH       SDCARD_MOUNT_POINT "/events.log"
#define CONFIG_FILE_PATH    SDCARD_MOUNT_POINT "/config.json"
#define DATA_FILE_PATH      SDCARD_MOUNT_POINT "/data.txt"


void initialize_required_services();

int vlogf(const char *format, va_list arg);

void synchronize_system_time_task(void *args);
void send_backlogged_data_task(void *args);
void sensor_sleep_task(void *args);

#endif /* MAIN_H_ */

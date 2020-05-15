/*
 * BME280.h
 *
 *  Created on: Apr 5, 2020
 *      Author: Mitch
 */

#ifndef COMPONENTS_SENSORS_BME280_H_
#define COMPONENTS_SENSORS_BME280_H_

#include <cmath>

#include "Sensor.h"

class BME280: public Sensor {
private:

	const char *TAG { "bme280" };
	const char *celsius { "celsius" }, *pascals { "pascals" },
		*relative_humidity_scale { "relative humidity" };
	const uint8_t I2C_ADDRESS { 0x76 }, REG_CHIP_ID { 0xd0 },
			REG_RESET { 0xe0 }, REG_CONTROLHUMID { 0xf2 }, REG_STATUS { 0xf3 },
			REG_CTRL_MEAS { 0xf4 }, REG_CONFIG { 0xf5 },
			REG_DATA_START { 0xf7 }, REG_TRIM_T1_TO_H1 { 0x88 },
			REG_TRIM_H2_TO_H5 { 0xe1 };

	int32_t t_fine;
	struct comp_val_t {
		uint16_t t1;
		int16_t t2;
		int16_t t3;

		uint16_t p1;
		int16_t p2;
		int16_t p3;
		int16_t p4;
		int16_t p5;
		int16_t p6;
		int16_t p7;
		int16_t p8;
		int16_t p9;

		uint8_t h1;
		int16_t h2;
		uint8_t h3;
		int16_t h4;
		int16_t h5;
		int8_t h6;
	} dig;

	int32_t calculate_t_fine(const int32_t adc_T) {
		const int32_t var1 = ((((adc_T >> 3) - ((int32_t) dig.t1 << 1)))
				* ((int32_t) dig.t2)) >> 11;
		const int32_t var2 = (((((adc_T >> 4) - ((int32_t) dig.t1))
				* ((adc_T >> 4) - ((int32_t) dig.t1))) >> 12)
				* ((int32_t) dig.t3)) >> 14;
		const int32_t T = var1 + var2;

		return T;
	}

	int32_t compensate_temperature() {
		// Return temperature in 1/100ths of a degree Celsius
		return (t_fine * 5 + 128) >> 8;
	}

	uint32_t compensate_pressure(const int32_t adc_P) {
		// Return pressure in Pascals * 256
		int64_t var1, var2, P;
		var1 = ((int64_t) t_fine) - 128000;
		var2 = var1 * var1 * (int64_t) dig.p6;
		var2 = var2 + ((var1 * (int64_t) dig.p5) << 17);
		var2 = var2 + (((int64_t) dig.p4) << 35);
		var1 = ((var1 * var1 * (int64_t) dig.p3) >> 8)
				+ ((var1 * (int64_t) dig.p2) << 12);
		var1 = (((((int64_t) 1) << 47) + var1)) * ((int64_t) dig.p1) >> 33;
		if (var1 == 0)
			return 0; // avoid divide by zero
		P = 1048576 - adc_P;
		P = (((P << 31) - var2) * 3125) / var1;
		var1 = (((int64_t) dig.p9) * (P >> 13) * (P >> 13)) >> 25;
		var2 = (((int64_t) dig.p8) * P) >> 19;
		P = ((P + var1 + var2) >> 8) + (((int64_t) dig.p7) << 4);

		return P;
	}

	uint32_t compensate_humidity(const int32_t adc_H) {
		// Return relative humidity * 1024
		int32_t v_x1_u32r;
		v_x1_u32r = (t_fine - ((int32_t) 76800));
		v_x1_u32r = (((((adc_H << 14) - (((int32_t) dig.h4) << 20)
				- (((int32_t) dig.h5) * v_x1_u32r)) + ((int32_t) 16384)) >> 15)
				* (((((((v_x1_u32r * ((int32_t) dig.h6)) >> 10)
						* (((v_x1_u32r * ((int32_t) dig.h3)) >> 11)
								+ ((int32_t) 32768))) >> 10)
						+ ((int32_t) 2097152)) * ((int32_t) dig.h2) + 8192)
						>> 14));
		v_x1_u32r = (v_x1_u32r
				- (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7)
						* ((int32_t) dig.h1)) >> 4));
		v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
		v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;
		const uint32_t H = (uint32_t) (v_x1_u32r >> 12);

		return H;
	}

public:

	const char* get_name() override {
		return "BME280";
	}

	esp_err_t ready() override {
		// Get compensation "dig" values
		uint8_t dig_buf[32];
		ESP_LOGV(TAG, "Getting data trim values");
		const esp_err_t dig_1_ret { i2c_read(I2C_ADDRESS, REG_TRIM_T1_TO_H1, dig_buf, 25) };
		if (dig_1_ret != ESP_OK)
			return dig_1_ret;
		const esp_err_t dig_2_ret { i2c_read(I2C_ADDRESS, REG_TRIM_H2_TO_H5, dig_buf + 25, 7) };
		if (dig_2_ret != ESP_OK)
			return dig_2_ret;

		// Bulk copy T1 to H1
		memcpy(&dig, dig_buf, 25);
		// Copy H2 to H6
		dig.h2 = (dig_buf[26] << 8) | dig_buf[25];
		dig.h3 = dig_buf[27];
		dig.h4 = (dig_buf[28] << 4) | (dig_buf[29] & 0x0f);
		dig.h5 = (dig_buf[30] << 4) | (dig_buf[29] >> 4);
		dig.h6 = dig_buf[31];

		return ESP_OK;
	}

	esp_err_t setup() override {
		// Soft reset the BME280
		ESP_LOGV(TAG, "Sending soft reset command");
		const uint8_t reset_cmd { 0xb6 };
		const esp_err_t reset_ret { i2c_write(I2C_ADDRESS, REG_RESET, &reset_cmd, 1) };
		if (reset_ret != ESP_OK)
			return reset_ret;

		// Wait for the reset to take effect
		ESP_LOGV(TAG, "Waiting for response to soft reset");
		uint8_t status;
		do {
			vTaskDelay(10 / portTICK_PERIOD_MS);
			const esp_err_t wait_ret { i2c_read(I2C_ADDRESS, REG_STATUS, &status, 1) };
			if (wait_ret != ESP_OK)
				return wait_ret;
		} while ((status & 0x01) == 0x01);

		// Ensure that chip ID is correct
		ESP_LOGV(TAG, "Getting chip ID");
		uint8_t id;
		if (i2c_read(I2C_ADDRESS, REG_CHIP_ID, &id, 1) != ESP_OK && id == 0x60)
			return ESP_ERR_NOT_FOUND;

		// Set the sensor to sleep mode, otherwise settings will be ignored
		ESP_LOGV(TAG, "Sending sleep mode command");
		const uint8_t sleep_cmd { 0x00 };
		const esp_err_t sleep_ret { i2c_write(I2C_ADDRESS, REG_CTRL_MEAS, &sleep_cmd, 1) };
		if (sleep_ret != ESP_OK)
			return sleep_ret;

		ESP_LOGV(TAG, "Setting filtering x16 and standby 20ms");
		// Set filtering x16 and set the standby in normal mode to 20ms
		const uint8_t filter_standby { 0xf0 };
		const esp_err_t filtering_ret { i2c_write(I2C_ADDRESS, REG_CONFIG, &filter_standby, 1) };
		if (filtering_ret != ESP_OK)
			return filtering_ret;

		ESP_LOGV(TAG, "Setting humidity sampling x16");
		// Write the sample rate to the Humidity control register
		const uint8_t hum_sample { 0x05 };
		const esp_err_t sample_ret { i2c_write(I2C_ADDRESS, REG_CONTROLHUMID, &hum_sample, 1) };
		if (sample_ret != ESP_OK)
			return sample_ret;

		// Set temperature and pressure sampling to x16 and sleep mode
		ESP_LOGV(TAG, "Setting temperature and pressure sampling x16 and sleep "
				"mode");
		const uint8_t tpm_setting { 0xb0 };
		return i2c_write(I2C_ADDRESS, REG_CTRL_MEAS, &tpm_setting, 1);
	}

	esp_err_t wakeup() override {
		// Set temperature and pressure sampling to x16 and normal mode
		const uint8_t tpm_setting { 0xb7 };
		return i2c_write(I2C_ADDRESS, REG_CTRL_MEAS, &tpm_setting, 1);
	}

	esp_err_t get_data(cJSON *json) override {
		// Store all the ADC values from the BME280 into a buffer
		uint8_t buf[8];
		const esp_err_t read_ret { i2c_read(I2C_ADDRESS, REG_DATA_START, buf, 8) };
		if (read_ret != ESP_OK)
			return read_ret;

		// Declare temperature in this scope so we can get dew point later
		double temperature_C;

		// Extract the pressure ADC values from the buffer
		int32_t adc_T = buf[3];
		for (int i = 4; i < 6; ++i) {
			adc_T <<= 8;
			adc_T |= buf[i];
		}

		// Check if temperature is turned off
		if (adc_T == 0x800000)
			// If temperature is turned off, we can't get any data
			return ESP_ERR_INVALID_STATE;
		else {
			// Calculate temperature in degrees Celsius
			adc_T >>= 4;
			t_fine = calculate_t_fine(adc_T);
			temperature_C = compensate_temperature() / 100.0;

			// We have at least one data point now, so start building the JSON object
			add_JSON_elem(json, "Temperature", temperature_C, celsius);
		}

		// Extract the pressure ADC values from the buffer
		int32_t adc_P = buf[0];
		for (int i = 1; i < 3; ++i) {
			adc_P <<= 8;
			adc_P |= buf[i];
		}
		// Calculate pressure in Pascals
		if (adc_P != 0x800000) {
			adc_P >>= 4;
			const uint64_t pressure_Pa { compensate_pressure(adc_P) / 256 };
			add_JSON_elem(json, "Barometric Pressure", pressure_Pa, pascals);
		}

		// Extract the pressure ADC values from the buffer
		int32_t adc_H = (buf[6] << 8) | buf[7];

		// Calculate relative humidity and dew point
		if (adc_H != 0x8000) {
			const double relative_humidity { compensate_humidity(adc_H) / 1024.0 };
			add_JSON_elem(json, "Relative Humidity", relative_humidity,
					relative_humidity_scale);
		}

		return ESP_OK;
	}

	esp_err_t sleep() override {
		// Set temperature and pressure sampling to x16 and sleep mode
		const uint8_t tpm_setting { 0xb4 };
		return i2c_write(I2C_ADDRESS, REG_CTRL_MEAS, &tpm_setting, 1);
	}

};

#endif /* COMPONENTS_SENSORS_BME280_H_ */

/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

/* Stubs for everything etc_calibration.c links against. These tests exercise
 * the locking and hardware-ownership model, not the measurement itself, so the
 * hardware side returns nominal in-range values and records that it was called.
 */

#include <zephyr/kernel.h>
#include <stdint.h>
#include <string.h>

#include "events/sensor_event.h"
#include "etc_calibration.h"
#include "etc_device.h"
#include "etc_sensor.h"
#include "mock_deps.h"

/* Mirrors enum switch_state in etc_calibration.c: the run drives the GPIO mask
 * to each of these in turn and checks the response against a per-switch range.
 */
#define MOCK_SW_ADC_OFFSET 0
#define MOCK_SW_TEMP_0_2   1
#define MOCK_SW_TEMP_25_0  2
#define MOCK_SW_TEMP_44_6  3
#define MOCK_SW_TEMP_70_4  4
#define MOCK_SW_ADC_HIGH   5

/* Nominal ADC counts, inside list_adc[] = {{-100, 100}, {3914, 4114}}. */
#define MOCK_ADC_OFFSET 0
#define MOCK_ADC_HIGH	4000

struct mock_state mock;

void mock_reset(void)
{
	memset(&mock, 0, sizeof(mock));
}

/* etc_sensor calibration hardware */
void etc_sensor_calibration_enter(void)
{
	mock.hw_powered = true;
	mock.hw_owner = HW_OWNER_CALIBRATION;
	mock.enter_calls++;
}

void etc_sensor_calibration_exit(void)
{
	mock.hw_powered = false;
	mock.hw_owner = HW_OWNER_NONE;
	mock.exit_calls++;
}

void etc_sensor_calibration_release_hw(void)
{
	/* Clears ownership but leaves the rail powered, mirroring the real
	 * implementation. */
	mock.hw_owner = HW_OWNER_NONE;
}

int etc_sensor_calibration_scan(void)
{
	return mock.scan_result;
}

int etc_sensor_calibration_read_sn(void)
{
	return mock.read_sn_result;
}

int etc_sensor_calibration_set_gpio_mask(int8_t mask)
{
	mock.last_gpio_mask = mask;
	return 0;
}

int etc_sensor_calibration_read_adc(struct etc_sensor_adc_raw_data *raw_adc)
{
	int value = mock.last_gpio_mask == MOCK_SW_ADC_HIGH ? MOCK_ADC_HIGH : MOCK_ADC_OFFSET;

	if (raw_adc) {
		for (int i = 0; i <= SENSOR_INPUT_IN4; i++) {
			raw_adc->port[i] = value;
		}
	}
	return value;
}

float etc_sensor_calibration_read_temperature_from_sensor(void)
{
	return mock.ambient_temp;
}

uint16_t etc_sensor_calibration_get_hw_version_adc(void)
{
	return 0;
}

void etc_sensor_calibration_save_temperature_compensation(uint16_t hw_raw_adc)
{
	ARG_UNUSED(hw_raw_adc);
}

/* Returns the temperature the switch currently selected by
 * etc_sensor_calibration_set_gpio_mask() is expected to read back, so a nominal
 * run passes its per-switch range checks.
 */
float etc_sensor_calibration_convert_temperature(int raw_adc, uint16_t *rr_hw_adc,
						 struct etc_sensor_adc_calibration_info *info)
{
	ARG_UNUSED(raw_adc);
	ARG_UNUSED(rr_hw_adc);
	ARG_UNUSED(info);

	switch (mock.last_gpio_mask) {
	case MOCK_SW_TEMP_0_2:
		return 0.2f;
	case MOCK_SW_TEMP_25_0:
		return 25.0f;
	case MOCK_SW_TEMP_44_6:
		return 44.6f;
	case MOCK_SW_TEMP_70_4:
		return 70.4f;
	default:
		return mock.ambient_temp;
	}
}

uint16_t etc_sensor_sample_and_get_battery(void)
{
	return mock.battery_mv;
}

/* etc_device settings */
int etc_device_write_setting(uint16_t setting_id, const void *setting, int setting_size)
{
	ARG_UNUSED(setting_id);
	ARG_UNUSED(setting);
	ARG_UNUSED(setting_size);
	return 0;
}

/* No stored calibration: the run then reports "no previous adjustment", which
 * is the path a first-ever calibration takes.
 */
int etc_device_read_setting(uint16_t setting_id, void *setting, int setting_size)
{
	ARG_UNUSED(setting_id);
	ARG_UNUSED(setting);
	ARG_UNUSED(setting_size);
	return -ENOENT;
}

/* date_time */
int date_time_now(int64_t *unix_time_ms)
{
	*unix_time_ms = 0;
	return 0;
}

/* app_module */
void app_module_notify_calibration_timeout(void)
{
	mock.timeout_notifications++;
}

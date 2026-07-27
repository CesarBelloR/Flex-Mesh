/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#include "mock/mock_deps.h"

#include "common.h"
#include "etc_calibration.h"
#include "etc_device.h"
#include "etc_settings.h"
#include "events/sensor_event.h"

#define MOCK_ADC_AMBIENT 2100
#define MOCK_ADC_BATTERY 2500
/* Inside SENSOR_RR_VALID_MIN/MAX so the Rr fixup path is exercised. */
#define MOCK_ADC_HW_VER 1000

struct mock_state mock;

static const struct gpio_dt_spec sel0 =
	GPIO_DT_SPEC_GET(DT_NODELABEL(sens_sel0), control_gpios);
static const struct gpio_dt_spec sel1 =
	GPIO_DT_SPEC_GET(DT_NODELABEL(sens_sel1), control_gpios);

void mock_reset(void)
{
	memset(&mock, 0, sizeof(mock));
	for (int i = 0; i < MOCK_NUM_PORTS; i++) {
		mock.port[i].adc[MOCK_BRANCH_A] = MOCK_ADC_OPEN;
		mock.port[i].adc[MOCK_BRANCH_B] = MOCK_ADC_OPEN;
		mock.port[i].switch_fail_branch = -1;
	}
}

void mock_attach_splitter(int port, int adc_a, int adc_b)
{
	mock.port[port].splitter = true;
	mock.port[port].adc[MOCK_BRANCH_A] = adc_a;
	mock.port[port].adc[MOCK_BRANCH_B] = adc_b;
}

void mock_attach_probe(int port, int adc)
{
	mock.port[port].splitter = false;
	mock.port[port].adc[MOCK_BRANCH_A] = adc;
	mock.port[port].adc[MOCK_BRANCH_B] = MOCK_ADC_OPEN;
}

int mock_selected_port(void)
{
	int s0 = gpio_emul_output_get(sel0.port, sel0.pin);
	int s1 = gpio_emul_output_get(sel1.port, sel1.pin);

	if (s0 < 0 || s1 < 0) {
		return -1;
	}
	/* remap_th_channel() for board 0.3.0 swaps ports in pairs, and is its own
	 * inverse, so the same operation recovers the logical port. */
	return ((s1 << 1) | s0) ^ 1;
}

float mock_temperature(int raw_adc)
{
	return raw_adc < 0 ? (float)SENSOR_TEMP_NO_CONNECTED : (float)raw_adc / 100.0f;
}

/* --- modules/exact lib/adc ------------------------------------------------ */

int adc_init(void)
{
	return 0;
}

int adc_get_channel_filtered(int channel)
{
	const struct mock_port *port;
	enum mock_branch branch;
	int selected;
	int raw;

	switch (channel) {
	case ETC_ADC_CHANNEL_AMB:
		return MOCK_ADC_AMBIENT;
	case ETC_ADC_CHANNEL_BATTERY:
		return MOCK_ADC_BATTERY;
	case ETC_ADC_CHANNEL_HW_VER:
		return MOCK_ADC_HW_VER;
	case ETC_ADC_CHANNEL_SENSOR:
		break;
	default:
		return 0;
	}

	selected = mock_selected_port();
	if (selected < 0) {
		return MOCK_ADC_OPEN;
	}
	port = &mock.port[selected];
	/* A port without a splitter has one branch: the probe answers whichever
	 * branch the firmware believes it selected. */
	branch = port->splitter ? port->branch : MOCK_BRANCH_A;
	raw = port->adc[branch];
	mock.adc_reads++;
	/* Two reads of the same probe differ by an LSB, as a median-of-N sampler
	 * does on hardware. */
	return raw >= MOCK_ADC_OPEN ? raw : raw + (mock.adc_reads & 1);
}

int adc_get_channel(int channel)
{
	return adc_get_channel_filtered(channel);
}

int adc_get_raw_to_millivolts(int channel, int *raw)
{
	ARG_UNUSED(channel);
	ARG_UNUSED(raw);
	return 0;
}

/* --- etc_sensor_helper ---------------------------------------------------- */

int etc_sensor_helper_get_calibrated_adc(int raw_adc, uint16_t *hw_raw_adc,
					 struct etc_sensor_adc_calibration_info *calibration_info)
{
	ARG_UNUSED(hw_raw_adc);
	ARG_UNUSED(calibration_info);
	return raw_adc;
}

float etc_sensor_helper_ntc_get(int raw_adc, int channel)
{
	ARG_UNUSED(channel);
	return mock_temperature(raw_adc);
}

/* --- remaining etc-app dependencies --------------------------------------- */

int etc_sensor_load_calibration_info(struct etc_sensor_adc_calibration_info *info)
{
	memset(info, 0, sizeof(*info));
	return 0;
}

int ds2484_get_logic_level(const struct device *dev)
{
	ARG_UNUSED(dev);
	/* Never grounded: the shorted-1-wire guard is not what this suite covers. */
	return 1;
}

void etc_battery_poll_status(void)
{
}

int etc_calibration_lock_timeout(k_timeout_t timeout)
{
	ARG_UNUSED(timeout);
	return 0;
}

void etc_calibration_unlock(void)
{
}

enum etc_device_type etc_get_device_type(void)
{
	return ETC_DEVICE_TYPE_LOGGER;
}

uint16_t etc_get_rr_value(void)
{
	return MOCK_ADC_HW_VER;
}

int etc_set_rr_value(int value)
{
	ARG_UNUSED(value);
	return 0;
}

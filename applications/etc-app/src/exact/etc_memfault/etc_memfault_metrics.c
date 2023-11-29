/*
 * Copyright (c) 2023 EXACT Technology
 *
 */

#include "etc_memfault.h"
#include "etc_memfault_metrics.h"
#include "etc_settings.h"
#include "etc_device.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <memfault/metrics/metrics.h>
#include <memfault/core/trace_event.h>

#include "etc_battery.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_memfault_metrics, CONFIG_MEMFAULT_ETC_LOG_LEVEL);

#define MAX_OPERATOR_LEN 16

struct message_stats {
	uint16_t num_failed_sends;
	uint16_t num_successful_sends;
} message_stats;

struct cumulative_average {
	uint16_t n;
	int32_t sum;
};

struct modem_internal_stat {
	char operator[MAX_OPERATOR_LEN];
	struct cumulative_average rsrp;
	struct cumulative_average rsrq;
} modem_stats;

static uint32_t modem_on_time_ms = 0;

static int64_t last_heartbeat_ms;
static bool battery_charger_was_connected;
static int8_t battery_charge_level_percent = -1;
static uint8_t num_started_ota_attempts;
static uint8_t num_failed_ota_attempts;

void etc_mflt_metrics_init_img_pubkey_id(void)
{
	uint8_t pubkey_id[IMG_PUBKEY_ID_LEN];
	char pubkey_id_hex[2 * IMG_PUBKEY_ID_LEN + 1];

	etc_device_get_img_pubkey_id(pubkey_id, sizeof(pubkey_id));
	bin2hex(pubkey_id, sizeof(pubkey_id), pubkey_id_hex, sizeof(pubkey_id_hex));
	memfault_metrics_heartbeat_set_string(MEMFAULT_METRICS_KEY(device_img_pubkey_id),
					      pubkey_id_hex);
}

void etc_mflt_metrics_charging(enum sensor_event_type evt)
{
	switch (evt) {
	case SENSOR_EVT_BATTERY_IN_NORMAL:
		break;

	case SENSOR_EVT_BATTERY_IN_CHARGING:
	case SENSOR_EVT_BATTERY_CHARGE_COMPLETE:
		battery_charger_was_connected = true;
		break;
	
	default:
	}
}

void etc_mflt_metrics_ota_started(void)
{
	num_started_ota_attempts++;
}

void etc_mflt_metrics_ota_failed(void)
{
	num_failed_ota_attempts++;
}

void etc_mflt_metrics_modem_network(uint16_t mcc, uint16_t mnc, int rsrp, int rsrq)
{
	if (rsrp < 0) {
		modem_stats.rsrp.sum += rsrp;
		modem_stats.rsrp.n++;
	}

	if (rsrq < 0) {
		modem_stats.rsrq.sum += rsrq;
		modem_stats.rsrq.n++;
	}


	memfault_metrics_heartbeat_set_unsigned(
		MEMFAULT_METRICS_KEY(modem_mcc), 
		mcc);
	memfault_metrics_heartbeat_set_unsigned(
		MEMFAULT_METRICS_KEY(modem_mnc), 
		mnc);
}

void etc_mflt_metrics_modem_conn_time(int64_t time_to_connect_ms) 
{
	if (time_to_connect_ms == -1) {
		return;
	}

	memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(modem_time_to_connect_ms), 
				(uint32_t)time_to_connect_ms);
}

void etc_mflt_metrics_modem_on_time(int64_t on_time_ms)
{
	if (on_time_ms < 0) {
		return;
	}

	modem_on_time_ms += on_time_ms;
}

void etc_mflt_metrics_send_successful(void)
{
	message_stats.num_successful_sends++;
}

void etc_mflt_metrics_send_failed(void)
{
	message_stats.num_failed_sends++;
}

char *battery_status_to_string(enum battery_status bat_status) 
{
	switch (bat_status) {
	case BATTERY_NORMAL:
		return "none";
	case BATTERY_CHARGE_IN_PROCESS:
		return "charging";
	case BATTERY_CHARGE_COMPLETE:
		return "full";
	case BATTERY_DAMAGED:
		return "error";
	case BATTERY_LOW:
		return "none";
	case BATTERY_NO_INSTALLED:
		return "error";
	case BATTERY_UNKNOWN:
		return "none";
	default:
		return "none";
	}
}

static int32_t average_result_and_reset(struct cumulative_average *avg_data)
{
	int32_t avg;

	if (avg_data->n == 0) {
		return 0;
	}

	avg = avg_data->sum / avg_data->n;
	avg_data->sum = 0;
	avg_data->n = 0;
	return avg;
}

static void collect_ota_metrics(void)
{
	memfault_metrics_heartbeat_set_unsigned(
		MEMFAULT_METRICS_KEY(ota_attempts), 
		num_started_ota_attempts);
	
	memfault_metrics_heartbeat_set_unsigned(
		MEMFAULT_METRICS_KEY(failed_ota_attempts), 
		num_failed_ota_attempts);
	
	num_started_ota_attempts = 0;
	num_failed_ota_attempts = 0;
}

static void collect_device_metrics(void)
{
	memfault_metrics_heartbeat_set_unsigned(
		MEMFAULT_METRICS_KEY(device_mode), 
		etc_get_device_mode());

	memfault_metrics_heartbeat_set_unsigned(
		MEMFAULT_METRICS_KEY(device_power_mode), 
		etc_get_power_mode());

	/* ToDo: account for probe mode tx interval */
	memfault_metrics_heartbeat_set_unsigned(
		MEMFAULT_METRICS_KEY(device_tx_interval_s), 
		etc_get_tx_interval_secs());
	
	memfault_metrics_heartbeat_set_unsigned(
		MEMFAULT_METRICS_KEY(device_unacked_messages), 
		etc_device_nack_count());
}

static void collect_modem_metrics(void)
{
	int32_t avg;

	if (modem_stats.rsrp.n > 0) {
		avg = average_result_and_reset(&modem_stats.rsrp);
		memfault_metrics_heartbeat_set_signed(
			MEMFAULT_METRICS_KEY(modem_avg_rsrp), 
			avg);
	}

	if (modem_stats.rsrq.n > 0) {
		avg = average_result_and_reset(&modem_stats.rsrq);
		memfault_metrics_heartbeat_set_signed(
			MEMFAULT_METRICS_KEY(modem_avg_rsrq), 
			avg);
	}

	memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(modem_on_time_ms),
						modem_on_time_ms);
	modem_on_time_ms = 0;
}

static void collect_battery_metrics(void)
{
	const uint16_t battery_voltage_mv = etc_battery_get_voltage_mV();
	const uint8_t battery_percent_now =
		etc_battery_percentage_from_voltage(battery_voltage_mv);
	const int16_t battery_drop = 
		battery_charge_level_percent - battery_percent_now;
	const enum battery_status bat_status = etc_battery_get_status();

	LOG_INF("bat: %u%%, drop %d%%", battery_percent_now, battery_drop);

	memfault_metrics_heartbeat_set_string(MEMFAULT_METRICS_KEY(battery_status),
					      battery_status_to_string(bat_status));

	memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(BatteryMv), 
					        battery_voltage_mv);
	
	if (battery_drop >= 0 && 
	    battery_charge_level_percent > 0 &&
	    !battery_charger_was_connected) {
		memfault_metrics_heartbeat_set_unsigned(
			MEMFAULT_METRICS_KEY(Battery_ChargeLevelDrop), 
			battery_drop);
	}

	if (battery_percent_now > 0) {
		memfault_metrics_heartbeat_set_unsigned(
			MEMFAULT_METRICS_KEY(Battery_ChargeLevel), 
			battery_percent_now);
	}

	battery_charge_level_percent = battery_percent_now;
	if ((bat_status == BATTERY_NORMAL) ||
	    (bat_status == BATTERY_UNKNOWN) ||
	    (bat_status == BATTERY_LOW)) {
		battery_charger_was_connected = false;
	}
}

static void collect_heartbeat_interval(void)
{
	int64_t time_now = k_uptime_get();
	if (last_heartbeat_ms == 0) {
		last_heartbeat_ms = time_now;
		return;
	}
	int64_t time_since_last_hearbeat_s = (time_now - last_heartbeat_ms) / 1000;

	last_heartbeat_ms = time_now;

	if (time_since_last_hearbeat_s > UINT32_MAX ||
	    time_since_last_hearbeat_s < 0) {
		return;
	}

	memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(heartbeat_interval_s),
						time_since_last_hearbeat_s);
}

static inline void collect_message_stats(void)
{
	memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(successful_sends),
						message_stats.num_successful_sends);
	memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(failed_sends),
						message_stats.num_failed_sends);

	message_stats.num_successful_sends = 0;
	message_stats.num_failed_sends = 0;
}

static int collect_nrf52_temp(void)
{
	int ret;
	const struct device *sensor_dev = DEVICE_DT_GET(DT_NODELABEL(temp));
	struct sensor_value sen_val;
	double temp;

	ret = sensor_sample_fetch(sensor_dev);
	if (ret) {
		LOG_WRN("could not read nRF temp, %d", ret);
		return -1;
	}
	
	ret = sensor_channel_get(sensor_dev, SENSOR_CHAN_DIE_TEMP, &sen_val);
	if (ret) {
		LOG_WRN("could not read nRF temp, %d", ret);
		return -1;
	}
	temp = sensor_value_to_double(&sen_val);
	temp = round(temp);

	memfault_metrics_heartbeat_set_signed(MEMFAULT_METRICS_KEY(nRF_Temp_C),
					      (int32_t)temp);

	return 0;
}

static void collect_heartbeat_metrics(void)
{
	int ret;
	collect_nrf52_temp();
	collect_message_stats();
	collect_heartbeat_interval();
	collect_battery_metrics();
	collect_modem_metrics();
	collect_device_metrics();
	collect_ota_metrics();
}

void memfault_metrics_heartbeat_collect_data(void)
{
	LOG_INF("Collecting Memfault metrics");
	collect_heartbeat_metrics();
}
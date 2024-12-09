/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Copyright (c) 2023 EXACT Technology
 */

#include <zephyr/net/lwm2m.h>
#include <date_time.h>
#include <lwm2m_resource_ids.h>
#include <string.h>
#include <math.h>

#include "lwm2m_codec_defines.h"
#include "lwm2m_codec_helpers.h"
#include "app_version.h"
#include "etc_util.h"
#include "etc_device.h"
#include "etc_settings.h"
#include "etc_sensor.h"
#include "etc_battery.h"
#include "etc_memfault.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lwm2m_codec_helpers, CONFIG_CLOUD_CODEC_LOG_LEVEL);

/* Some resources does not have designated buffers. Therefore we define those in here. */
static uint8_t bearers[2] = { LTE_FDD_BEARER, NB_IOT_BEARER };
static int battery_voltage;
static int battery_status;
static time_t button_ts;

static char device_id[ETC_SETTINGS_DEVICE_ID_LEN];
static char hardware_version[ETC_SETTING_HW_VER_LEN];

/* Timestamps, minimum, and maximum values for the BME680 present on the Thingy:91. */
static double temp_min_range_val = TEMP_MIN_RANGE_VALUE;
static double temp_max_range_val = TEMP_MAX_RANGE_VALUE;
static double humid_min_range_val = HUMID_MIN_RANGE_VALUE;
static double humid_max_range_val = HUMID_MAX_RANGE_VALUE;
static time_t temperature_ts[SENSOR_INPUT_AMBIENT + 1];
static time_t humidity_ts;

static int lwm2m_codec_helpers_setup_sensor_obj_values(void)
{
	int err;
	
	for (int i = 0; i <= SENSOR_INPUT_AMBIENT; i++)
	{
		/* Temperature object. */
		err = lwm2m_set_f64(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, i,
					       MIN_RANGE_VALUE_RID),
				    temp_min_range_val);
		if (err) {
			return err;
		}

		err = lwm2m_set_f64(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, i,
					       MAX_RANGE_VALUE_RID),
				    temp_max_range_val);
		if (err) {
			return err;
		}

		err = lwm2m_set_f64(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, i,
					       SENSOR_VALUE_RID), NAN);

		err = lwm2m_set_u8(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, i,
					      ETC_TEMP_OBJ_R_TYPE), 0);

		err = lwm2m_set_u8(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, i,
					      ETC_TEMP_OBJ_R_PORT), i);
	}

	/* Humidity object. */
	err = lwm2m_set_f64(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0,
				       MIN_RANGE_VALUE_RID),
			    humid_min_range_val);
	if (err) {
		return err;
	}

	err = lwm2m_set_f64(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0,
				       MAX_RANGE_VALUE_RID),
			    humid_max_range_val);
	if (err) {
		return err;
	}

	/* Set default object port for humidity is -1 */
	err = lwm2m_set_s8(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0,
				ETC_HUMID_OBJ_R_PORT), -1);

	return 0;
}

static int lwm2m_codec_helpers_validate_config_cb(uint16_t obj_inst_id,
						  uint16_t res_id, uint16_t res_inst_id,
						  uint8_t *data, uint16_t data_len,
						  bool last_block, size_t total_size)
{
	int rc = 0;

	switch (res_id) {
	case ETC_CFG_OBJ_R_DEVICE_MODE:
		rc = util_validate_u8(*(uint8_t *)data, ETC_CFG_OBJ_R_DEVICE_MODE_MIN_VAL,
			    ETC_CFG_OBJ_R_DEVICE_MODE_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_POWER_MODE:
		rc = util_validate_u8(*(uint8_t *)data, ETC_CFG_OBJ_R_POWER_MODE_MIN_VAL,
			    ETC_CFG_OBJ_R_POWER_MODE_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_TX_INTERVAL:
		rc = util_validate_u32(*(uint32_t *)data, ETC_CFG_OBJ_R_TX_INTERVAL_MIN_VAL,
			    ETC_CFG_OBJ_R_TX_INTERVAL_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_LOG_INTERVAL:
		rc = util_validate_u32(*(uint32_t *)data, ETC_CFG_OBJ_R_LOG_INTERVAL_MIN_VAL,
			    ETC_CFG_OBJ_R_LOG_INTERVAL_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_LOG_INTERVAL_ALARM:
		rc = util_validate_u32(*(uint32_t *)data, ETC_CFG_OBJ_R_LOG_INTERVAL_ALARM_MIN_VAL,
			    ETC_CFG_OBJ_R_LOG_INTERVAL_ALARM_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_TX_DELAY:
		rc = util_validate_u16(*(uint16_t *)data, ETC_CFG_OBJ_R_TX_DELAY_MIN_VAL,
			    ETC_CFG_OBJ_R_TX_DELAY_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_WAKE_EARLY:
		rc = util_validate_u16(*(uint16_t *)data, ETC_CFG_OBJ_R_WAKE_EARLY_MIN_VAL,
			    ETC_CFG_OBJ_R_WAKE_EARLY_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_RX_DURATION:
		rc = util_validate_u16(*(uint16_t *)data, ETC_CFG_OBJ_R_RX_DURATION_MIN_VAL,
			    ETC_CFG_OBJ_R_RX_DURATION_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_TX_INTERVAL_ALARM:
		rc = util_validate_u32(*(uint32_t *)data, ETC_CFG_OBJ_R_TX_INTERVAL_ALARM_MIN_VAL,
			    ETC_CFG_OBJ_R_TX_INTERVAL_ALARM_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_TX_PROBE:
		rc = util_validate_u32(*(uint32_t *)data, ETC_CFG_OBJ_R_TX_PROBE_MIN_VAL,
			    ETC_CFG_OBJ_R_TX_PROBE_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_LORA_PROBE_OFFSET: {
		rc = util_validate_in_lora_probe_offset(*(uint16_t*)data);
		break;
	}
	case ETC_CFG_OBJ_R_LTE_PROBE_OFFSET:
		rc = util_validate_u16(*(uint16_t *)data, ETC_CFG_OBJ_R_LTE_PROBE_OFFSET_MIN_VAL,
			    ETC_CFG_OBJ_R_LTE_PROBE_OFFSET_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_LOCATION_REQ_INTERVAL:
		rc = util_validate_u32(*(uint32_t *)data, ETC_CFG_OBJ_R_LOCATION_REQ_INTERVAL_MIN_VAL,
				       ETC_CFG_OBJ_R_LOCATION_REQ_INTERVAL_MAX_VAL);
		break;
	case ETC_CFG_OBJ_R_GNSS_TIMEOUT:
		rc = util_validate_u16(*(uint16_t *)data, ETC_CFG_OBJ_R_GNSS_TIMEOUT_MIN_VAL,
				       ETC_CFG_OBJ_R_GNSS_TIMEOUT_MAX_VAL);
		break;
	}
	return rc;
}

int lwm2m_codec_helpers_create_objects_and_resources(void)
{
	int err;

	err = lwm2m_create_object_inst(
				&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0));
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
					   APN, 0), 
				CONFIG_MODEM_QUECTEL_BG95_M3_APN, 
				sizeof(CONFIG_MODEM_QUECTEL_BG95_M3_APN),
				sizeof(CONFIG_MODEM_QUECTEL_BG95_M3_APN),
				LWM2M_RES_DATA_FLAG_RO);

	err = lwm2m_create_res_inst(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0,
						POWER_SOURCE_VOLTAGE_RID, 0));
	if (err) {
		return err;
	}

	return 0;
}

static int lwm2m_codec_helpers_set_callback_for_config_object(lwm2m_engine_set_data_cb_t callback)
{
	int err;

	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, DEVICE_MODE_RID),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, DEVICE_MODE_RID),
						 lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}
	
	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, POWER_MODE_RID),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, POWER_MODE_RID),
						 lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}

	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, LOG_INTERVAL_RID),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, LOG_INTERVAL_RID),
						 lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}

	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, LOG_INTERVAL_ALARM_RID),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, LOG_INTERVAL_ALARM_RID),
						 lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}

	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, TX_INTERVAL_RID),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, TX_INTERVAL_RID),
						 lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}

	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, TX_INTERVAL_ALARM_RID),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, TX_INTERVAL_ALARM_RID),
						 lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}

	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, WAKE_EARLY_RID),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, WAKE_EARLY_RID),
						 lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}

	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, TX_DELAY_RID),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, TX_DELAY_RID),
						 lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}

	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, RX_DURATION_RID),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, RX_DURATION_RID),
						 lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}

	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, ETC_CFG_OBJ_R_TX_PROBE),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, ETC_CFG_OBJ_R_TX_PROBE),
						 lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}

	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, ETC_CFG_OBJ_R_LOCATION_REQ_INTERVAL),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
					       0, ETC_CFG_OBJ_R_LOCATION_REQ_INTERVAL),
					       lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}

	err = lwm2m_register_post_write_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
						 0, ETC_CFG_OBJ_R_GNSS_TIMEOUT),
						 callback);
	if (err) {
		return err;
	}
	err = lwm2m_register_validate_callback(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 
					       0, ETC_CFG_OBJ_R_GNSS_TIMEOUT),
					       lwm2m_codec_helpers_validate_config_cb);
	if (err) {
		return err;
	}

	return 0;
}

int lwm2m_codec_helpers_setup_resources(void)
{
	int err;

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0,
					   POWER_SOURCE_VOLTAGE_RID),
				&battery_voltage, sizeof(battery_voltage),
				sizeof(battery_voltage), LWM2M_RES_DATA_FLAG_RW);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0,
					   BATTERY_STATUS_RID),
				&battery_status, sizeof(battery_status),
				sizeof(battery_status), LWM2M_RES_DATA_FLAG_RW);
	if (err) {
		return err;
	}

	for (int i = 0; i <= SENSOR_INPUT_AMBIENT; i++) {
		err = lwm2m_set_res_buf(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 
						   i, TIMESTAMP_RID),
					&temperature_ts[i], sizeof(temperature_ts[i]),
					sizeof(temperature_ts[i]), LWM2M_RES_DATA_FLAG_RW);
		if (err) {
			return err;
		}
		err = lwm2m_set_res_buf(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 
						   i, SENSOR_UNITS_RID),
					TEMP_UNIT, (uint16_t)strlen(TEMP_UNIT) + 1,
					(uint16_t)strlen(TEMP_UNIT) + 1, LWM2M_RES_DATA_FLAG_RO);
		if (err) {
			return err;
		}
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 
					0, TIMESTAMP_RID),
			&humidity_ts, sizeof(humidity_ts),
			sizeof(humidity_ts), LWM2M_RES_DATA_FLAG_RW);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0,
					   SENSOR_UNITS_RID),
				HUMID_UNIT, (uint16_t)strlen(HUMID_UNIT) + 1,
				(uint16_t)strlen(HUMID_UNIT) + 1,
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_codec_helpers_setup_sensor_obj_values();
	if (err) {
		return err;
	}

	err = lwm2m_set_opaque(&LWM2M_OBJ(ETC_RELAY_OBJECT_ID, 0, ETC_RELAY_OBJ_R_DATA),
			       NULL, 0);
	if (err) {
		return err;
	}

	return 0;
}

int lwm2m_codec_helpers_setup_configuration_object(struct etc_config *cfg,
						   lwm2m_engine_set_data_cb_t callback)
{
	int err;

	err = lwm2m_set_u8(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, DEVICE_MODE_RID),
			   (uint8_t)cfg->device_mode);
	if (err) {
		return err;
	}

	err = lwm2m_set_u8(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, POWER_MODE_RID),
			   (uint8_t)cfg->power_mode);
	if (err) {
		return err;
	}

	err = lwm2m_set_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, LOG_INTERVAL_RID),
			    cfg->log_interval_secs);
	if (err) {
		return err;
	}

	err = lwm2m_set_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, LOG_INTERVAL_ALARM_RID),
			    cfg->log_interval_alarm_secs);
	if (err) {
		return err;
	}

	err = lwm2m_set_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, TX_INTERVAL_RID),
			    cfg->tx_interval_secs);
	if (err) {
		return err;
	}

	err = lwm2m_set_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, TX_INTERVAL_ALARM_RID),
			    cfg->tx_interval_alarm_secs);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, WAKE_EARLY_RID),
			    cfg->wake_early_secs);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, TX_DELAY_RID),
			    cfg->tx_delay_msec);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, RX_DURATION_RID),
			    cfg->rx_duration_secs);
	if (err) {
		return err;
	}

	err = lwm2m_set_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, ETC_CFG_OBJ_R_TX_PROBE),
			    cfg->tx_probe_secs);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, ETC_CFG_OBJ_R_LORA_PROBE_OFFSET),
			    cfg->lora_probe_offset_secs);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, ETC_CFG_OBJ_R_LTE_PROBE_OFFSET),
			    cfg->lte_probe_offset_secs);
	if (err) {
		return err;
	}
	
	err = lwm2m_set_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, ETC_CFG_OBJ_R_LOCATION_REQ_INTERVAL),
			    cfg->gnss_interval_secs);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, ETC_CFG_OBJ_R_GNSS_TIMEOUT),
			    cfg->gnss_timeout_secs);
	if (err) {
		return err;
	}

	err = lwm2m_codec_helpers_set_callback_for_config_object(callback);
	if (err) {
		return err;
	}

	return 0;
}

int lwm2m_codec_helpers_get_configuration_object(struct etc_config *cfg)
{
	int err;

	/* There has been a configuration update. Send callback to application with the latest
	 * state of the configuration.
	 */
	err = lwm2m_get_u8(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, DEVICE_MODE_RID),
			   &cfg->device_mode);
	if (err) {
		return err;
	}

	err = lwm2m_get_u8(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, POWER_MODE_RID),
			   &cfg->power_mode);
	if (err) {
		return err;
	}

	err = lwm2m_get_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, LOG_INTERVAL_RID),
		     	    &cfg->log_interval_secs);
	if (err) {
		return err;
	}
	
	err = lwm2m_get_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, LOG_INTERVAL_ALARM_RID),
		     	    &cfg->log_interval_alarm_secs);
	if (err) {
		return err;
	}

	err = lwm2m_get_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, TX_INTERVAL_RID),
		     	    &cfg->tx_interval_secs);
	if (err) {
		return err;
	}
		
	err = lwm2m_get_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, TX_INTERVAL_ALARM_RID),
		     	    &cfg->tx_interval_alarm_secs);
	if (err) {
		return err;
	}

	err = lwm2m_get_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, WAKE_EARLY_RID),
		     	    &cfg->wake_early_secs);
	if (err) {
		return err;
	}

	err = lwm2m_get_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, TX_DELAY_RID),
		     	    &cfg->tx_delay_msec);
	if (err) {
		return err;
	}

	err = lwm2m_get_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, RX_DURATION_RID),
		     	    &cfg->rx_duration_secs);
	if (err) {
		return err;
	}

	err = lwm2m_get_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, ETC_CFG_OBJ_R_TX_PROBE),
		     	    &cfg->tx_probe_secs);
	if (err) {
		return err;
	}
	
	err = lwm2m_get_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, ETC_CFG_OBJ_R_LORA_PROBE_OFFSET),
		     	    &cfg->lora_probe_offset_secs);
	if (err) {
		return err;
	}

	err = lwm2m_get_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, ETC_CFG_OBJ_R_LTE_PROBE_OFFSET),
		     	    &cfg->lte_probe_offset_secs);
	if (err) {
		return err;
	}

	err = lwm2m_get_u32(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, ETC_CFG_OBJ_R_LOCATION_REQ_INTERVAL),
		     	    &cfg->gnss_interval_secs);
	if (err) {
		return err;
	}
	
	err = lwm2m_get_u16(&LWM2M_OBJ(ETC_CFG_OBJECT_ID, 0, ETC_CFG_OBJ_R_GNSS_TIMEOUT),
		     	    &cfg->gnss_timeout_secs);
	if (err) {
		return err;
	}

	return 0;
}

int lwm2m_codec_helpers_set_modem_dynamic_data(struct data_modem_dynamic *modem_dynamic)
{
	int err;
	int64_t current_time;

	if (!modem_dynamic->queued) {
		return -ENODATA;
	}

	if (modem_dynamic->nw_mode == ACT_LTE_M) {
		err = lwm2m_set_u8(&LWM2M_OBJ(
				   LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
				   NETWORK_BEARER_ID),
				   LTE_FDD_BEARER);
		if (err) {
			return err;
		}
	} else if (modem_dynamic->nw_mode == ACT_NB_IOT) {
		err = lwm2m_set_u8(&LWM2M_OBJ(
				   LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
				   NETWORK_BEARER_ID),
				   NB_IOT_BEARER);
		if (err) {
			return err;
		}
	} else {
		return -EINVAL;
	}

	err = lwm2m_set_u32(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, CELLID),
			    modem_dynamic->cell);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, SMNC),
			    modem_dynamic->mnc);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, SMCC),
			    modem_dynamic->mcc);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, LAC),
			    modem_dynamic->area);
	if (err) {
		return err;
	}

	err = lwm2m_set_s16(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, RSS),
			    modem_dynamic->rsrp);
	if (err) {
		return err;
	}
	
	err = lwm2m_set_s16(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, QUAL),
			   modem_dynamic->qual);

	err = date_time_now(&current_time);
	if (err) {
		return err;
	}

	err = lwm2m_set_time(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, CURRENT_TIME_RID),
			     (current_time / MSEC_PER_SEC));
	if (err) {
		return err;
	}

	return 0;
}

int lwm2m_codec_helpers_set_device_data(void)
{
	int err;
	uint8_t pubkey_id[IMG_PUBKEY_ID_LEN];

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, MODEL_NUMBER_RID),
				CONFIG_CLOUD_CODEC_MODEL,
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_MODEL) + 1,
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_MODEL) + 1,
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, MANUFACTURER_RID),
				CONFIG_CLOUD_CODEC_MANUFACTURER,
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_MANUFACTURER) + 1,
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_MANUFACTURER) + 1,
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, DEVICE_TYPE_RID),
				CONFIG_CLOUD_CODEC_DEVICE_TYPE,
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_DEVICE_TYPE) + 1,
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_DEVICE_TYPE) + 1,
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, FIRMWARE_VERSION_RID),
				APP_VERSION_STRING,
				(uint16_t)strlen(APP_VERSION_STRING) + 1,
				(uint16_t)strlen(APP_VERSION_STRING) + 1,
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}
	
	etc_get_hw_version(hardware_version, sizeof(hardware_version));

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, HARDWARE_VERSION_RID),
				hardware_version,
				(uint16_t)strlen(hardware_version) + 1,
				(uint16_t)strlen(hardware_version) + 1,
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	etc_get_device_id(device_id, sizeof(device_id));

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0,
				DEVICE_SERIAL_NUMBER_ID),
				device_id,
				(uint16_t)strlen(device_id) + 1,
				(uint16_t)strlen(device_id) + 1,
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	etc_device_get_img_pubkey_id(pubkey_id, sizeof(pubkey_id));
	err = lwm2m_set_opaque(&LWM2M_OBJ(ETC_INFO_OBJECT_ID, 0,
					  ETC_INFO_OBJ_R_IMG_PUBKEY_ID),
			       pubkey_id, sizeof(pubkey_id));
	if (err) {
		return err;
	}

	return 0;
}

int lwm2m_codec_helpers_update_location(struct cloud_codec_data *cloud_data,
					struct etc_gnss_data *data)
{
	int err;

	err = lwm2m_set_f64(&LWM2M_OBJ(ETC_LOCATION_OBJ_ID, 0, ETC_LOCATION_OBJ_R_LATITUDE),
			    NANODEGREE_TO_DEGREE(data->latitude));
	if (err) {
		return err;
	}
	err = lwm2m_set_f64(&LWM2M_OBJ(ETC_LOCATION_OBJ_ID, 0, ETC_LOCATION_OBJ_R_LONGITUDE),
			    NANODEGREE_TO_DEGREE(data->longitude));
	if (err) {
		return err;
	}
	err = lwm2m_set_time(&LWM2M_OBJ(ETC_LOCATION_OBJ_ID, 0, TIMESTAMP_RID),
			     data->timestamp);
	if (err) {
		return err;
	}

	if (cloud_data != NULL) {
		err = lwm2m_codec_helpers_object_path_list_add(
			cloud_data, &LWM2M_OBJ(ETC_LOCATION_OBJ_ID, 0), 1);
	}

	return 0;
}

int lwm2m_codec_helpers_update_location_dummy(struct cloud_codec_data *cloud_data)
{
	struct etc_gnss_data dummy_location = {
		.latitude = 43670059200,
		.longitude = -79434526100
	};
	int err;
	int64_t time_now;

	date_time_now(&time_now);
	/* Convert time from ms to s */
	time_now /= 1000;
	dummy_location.timestamp = time_now;

	return lwm2m_codec_helpers_update_location(cloud_data, &dummy_location);
}

int lwm2m_codec_helpers_set_modem_static_data(struct data_modem_static *modem_static)
{
	int err;

	if (!modem_static->queued) {
		return -ENODATA;
	}

	err = lwm2m_set_string(&LWM2M_OBJ(ETC_INFO_OBJECT_ID, 0, ETC_INFO_OBJ_R_IMEI),
			       modem_static->imei);
	if (err) {
		return err;
	}

	err = lwm2m_set_string(&LWM2M_OBJ(ETC_INFO_OBJECT_ID, 0, ETC_INFO_OBJ_R_MODEM_REV),
			       modem_static->fw);
	if (err) {
		return err;
	}

	err = lwm2m_set_string(&LWM2M_OBJ(ETC_INFO_OBJECT_ID, 0, ETC_INFO_OBJ_R_IMSI),
			       modem_static->imsi);
	if (err) {
		return err;
	}

	err = lwm2m_set_string(&LWM2M_OBJ(ETC_INFO_OBJECT_ID, 0, ETC_INFO_OBJ_R_ICCID),
			       modem_static->iccid);
	if (err) {
		return err;
	}

	return 0;
}

int lwm2m_codec_helpers_set_relay_data(const uint8_t *data, uint16_t data_len)
{
	int err;

	err = lwm2m_set_opaque(&LWM2M_OBJ(ETC_RELAY_OBJECT_ID, 0, ETC_RELAY_OBJ_R_DATA),
			       data, data_len);

	return err;
}

int lwm2m_codec_helpers_set_relay_legacy_data(const char *data, uint16_t data_len)
{
	int err;

	if (strlen(data) != data_len) {
		ETC_MEMFAULT_TRACE_EVENT(lengths_dont_match);
		LOG_WRN("String length does no match data length");
	}
	
	err = lwm2m_set_string(&LWM2M_OBJ(ETC_RELAY_OBJECT_ID, 0, ETC_RELAY_OBJ_R_LEGACY_DATA),
			       data);

	return err;
}

/** Invalidate the current sensor value.
 * 
 * @retval 1 value invalidated and changed
 * @retval 0 value was already invalidated
 * @retval <0 error
 * 
*/
static int invalidate_temp_sensor_value(struct cloud_codec_data *cloud_data,
				   int obj_inst_id,
				   time_t timestamp)
{
	int err;
	double val = NAN;

	lwm2m_get_f64(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, obj_inst_id, SENSOR_VALUE_RID),
		      &val);
		      
	if (!isnan(val)) {
		err = lwm2m_set_f64(
			&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, obj_inst_id, SENSOR_VALUE_RID),
			NAN);
		if (err) {
			return err;
		}
		
		err = lwm2m_set_time(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, obj_inst_id, TIMESTAMP_RID),
				     timestamp);
		if (err) {
			return err;
		}
		return 1;
	}

	return 0;
}

static int invalidate_humid_sensor_value(struct cloud_codec_data *cloud_data, const struct lwm2m_obj_path *path,
	time_t timestamp)
{
	int err;
	double val = NAN;

	lwm2m_get_f64(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0, SENSOR_VALUE_RID),
		      &val);
		      
	if (!isnan(val)) {
		err = lwm2m_set_f64(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0, SENSOR_VALUE_RID), NAN);
		if (err) {
			return err;
		}
					
		err = lwm2m_set_time(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0, TIMESTAMP_RID), timestamp);
		if (err) {
			return err;
		}

		err = lwm2m_set_s8(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0, ETC_HUMID_OBJ_R_PORT), -1);
		if (err) {
			return err;
		}

		err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
							       path, 1);
		if (err) {
			LOG_ERR("Failed populating object path list, error: %d", err);
			return err;
		}
	}

	return 0;
}

static inline int set_resource_if_changed_s32(struct cloud_codec_data *cloud_data,
					      const struct lwm2m_obj_path *path,
					      int32_t new_value)
{
	int32_t value;
	int err;

	err = lwm2m_get_s32(path, &value);
	if (err) {
		return -1;
	}
	if (value != new_value) {
		err = lwm2m_set_s32(path,
				    new_value);
		lwm2m_codec_helpers_object_path_list_add(cloud_data,
							 path,
							 1);
	}
	return err;
}

/** Set current values of a temperature object instance.
 * 
 * @retval 1 Object instance updated successfully.
 * @retval 0 Temperature is invalid
 * @retval <0 error
*/
static int set_temperature(int instance_id, float value, int64_t timestamp)
{
	int err;

	if (!data_codec_compare_temperature_is_valid(value)) {
		return 0;
	}
	
	err = lwm2m_set_time(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, instance_id, TIMESTAMP_RID),
			(time_t)(timestamp));
	if (err) {
		return err;
	}
	err = lwm2m_set_f64(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, instance_id, SENSOR_VALUE_RID),
				value);
	if (err) {
		return err;
	}

	LOG_DBG("temp inst: %d, value: %.2f", instance_id, value);

	return 1;
}

int lwm2m_codec_helpers_set_sensor_data(struct cloud_codec_data *cloud_data,
					union etc_device_record *record)
{
	int err;

	const struct lwm2m_obj_path humid_path_list[] = {
		LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0),
	};
	
	/* Set battery voltage in mV (required by resource spec) */
	err = set_resource_if_changed_s32(cloud_data,
					  &LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, POWER_SOURCE_VOLTAGE_RID),
					  (int32_t)roundf(record->battery * 1000.0));
	if (err) {
		return err;
	}

	/* Set the battery status */
	err = set_resource_if_changed_s32(cloud_data,
					  &LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, BATTERY_STATUS_RID),
					  (int32_t)record->flag);
	if (err) {
		return err;
	}

	/* Set ambient temperature and timestamp */
	err = lwm2m_set_time(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 0, TIMESTAMP_RID),
			(time_t)(record->timestamp));
	if (err) {
		
		return err;
	}
	err = lwm2m_set_f64(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 0, SENSOR_VALUE_RID),
			    record->sensor[SENSOR_INPUT_AMBIENT]);
	if (err) {
		return err;
	}

	if (data_codec_compare_humidity_is_valid(record->sensor[SENSOR_INPUT_HUMID])) {
		err = lwm2m_set_time(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0, TIMESTAMP_RID), 
				     (time_t)(record->timestamp));
		if (err) {
			return err;
		}

		err = lwm2m_set_f64(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0, SENSOR_VALUE_RID),
				    record->sensor[SENSOR_INPUT_HUMID]);
		if (err) {
			return err;
		}

		err = lwm2m_set_s8(&LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0,
					      ETC_HUMID_OBJ_R_PORT), 
				   etc_sensor_get_probe_humid_index());
		if (err) {
			return err;
		}

		err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
							       humid_path_list,
							       ARRAY_SIZE(humid_path_list));
		if (err) {
			LOG_ERR("Failed populating object path list, error: %d", err);
			return err;
		}

	} else {
		invalidate_humid_sensor_value(cloud_data, humid_path_list, (time_t)(record->timestamp));
	}

	/* Set external sensor temperature and timestamp */
	for (int i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		int obj_inst_id = i + 1;
		const struct lwm2m_obj_path path_list[] = {
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, obj_inst_id),
		};
		
		err = set_temperature(obj_inst_id, record->sensor[i], record->timestamp);
		if (err == 0) {
			err = invalidate_temp_sensor_value(cloud_data, obj_inst_id,
							   (time_t)(record->timestamp));
		}

		/* Add path to temperature object if value changed. */
		if (err == 1) {
			err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
									path_list,
									ARRAY_SIZE(path_list));
			if (err) {
				LOG_ERR("Failed populating object path list, error: %d", err);
			}
			continue;
		}
	}

	return 0;
}

int lwm2m_codec_helpers_update_functional_test(struct cloud_codec_data *cloud_data,
					       struct sensor_data *sensor_data,
					       int modem_rsrp,
					       enum functional_test_result result)
{
	int ret;

	for (int i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		int obj_inst_id = i + 1;
		const struct lwm2m_obj_path paths[] = {
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, obj_inst_id, SENSOR_VALUE_RID),
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, obj_inst_id, TIMESTAMP_RID)
		};

		ret = set_temperature(obj_inst_id, sensor_data->sensor[i],
				      sensor_data->timestamp);
		if (ret < 0) {
			LOG_ERR("set temperature");
			return ret;
		}

		lwm2m_codec_helpers_object_path_list_add(cloud_data,
							 paths, ARRAY_SIZE(paths));
	}

	
	const struct lwm2m_obj_path path_list[] = {
		LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, RSS),
		LWM2M_OBJ(ETC_FUNCTIONAL_TEST_OBJECT_ID, 0, ETC_FUNCTIONAL_TEST_OBJ_R_STATUS),
		LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, DEVICE_SERIAL_NUMBER_ID),
		LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, POWER_SOURCE_VOLTAGE_RID)
	};

	/* Set battery voltage in mV (required by resource spec) */
	ret = set_resource_if_changed_s32(cloud_data,
					  &LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, POWER_SOURCE_VOLTAGE_RID),
					  (int32_t)sensor_data->battery_mV);
	if (ret) {
		return ret;
	}

	ret = lwm2m_set_s16(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, RSS),
			    modem_rsrp);
	if (ret) {
		return ret;
	}
	ret = lwm2m_set_u8(&LWM2M_OBJ(ETC_FUNCTIONAL_TEST_OBJECT_ID, 0, ETC_FUNCTIONAL_TEST_OBJ_R_STATUS),
			   result);
	if (ret) {
		return ret;
	}
	lwm2m_codec_helpers_object_path_list_add(cloud_data,
						 path_list, ARRAY_SIZE(path_list));

	return 0;
}

bool lwm2m_codec_helpers_update_reclaim_state(struct cloud_codec_data *cloud_data,
					      enum data_reclaim_state new_state)
{
	uint8_t lwm2m_reclaim_state;
	const struct lwm2m_obj_path reclaim_state_path = 
		LWM2M_OBJ(ETC_RECLAIM_OBJECT_ID, 0, ETC_RECLAIM_OBJ_R_STATUS);
	int err;

	switch (new_state) {
	case RECLAIM_IDLE:
		lwm2m_reclaim_state = ETC_RECLAIM_STATUS_IDLE;
		break;
	case RECLAIM_IN_PROGRESS:
		lwm2m_reclaim_state = ETC_RECLAIM_STATUS_IN_PROGRESS;
		break;

	case RECLAIM_SUCCESS:
		lwm2m_reclaim_state = ETC_RECLAIM_STATUS_COMPLETE;
		break;

	case RECLAIM_ERROR:
		lwm2m_reclaim_state = ETC_RECLAIM_STATUS_ERROR;
		break;
	
	default:
		return false;
	}

	err = lwm2m_set_u8(&reclaim_state_path, lwm2m_reclaim_state);
	if (err) {
		return false;
	}
	err = lwm2m_codec_helpers_object_path_list_add(cloud_data, 
						       &reclaim_state_path,
						       1);
	if (err) {
		return false;
	}

	return true;
}

/**
 * @return If success, index copied into place
 *         If no valid entry found: -1.
*/
static int move_valid_entry(struct lwm2m_obj_path *path_list, size_t list_size)
{
	/* Skip first (invalid) entry*/
	for (int i = 1; i < list_size; i++) {
		if (path_list[i].level > 0) {			
			path_list[0].obj_id = path_list[i].obj_id;
			path_list[0].obj_inst_id = path_list[i].obj_inst_id;
			path_list[0].res_id = path_list[i].res_id;
			path_list[0].res_inst_id = path_list[i].res_inst_id;
			path_list[0].level = path_list[i].level;

			path_list[i].level = 0;
			
			return i;
		}
	}

	return -1;
}

static void lwm2m_codec_helpers_object_path_list_remove_invalids(struct cloud_codec_data *cloud_data)
{
	int ret;

	for (int i = 0; i < cloud_data->valid_object_paths; i++) {
		/* If invalid entry is found, move next valid entry in its place. */
		if (cloud_data->paths[i].level == 0) {
			ret = move_valid_entry(&cloud_data->paths[i],
					       cloud_data->valid_object_paths - i);
			/* Set new size and exit if either no valid entry found
			   or if last entry was copied into place. */
			if (ret == -1 || 
			    (ret + i + 1) == cloud_data->valid_object_paths) {
				cloud_data->valid_object_paths = i + 1;
				return;
			}
		}
	}
}

void lwm2m_codec_helpers_path_list_log(const struct lwm2m_obj_path path_list[],
				       uint8_t path_list_size)
{
	__ASSERT_NO_MSG(path_list);
	char buf[LWM2M_MAX_PATH_STR_SIZE];

	LOG_INF("Path list:");
	for (int i = 0; i < path_list_size; i++) {
		if (path_list->level == 0) {
			return;
		}
		lwm2m_path_log_buf(buf, (struct lwm2m_obj_path*)&path_list[i]);
		LOG_DBG("%s", buf);
	}
}

int lwm2m_codec_helpers_object_path_list_is_empty(struct cloud_codec_data *data)
{
	__ASSERT_NO_MSG(data != NULL);

	if (data->valid_object_paths == 0) {
		return 1;
	}

	return 0;
}

int lwm2m_codec_helpers_object_path_list_clear(struct cloud_codec_data *output)
{
	if (output == NULL) {
		return -EINVAL;
	}

	memset(output->paths, 0, sizeof(output->paths));
	output->valid_object_paths = 0;

	return 0;
}

int lwm2m_codec_helpers_object_path_list_add(struct cloud_codec_data *output,
					     const struct lwm2m_obj_path path[],
					     size_t path_size)
{
	bool path_added = false;
	int added_cnt = 0;
	int new_paths_idx[path_size];
	int new_paths_len = 0;

	if (output == NULL || path == NULL || path_size == 0) {
		return -EINVAL;
	}

	/* Loop through paths to find new paths that are already in the output
	 * path list */
	if (output->valid_object_paths > 0) {
		for (int i = 0; i < path_size; i++) {
			bool new_path = true;

			/* Compare existing paths to the path that needs to be added
			 * and check if they are the identical. If they are identical,
			 * skip comparing more paths. */
			for (int j = 0; j < output->valid_object_paths; j++) {
				if (memcmp(&path[i], &output->paths[j], 
					   sizeof(path[i])) == 0) {
					new_path = false;
					break;
				}
			}

			/* Populate new path in index list, if valid */
			if (new_path && (path[i].level > 0)) {
				new_paths_idx[new_paths_len] = i;
				new_paths_len++;
			}
		}
	} else {
		/* Populate new_paths_idx with all new path indices, if path is valid. */
		for (int i = 0; i < path_size; i++) {
			if (path[i].level > 0) {
				new_paths_idx[new_paths_len] = i;
				new_paths_len++;
			}
		}
	}

	if (new_paths_len == 0) {
		return 0;
	}

	/* Add new paths to output paths */
	for (int i = 0; i < ARRAY_SIZE(output->paths); i++) {
		if (output->paths[i].level == 0) {
			int path_idx = new_paths_idx[added_cnt];

			output->paths[i].obj_id = path[path_idx].obj_id;
			output->paths[i].obj_inst_id = path[path_idx].obj_inst_id;
			output->paths[i].res_id = path[path_idx].res_id;
			output->paths[i].res_inst_id = path[path_idx].res_inst_id;
			output->paths[i].level = path[path_idx].level;

			output->valid_object_paths++;
			path_added = true;
			added_cnt++;

			if (added_cnt == new_paths_len) {
				break;
			}
		}
	}

	/* This API can be called multiple times to populate the same path buffer. Due to this we
	 * also check if all entries in the incoming path[] list was added.
	 */
	if (!path_added || (added_cnt != new_paths_len)) {
		return -ENOMEM;
	}

	return 0;
}

static inline bool is_path_measurement(struct lwm2m_obj_path *path)
{
	__ASSERT_NO_MSG(path != NULL);

	if ((path->level > 0) &&
	    ((path->obj_id == ETC_TEMP_OBJECT_ID) ||
	     (path->obj_id == ETC_HUMID_OBJECT_ID))) {
		return true;
	}

	return false;
}

bool lwm2m_codec_helpers_object_path_list_contains_measurement(struct cloud_codec_data *cloud_data)
{
	__ASSERT_NO_MSG(cloud_data != NULL);

	for (int i = 0; i < cloud_data->valid_object_paths; i++) {
		if (is_path_measurement(&cloud_data->paths[i])) {
			return true;
		}
	}

	return false;
}

int lwm2m_codec_helpers_object_path_list_move(struct cloud_codec_data *cloud_data,
					      struct cloud_codec_data *backup_data)
{
	int ret;

	ret = lwm2m_codec_helpers_object_path_list_add(backup_data,
						       cloud_data->paths,
						       cloud_data->valid_object_paths);
	if (ret != 0) {
		return -ENOMEM;
	}

	lwm2m_codec_helpers_object_path_list_clear(cloud_data);
	
	return 0;
}

int lwm2m_codec_helpers_object_path_list_split(struct cloud_codec_data *cloud_data,
					       struct cloud_codec_data *backup_data)
{
	struct lwm2m_obj_path 
		path_list[CONFIG_CLOUD_CODEC_LWM2M_PATH_LIST_ENTRIES_MAX] = { 0 };
	size_t path_size = 0;
	int ret;


	/* Loop through valid paths and pick out any paths that do not contain
	 * measurement data, copy them into the temporary path list, and invalidate
	   them by setting the level to 0. */
	for (int i = 0; i < cloud_data->valid_object_paths; i++) {
		if (!is_path_measurement(&cloud_data->paths[i])) {
			path_list[path_size].obj_id = cloud_data->paths[i].obj_id;
			path_list[path_size].obj_inst_id = cloud_data->paths[i].obj_inst_id;
			path_list[path_size].res_id = cloud_data->paths[i].res_id;
			path_list[path_size].res_inst_id = cloud_data->paths[i].res_inst_id;
			path_list[path_size].level = cloud_data->paths[i].level;

			/* Invalidate/remove path from list by setting its level 
			   to 0. */
			cloud_data->paths[i].level = 0;

			path_size++;

			/* Exit when at least half the entries have been removed or
			   when path_list array is full. */
			if (path_size == sizeof(path_list) || 
			    path_size >= round_int(cloud_data->valid_object_paths, 2U) / 2U) {
				break;
			}
		}
	}

	/* Recalculate cloud_data->paths length */
	for (int i = cloud_data->valid_object_paths - 1; i >= 0; i--) {
		if (cloud_data->paths[i].level == 0) {
			cloud_data->valid_object_paths--;
		} else {
			break;
		}
	}
	lwm2m_codec_helpers_object_path_list_remove_invalids(cloud_data);

	ret = lwm2m_codec_helpers_object_path_list_add(backup_data, path_list, path_size);
	if (ret != 0) {
		return -ENOMEM;
	}

	return 0;
}
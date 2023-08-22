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
#include "etc_settings.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lwm2m_codec_helpers, CONFIG_CLOUD_CODEC_LOG_LEVEL);

/* Some resources does not have designated buffers. Therefore we define those in here. */
static uint8_t bearers[2] = { LTE_FDD_BEARER, NB_IOT_BEARER };
static int battery_voltage;
static time_t button_ts;

static char device_id[ETC_SETTINGS_DEVICE_ID_LEN];
static char hardware_version[ETC_SETTING_HW_VER_LEN];

/* Timestamps, minimum, and maximum values for the BME680 present on the Thingy:91. */
static double temp_min_range_val = TEMP_MIN_RANGE_VALUE;
static double temp_max_range_val = TEMP_MAX_RANGE_VALUE;
static double humid_min_range_val = HUMID_MIN_RANGE_VALUE;
static double humid_max_range_val = HUMID_MAX_RANGE_VALUE;
static time_t temperature_ts[SENSOR_INPUT_MAX];
static time_t humidity_ts;

static int lwm2m_codec_helpers_setup_sensor_obj_values(void)
{
	int err;
	
	for (int i = 0; i < SENSOR_INPUT_MAX; i++)
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
	err = lwm2m_set_f64(&LWM2M_OBJ(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 0,
				       MIN_RANGE_VALUE_RID),
			    humid_min_range_val);
	if (err) {
		return err;
	}

	err = lwm2m_set_f64(&LWM2M_OBJ(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 0,
				       MAX_RANGE_VALUE_RID),
			    humid_max_range_val);
	if (err) {
		return err;
	}

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
	}

	return rc;
}

int lwm2m_codec_helpers_create_objects_and_resources(void)
{
	int err;

	for (int i = 0; i < SENSOR_INPUT_MAX; i++) {
		err = lwm2m_create_object_inst(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID,
							  i));
		if (err) {
			return err;
		}
	}

	err = lwm2m_create_object_inst(
				&LWM2M_OBJ(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 0));
	if (err) {
		return err;
	}
	
	err = lwm2m_create_res_inst(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
						AVAIL_NETWORK_BEARER_ID, 0));
	if (err) {
		return err;
	}

	err = lwm2m_create_res_inst(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
						IP_ADDRESSES, 0));
	if (err) {
		return err;
	}

	err = lwm2m_create_res_inst(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
						APN, 0));
	if (err) {
		return err;
	}
	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
					   APN, 0), 
				CONFIG_MODEM_QUECTEL_BG95_M3_APN, 
				sizeof(CONFIG_MODEM_QUECTEL_BG95_M3_APN) - 1,
				sizeof(CONFIG_MODEM_QUECTEL_BG95_M3_APN) - 1,
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

	for (int i = 0; i < SENSOR_INPUT_MAX; i++) {
		err = lwm2m_set_res_buf(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 
						   i, TIMESTAMP_RID),
					&temperature_ts[i], sizeof(temperature_ts[i]),
					sizeof(temperature_ts[i]), LWM2M_RES_DATA_FLAG_RW);
		if (err) {
			return err;
		}
		err = lwm2m_set_res_buf(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 
						   i, SENSOR_UNITS_RID),
					TEMP_UNIT, (uint16_t)strlen(TEMP_UNIT),
					(uint16_t)strlen(TEMP_UNIT), LWM2M_RES_DATA_FLAG_RO);
		if (err) {
			return err;
		}
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 
					0, TIMESTAMP_RID),
			&humidity_ts, sizeof(humidity_ts),
			sizeof(humidity_ts), LWM2M_RES_DATA_FLAG_RW);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 0,
					   SENSOR_UNITS_RID),
				HUMID_UNIT, (uint16_t)strlen(HUMID_UNIT),
				(uint16_t)strlen(HUMID_UNIT),
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

#if 0
	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID,
					   0, IP_ADDRESSES, 0),
				modem_dynamic->ip, (uint16_t)strlen(modem_dynamic->ip),
				(uint16_t)strlen(modem_dynamic->ip),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}
#endif

	err = lwm2m_set_s8(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, RSS),
			   (int8_t)modem_dynamic->rsrp);
	if (err) {
		return err;
	}
	
	err = lwm2m_set_u8(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, QUAL),
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

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, MODEL_NUMBER_RID),
				CONFIG_CLOUD_CODEC_MODEL,
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_MODEL),
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_MODEL),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, MANUFACTURER_RID),
				CONFIG_CLOUD_CODEC_MANUFACTURER,
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_MANUFACTURER),
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_MANUFACTURER),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, DEVICE_TYPE_RID),
				CONFIG_CLOUD_CODEC_DEVICE_TYPE,
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_DEVICE_TYPE),
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_DEVICE_TYPE),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, FIRMWARE_VERSION_RID),
				APP_VERSION_NUM_STR,
				(uint16_t)strlen(APP_VERSION_NUM_STR),
				(uint16_t)strlen(APP_VERSION_NUM_STR),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}
	
	etc_get_hw_version(hardware_version, sizeof(hardware_version));

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, HARDWARE_VERSION_RID),
				hardware_version,
				(uint16_t)strlen(hardware_version),
				(uint16_t)strlen(hardware_version),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	etc_get_device_id(device_id, sizeof(device_id));

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0,
				DEVICE_SERIAL_NUMBER_ID),
				device_id,
				(uint16_t)strlen(device_id),
				(uint16_t)strlen(device_id),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	return 0;
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

static int invalidate_sensor_value(struct cloud_codec_data *cloud_data,
				   int obj_inst_id, const struct lwm2m_obj_path *path,
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
		err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
				path,
				1);
		if (err) {
			LOG_ERR("Failed populating object path list, error: %d", err);
			return err;
		}
	}

	return 0;
}

int lwm2m_codec_helpers_set_sensor_data(struct cloud_codec_data *cloud_data,
					union etc_device_record *record)
{
	int err;

	/* Set battery voltage in mV (required by resource spec) */
	err = lwm2m_set_s32(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, POWER_SOURCE_VOLTAGE_RID),
			    (int32_t)roundf(record->battery * 1000.0));
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

	/* Set external sensor temperature and timestamp */
	for (int i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		int obj_inst_id = i + 1;
		const struct lwm2m_obj_path path_list[] = {
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, obj_inst_id),
		};
		
		if (!data_codec_compare_temperature_is_valid(record->sensor[i])) {
			invalidate_sensor_value(cloud_data, obj_inst_id, path_list,
						(time_t)(record->timestamp));
			continue;
		}
		
		err = lwm2m_set_time(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, obj_inst_id, TIMESTAMP_RID),
				(time_t)(record->timestamp));
		if (err) {
			return err;
		}
		err = lwm2m_set_f64(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, obj_inst_id, SENSOR_VALUE_RID),
				    record->sensor[i]);
		if (err) {
			return err;
		}

		err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
							       path_list,
							       ARRAY_SIZE(path_list));
		if (err) {
			LOG_ERR("Failed populating object path list, error: %d", err);
			return err;
		}
	}

	return 0;
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
		lwm2m_path_log_buf(buf, &path_list[i]);
		LOG_DBG("%s", buf);
	}
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

			/* Populate new path in index list */
			if (new_path) {
				new_paths_idx[new_paths_len] = i;
				new_paths_len++;
			}
		}
	} else {
		/* Populate new_paths_idx with all new path indices */
		for (int i = 0; i < path_size; i++) {
			new_paths_idx[i] = i;
		}
		new_paths_len = path_size;
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

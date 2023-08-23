/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef LWM2M_CODEC_DEFINES_H__
#define LWM2M_CODEC_DEFINES_H__

#include "etc_cfg_obj_48931.h"
#include "etc_temp_obj_48932.h"
#include "etc_info_obj_48933.h"
#include "etc_relay_obj_48935.h"
#include "etc_humid_obj_48936.h"

/* LwM2M read-write flag. */
#define LWM2M_RES_DATA_FLAG_RW 0

/* Connectivity monitoring object RIDs. */
#define NETWORK_BEARER_ID		0
#define AVAIL_NETWORK_BEARER_ID		1
/* Radio Signal Strength */
#define RSS				2
/* Radio signal quality */
#define QUAL                            3
#define IP_ADDRESSES			4
#define APN				7
#define CELLID				8
#define SMNC				9
#define SMCC				10
#define LAC				12

/* Device object RIDs. */
#define FIRMWARE_VERSION_RID		3
#define SOFTWARE_VERSION_RID		19
#define DEVICE_SERIAL_NUMBER_ID		2
#define CURRENT_TIME_RID		13
#define POWER_SOURCE_VOLTAGE_RID	7
#define MODEL_NUMBER_RID		1
#define DEVICE_TYPE_RID                 17
#define MANUFACTURER_RID		0
#define HARDWARE_VERSION_RID		18

#define DEVICE_MODE_RID		1
#define POWER_MODE_RID		2
#define TX_INTERVAL_RID		3
#define LOG_INTERVAL_RID	4
#define LOG_INTERVAL_ALARM_RID	5
#define TX_DELAY_RID		6
#define WAKE_EARLY_RID		7
#define RX_DURATION_RID		8
#define TX_INTERVAL_ALARM_RID   9

/* LTE-FDD (LTE-M) bearer & NB-IoT bearer. */
#define LTE_FDD_BEARER 6U
#define NB_IOT_BEARER 7U

/* Temperature sensor metadata. */
#define TEMP_MIN_RANGE_VALUE -40.0
#define TEMP_MAX_RANGE_VALUE 120.0
#define TEMP_UNIT "Cel"

/* Humidity sensor metadata. */
#define HUMID_MIN_RANGE_VALUE 0.0
#define HUMID_MAX_RANGE_VALUE 100.0
#define HUMID_UNIT "%"


#endif /* LWM2M_CODEC_DEFINES_H */

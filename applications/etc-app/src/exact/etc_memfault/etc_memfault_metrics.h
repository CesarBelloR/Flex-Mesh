/*
 * Copyright (c) 2023 EXACT Technology
 *
 */
#ifndef ETC_MEMFAULT_METRICS_H_
#define ETC_MEMFAULT_METRICS_H_

#include "events/sensor_event.h"

/**
 * Add the charging status metric to Memfault.
 * 
 * @param evt Sensor evt type that contains charging status.
 * One of:
 * - SENSOR_EVT_BATTERY_IN_NORMAL
 * - SENSOR_EVT_BATTERY_IN_CHARGING
 * - SENSOR_EVT_BATTERY_CHARGE_COMPLETE
*/
void etc_mflt_metrics_charging(enum sensor_event_type evt);

/**
 * Record modem network metrics.
 * 
 * @param mcc Current mobile network country code.
 * @param mnc Current mobile network network code.
 * @param rsrp Current modem RSRP.
 * @param rsrq Current modem RSRQ.
*/
void etc_mflt_metrics_modem_network(uint16_t mcc, uint16_t mnc, int rsrp, int rsrq);

/**
 * Set modem time to connect metric.
 * 
 * @param time_to_connect_ms Time that it took the modem to make
 * a connection to an LTE network in ms.
*/
void etc_mflt_metrics_modem_conn_time(int64_t time_to_connect_ms);

/**
 * Set modem on time metric.
 * 
 * @param on_time_ms Last modem total on time in ms.
*/
void etc_mflt_metrics_modem_on_time(int64_t on_time_ms);

/**
 * To be called when a send was successful. Keeps track of number of successful
 * messages sent within a heartbeat interval.
*/
void etc_mflt_metrics_send_successful(void);

/**
 * To be called when a send failed. Keeps track of number of failed
 * messages within a heartbeat interval.
*/
void etc_mflt_metrics_send_failed(void);

/**
 * To be called when an OTA attempt is started.
*/
void etc_mflt_metrics_ota_started(void);

/**
 * To be called when an OTA attempt failed.
*/
void etc_mflt_metrics_ota_failed(void);

/**
 * Set the device_img_pubkey_id attribute to the currently used
 * image public key ID.
*/
void etc_mflt_metrics_init_img_pubkey_id(void);

#endif /* ETC_MEMFAULT_METRICS_H_ */
/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <time.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "etc_date_time.h"

LOG_MODULE_REGISTER(lwm2m_senml_cbor_time, CONFIG_CLOUD_INTEGRATION_LOG_LEVEL);

double lwm2m_rw_senml_cbor_basetime(void)
{
	int64_t now_ms;

	if (date_time_now(&now_ms) != 0) {
		return 0.0;
	}

	time_t now_sec = (time_t)(now_ms / MSEC_PER_SEC);
	struct tm tm_utc;
	char iso_time[sizeof("YYYY-MM-DDTHH:MM:SS")];

	gmtime_r(&now_sec, &tm_utc);
	strftime(iso_time, sizeof(iso_time), "%Y-%m-%dT%H:%M:%S", &tm_utc);
	LOG_INF("SenML CBOR basetime: %s.%03lldZ", iso_time, (long long)(now_ms % MSEC_PER_SEC));

	return (double)now_ms / (double)MSEC_PER_SEC;
}

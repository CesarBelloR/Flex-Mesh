#ifndef _SENSOR_EVENT_H_
#define _SENSOR_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_EVENT_NUM_DEV_MAX 5
#define SENSOR_NTC_NO_CONNECTED -273.150

/** @brief Sensor event types submitted by the Sensor module. */
enum sensor_event_type {
	SENSOR_EVT_ENVIRONMENTAL_DATA_READY,
	SENSOR_EVT_ENVIRONMENTAL_NOT_SUPPORTED,
	SENSOR_EVT_HALL_DATA_READY,
	SENSOR_EVT_SHUTDOWN_READY,
	SENSOR_EVT_ERROR,
};

/** @brief Structure used to provide environmental data. */
struct sensor_data {
	/** Uptime when the data was sampled. */
	int64_t timestamp;
	/** Temperature in Celsius degrees. */
	float temperature[SENSOR_EVENT_NUM_DEV_MAX];
};

struct sensor_event {
	struct app_event_header header;

	enum sensor_event_type type;
	union {
		/** Code signifying the cause of error. */
		int err;
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		struct sensor_data sensors;
	} data;
};

APP_EVENT_TYPE_DECLARE(sensor_event);

#ifdef __cplusplus
}
#endif

#endif /* _SENSOR_EVENT_H_ */
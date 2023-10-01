#ifndef _SENSOR_EVENT_H_
#define _SENSOR_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Temperature value when no probe */
#define SENSOR_TEMP_NO_CONNECTED -273.150
/* Humid value when no probe */
#define SENSOR_HUMID_NO_CONNECTED -1.0
/* Minimum sensor temperature that is a valid reading. */
#define SENSOR_TEMP_C_MIN	-40.0f
/* Maximum sensor temperature that is a valid reading. */
#define SENSOR_TEMP_C_MAX	120.0f
/* Minimum sensor humidity that is a valid reading. */
#define SENSOR_HUMID_C_MIN	0.0f
/* Maximum sensor humidity that is a valid reading. */
#define SENSOR_HUMID_C_MAX	100.0f
/* No probe connected in DVT2 - etc@0.3.0 */
#define SENSOR_ADC_NO_CONNECTED 4090
/* One-wire probe connected in DVT2 - etc@0.3.0 */
#define SENSOR_ADC_ONE_WIRE_CONNECTED 10

/** @brief Sensor event types submitted by the Sensor module. */
enum sensor_event_type {
	SENSOR_EVT_ENVIRONMENTAL_DATA_READY,
	SENSOR_EVT_ENVIRONMENTAL_TEST_DATA_READY,
	SENSOR_EVT_ENVIRONMENTAL_NO_CONNECT,
	SENSOR_EVT_ENVIRONMENTAL_CONNECTED,
	SENSOR_EVT_BATTERY_IN_CHARGING,
	SENSOR_EVT_BATTERY_CHARGE_COMPLETE,
	SENSOR_EVT_SHUTDOWN_READY,
	SENSOR_EVT_ERROR,
};

enum sensor_input {
	SENSOR_INPUT_IN1 = 0,
	SENSOR_INPUT_IN2,
	SENSOR_INPUT_IN3,
	SENSOR_INPUT_IN4,
	SENSOR_INPUT_AMBIENT,
	SENSOR_INPUT_HUMID,
	SENSOR_INPUT_MAX
};

enum sensor_type {
	SENSOR_TYPE_UNDEF = 0,
	SENSOR_TYPE_ANALOG,
	SENSOR_TYPE_DIGITAL,
	SENSOR_TYPE_MAX,
};

#define SENSOR_EVENT_NUM_DEV_MAX SENSOR_INPUT_MAX

/** @brief Structure used to provide environmental data. */
struct sensor_data {
	/** Uptime when the data was sampled. */
	int64_t timestamp;
	/** 5 Temperature in Celsius degrees + 1 Humidity %*/
	float sensor[SENSOR_EVENT_NUM_DEV_MAX];
	/** Voltage of battery in mV */
	uint16_t battery_mV;
};

struct battery_data {
	/** Uptime when the data was sampled. */
	int64_t timestamp;
	/** Temperature in Celsius degrees. */
	uint16_t battery_mV;
};

struct sensor_event {
	struct app_event_header header;

	enum sensor_event_type type;
	union {
		/** Code signifying the cause of error. */
		int err;
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		struct sensor_data* sensors;
	} data;
};

APP_EVENT_TYPE_DECLARE(sensor_event);

#ifdef __cplusplus
}
#endif

#endif /* _SENSOR_EVENT_H_ */
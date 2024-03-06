#ifndef _DATA_EVENT_H_
#define _DATA_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include <zephyr/net/lwm2m.h>
#include "compiler.h"
#include "etc_functional_test.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Data event types submitted by Data module. */
enum data_event_type {
	/** All data has been received for a given sample request. */
	DATA_EVT_DATA_READY,

	/** The option to support fast sample request */
	DATA_EVT_TEST_DATA_READY,

	/** Relay data is ready to be published. */
	DATA_EVT_RELAY_DATA_READY,

	/** Send newly sampled data.
	 *  The event has an associated payload of type @ref data_module_data_buffers in
	 *  the `data.buffer` member.
	 *
	 *  If a non LwM2M build is used the data is heap allocated and must be freed after use by
	 *  calling k_free() on `data.buffer.buf`.
	 */
	DATA_EVT_DATA_SEND,

	DATA_EVT_DATA_SEND_BLE,
	/**
	 * Sending data is complete and there is no more data to send
	 * currently.
	 */
	DATA_EVT_SEND_COMPLETE,

	/** Send UI button data.
	 *  The event has an associated payload of type @ref data_module_data_buffers in
	 *  the `data.buffer` member.
	 *
	 *  If a non LwM2M build is used the data is heap allocated and must be freed after use by
	 *  calling k_free() on `data.buffer.buf`.
	 */
	DATA_EVT_UI_DATA_SEND,

	/** UI button data is ready to be sent. */
	DATA_EVT_UI_DATA_READY,

	/** Impact data is ready to be sent. */
	DATA_EVT_IMPACT_DATA_READY,

	/** Send impact data, similar to DATA_EVT_UI_DATA_SEND */
	DATA_EVT_IMPACT_DATA_SEND,

	/** Send the initial device configuration.
	 *  The event has an associated payload of type @ref cloud_data_cfg in
	 *  the `data.cfg` member.
	 */
	DATA_EVT_CONFIG_INIT,

	/** Send the updated device configuration.
	 *  The event has an associated payload of type @ref cloud_data_cfg in
	 *  the `data.cfg` member.
	 */
	DATA_EVT_CONFIG_READY,

	/** Acknowledge the applied device configuration to cloud.
	 *  The event has an associated payload of type @ref data_module_data_buffers in
	 *  the `data.buffer` member.
	 *
	 *  If a non LwM2M build is used the data is heap allocated and must be freed after use by
	 *  calling k_free() on `data.buffer.buf`.
	 */
	DATA_EVT_CONFIG_SEND,

	/** Get the recent device configuration from cloud. */
	DATA_EVT_CONFIG_GET,

	/** Date time has been obtained. */
	DATA_EVT_DATE_TIME_OBTAINED,

	/* Functional test started */
	DATA_EVT_FUNCTIONAL_TEST_START,

	/* Functional test data send requested */
	DATA_EVT_FUNCTIONAL_TEST_SEND_DATA,

	/* Functional test complete */
	DATA_EVT_FUNCTIONAL_TEST_COMPLETE,

	/** The data module has performed all procedures to prepare for
	 *  a shutdown of the system. The event carries the ID (id) of the module.
	 */
	DATA_EVT_SHUTDOWN_READY,

	/** An irrecoverable error has occurred in the data module. Error details are
	 *  attached in the event structure.
	 */
	DATA_EVT_ERROR
};

/** @brief Structure that contains a pointer to encoded data. */
struct data_module_data_buffers {
	char *buf;
	size_t len;
	/** Object paths used in lwM2M. */
	struct lwm2m_obj_path paths[CONFIG_CLOUD_CODEC_LWM2M_PATH_LIST_ENTRIES_MAX];
	uint8_t valid_object_paths;
};

/** @brief Structure that contains a pointer to relay data. */
struct relay_data_buffer {
	const uint8_t *data;
	uint16_t data_len;
};

struct data_encoded_buffer {
	char *buf;
};

struct data_event {
	struct app_event_header header;

	enum data_event_type type;
	union {
		/** Variable that carries a pointer to data encoded by the module. */
		struct data_encoded_buffer buffer;
		/** Code signifying the cause of error. */
		int err;
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		/* Publish messag id */
		uint32_t message_id;
		/** Relay data to be published */
		struct relay_data_buffer relay_data;
		/* Result of the functional test */
		enum functional_test_result test_result;
	} data;
};

APP_EVENT_TYPE_DECLARE(data_event);

#ifdef __cplusplus
}
#endif

#endif /* _DATA_EVENT_H_ */
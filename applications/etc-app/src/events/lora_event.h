#ifndef _LORA_EVENT_H_
#define _LORA_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LORA_EVENT_MSG_DATA_LEN 32

/** @brief Data event types submitted by Data module. */
enum lora_event_type {
	LORA_EVT_TX_READY,
	LORA_EVT_RX_READY,
	LORA_EVT_RX_DATA_READY,
	LORA_EVT_SHUTDOWN_READY,
	LORA_EVT_ERROR,
};

struct lora_event {
	struct app_event_header header;

	enum lora_event_type type;
	union {
		/** Code signifying the cause of error. */
		int err;
		/* Module ID, used when acknowledging shutdown requests. */
		uint32_t id;
		int64_t timestamp;
		char sensor_msg[LORA_EVENT_MSG_DATA_LEN];
	} data;
};

APP_EVENT_TYPE_DECLARE(lora_event);

#ifdef __cplusplus
}
#endif 

#endif /* _LORA_EVENT_H_ */
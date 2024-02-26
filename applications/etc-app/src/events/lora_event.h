#ifndef _LORA_EVENT_H_
#define _LORA_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif


/** @brief Data event types submitted by Data module. */
enum lora_event_type {
	LORA_EVT_TX_READY,
	LORA_EVT_RX_READY,
	LORA_EVT_RX_DATA_READY,
	LORA_EVT_SEND,
	LORA_EVT_ACK,
	LORA_EVT_NACK,
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
	} data;
};

APP_EVENT_TYPE_DECLARE(lora_event);

#ifdef __cplusplus
}
#endif 

#endif /* _LORA_EVENT_H_ */
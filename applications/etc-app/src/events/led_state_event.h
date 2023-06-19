#ifndef _LED_STATE_EVENT_H_
#define _LED_STATE_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>


/** @brief Asset Tracker led states in the application. */
enum led_state {
	LED_STATE_LTE_DISCONNECTED,
	LED_STATE_LTE_CONNECTING,
	LED_STATE_LTE_CONNECTED,
	LED_STATE_CLOUD_PUBLISHING,
	LED_STATE_CLOUD_CONNECTING,
	LED_STATE_CLOUD_CONNECTED,
	LED_STATE_ERROR_CLOUD,
	LED_STATE_SENSOR_AQUIRING,
	LED_STATE_LORA_TRANSMITTING,
	LED_STATE_LORA_RECEIVING,
	LED_STATE_ACTIVE_MODE,
	LED_STATE_PASSIVE_MODE,
	LED_STATE_ERROR_SYSTEM_FAULT,
	LED_STATE_FOTA_UPDATING,
	LED_STATE_FOTA_UPDATE_REBOOT,
	LED_STATE_FOTA_UPDATE_ERROR,
	LED_STATE_TURN_OFF,
	LED_STATE_COUNT
};

/** @brief Led state event. */
struct led_state_event {
	struct app_event_header header; /**< Event header. */

	enum led_state state;
};

APP_EVENT_TYPE_DECLARE(led_state_event);

/** @} */

#endif /* _LED_STATE_EVENT_H_ */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <app_event_manager.h>
#include <ui.h>
#define MODULE ui_module

#include "modules_common.h"
#include "events/app_event.h"
#include "events/data_event.h"
#include "events/ui_event.h"
#include "events/sensor_event.h"
#include "events/util_event.h"
#include "events/modem_event.h"
#include "events/cloud_event.h"
#include "events/led_state_event.h"
#include "events/lora_event.h"
#include "etc_interface.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_ETC_APP_LOG_LEVEL);

struct ui_msg_data {
	union {
		struct app_event app;
		struct modem_event modem;
		struct sensor_event sensor;
		struct data_event data;
		struct util_event util;
		struct cloud_event cloud;
		struct lora_event lora;
	} module;
};

/* UI module states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_FOTA_UPDATE,
	STATE_FUNCTIONAL_TEST,
	STATE_SHUTDOWN
} state;

/* UI module sub states. */
enum sub_state_type {
	SUB_STATE_NORMAL,
	SUB_STATE_NORMAL_BAT_LOW,
	SUB_STATE_NORMAL_BAT_MED,
	SUB_STATE_NORMAL_BAT_FULL,
	SUB_STATE_CHARGE_BAT_IN_PROCESS,
	SUB_STATE_CHARGE_BAT_COMPLETE,
	SUB_STATE_LTE_CONNECTING,
	SUB_STATE_LTE_CONNECTED,
	SUB_STATE_CLOUD_CONNECTED,
	SUB_STATE_LORA_LISTEN,
};

static enum sub_state_type sub_state = SUB_STATE_NORMAL;
static enum sub_state_type last_sub_state = SUB_STATE_NORMAL; 
static enum sub_state_type last_battery_state = SUB_STATE_NORMAL;
static int ui_module_gen_num_of_sample = 0;

/* Forward declarations */
static void led_pattern_update_work_fn(struct k_work *work);

/* Definition used to specify LED patterns that should hold forever. */
#define HOLD_FOREVER -1
#define UI_LED_WAIT_TIME K_MSEC(2000)
#define UI_LED_WAIT_NORMAL_DURATION_MSEC (5000)
#define UI_LED_WAIT_SWITCH_STATE_MSEC (1000)
#define UI_LED_BATTERY_NORMAL_ON_DURATION_MSEC (100)
#define UI_LED_BATTERY_NORMAL_OFF_DURATION_MSEC (9900)
#define UI_LED_ERROR_BASE_DURATION_MSEC (1000)

/* List of LED patterns supported in the UI module. */
static struct led_pattern {
	/* Variable used to construct a linked list of led patterns. */
	sys_snode_t header;
	/* LED state. */
	enum led_state led_state;
	/* Duration of the LED state. */
	int16_t duration_msec;
} led_pattern_list[LED_STATE_COUNT];

/* Linked list used to schedule multiple LED pattern transitions. */
static sys_slist_t pattern_transition_list = SYS_SLIST_STATIC_INIT(&pattern_transition_list);

/* Delayed work that is used to display and transition to the correct LED pattern depending on the
 * internal state of the module.
 */
static K_WORK_DELAYABLE_DEFINE(led_pattern_update_work, led_pattern_update_work_fn);

/* UI module message queue. */
#define UI_QUEUE_ENTRY_COUNT		10
#define UI_QUEUE_BYTE_ALIGNMENT		4

K_MSGQ_DEFINE(msgq_ui, sizeof(struct ui_msg_data),
	      UI_QUEUE_ENTRY_COUNT, UI_QUEUE_BYTE_ALIGNMENT);

static struct module_data self = {
	.name = "ui",
	.msg_q = NULL,
	.supports_shutdown = true,
};

/* Forward declarations. */
static void message_handler(struct ui_msg_data *msg);

/* Convenience functions used in internal state handling. */
static char *state2str(enum state_type new_state)
{
	switch (new_state) {
	case STATE_INIT:
		return "STATE_INIT";
	case STATE_RUNNING:
		return "STATE_RUNNING";
	case STATE_FOTA_UPDATE:
		return "STATE_FOTA_UPDATE";
	case STATE_FUNCTIONAL_TEST:
		return "STATE_FUNCTIONAL_TEST";
	case STATE_SHUTDOWN:
		return "STATE_SHUTDOWN";
	default:
		return "Unknown";
	}
}

/* Convenience functions used in internal state handling. */
static char *sub_state2str(enum sub_state_type new_state)
{
	switch (new_state) {
	case SUB_STATE_NORMAL:
		return "SUB_STATE_NORMAL";
	case SUB_STATE_NORMAL_BAT_LOW:
		return "SUB_STATE_NORMAL_BAT_LOW";
	case SUB_STATE_NORMAL_BAT_MED:
		return "SUB_STATE_NORMAL_BAT_MED";
	case SUB_STATE_NORMAL_BAT_FULL:
		return "SUB_STATE_NORMAL_BAT_FULL";
	case SUB_STATE_CHARGE_BAT_IN_PROCESS:
		return "SUB_STATE_CHARGE_BAT_IN_PROCESS";
	case SUB_STATE_CHARGE_BAT_COMPLETE:
		return "SUB_STATE_CHARGE_BAT_COMPLETE";
	case SUB_STATE_LTE_CONNECTING:
		return "SUB_STATE_LTE_CONNECTING";
	case SUB_STATE_LTE_CONNECTED:
		return "SUB_STATE_LTE_CONNECTED";
	case SUB_STATE_CLOUD_CONNECTED:
		return "SUB_STATE_CLOUD_CONNECTED";
	case SUB_STATE_LORA_LISTEN:
		return "SUB_STATE_LORA_LISTEN";
	default:
		return "Unknown";
	}
}

static void state_set(enum state_type new_state)
{
	if (new_state == state) {
		LOG_DBG("State: %s", state2str(state));
		return;
	}

	LOG_DBG("State transition %s --> %s",
		state2str(state),
		state2str(new_state));

	state = new_state;
}

static void sub_state_set(enum sub_state_type new_state)
{
	if (new_state == sub_state) {
		LOG_DBG("Sub state: %s", sub_state2str(sub_state));
		return;
	}

	LOG_DBG("Sub state transition %s --> %s",
		sub_state2str(sub_state),
		sub_state2str(new_state));

	/* Save last sub-state for battery (in-charge or no charge) */
	if (sub_state == SUB_STATE_NORMAL_BAT_FULL ||
		sub_state == SUB_STATE_NORMAL_BAT_LOW ||
		sub_state == SUB_STATE_NORMAL_BAT_MED || 
		sub_state == SUB_STATE_CHARGE_BAT_IN_PROCESS ||
		sub_state == SUB_STATE_CHARGE_BAT_COMPLETE) {
		last_battery_state = sub_state;
	}
	last_sub_state = sub_state;
	sub_state = new_state;
}

static void last_sub_state_set(enum sub_state_type state) {
	if (state == last_sub_state) {
		LOG_DBG("Last sub state: %s", sub_state2str(last_sub_state));
		return;
	}

	LOG_DBG("Last sub state transition %s --> %s",
		sub_state2str(last_sub_state),
		sub_state2str(state));

	if (state == SUB_STATE_NORMAL_BAT_FULL ||
		state == SUB_STATE_NORMAL_BAT_LOW ||
		state == SUB_STATE_NORMAL_BAT_MED || 
		state == SUB_STATE_CHARGE_BAT_IN_PROCESS ||
		state == SUB_STATE_CHARGE_BAT_COMPLETE) {
		last_battery_state = state;
	}

	last_sub_state = state;
}

static enum sub_state_type sub_state_get(void) {
	return sub_state;
}

static enum sub_state_type last_sub_state_get(void) {
	return last_sub_state;
}

static bool is_sub_state_higher_priority() {
	if (sub_state == SUB_STATE_LTE_CONNECTING ||
		sub_state == SUB_STATE_LTE_CONNECTED ||
		sub_state == SUB_STATE_CLOUD_CONNECTED ||
		sub_state == SUB_STATE_LORA_LISTEN) 
	{
			return true;
	}
	return false;
}

/* Handlers */
static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_app_event(aeh)) {
		struct app_event *event = cast_app_event(aeh);
		struct ui_msg_data ui_msg = {
			.module.app = *event
		};

		message_handler(&ui_msg);
	}

	if (is_data_event(aeh)) {
		struct data_event *event = cast_data_event(aeh);
		struct ui_msg_data ui_msg = {
			.module.data = *event
		};

		message_handler(&ui_msg);
	}

	if (is_modem_event(aeh)) {
		struct modem_event *event = cast_modem_event(aeh);
		struct ui_msg_data ui_msg = {
			.module.modem = *event
		};

		message_handler(&ui_msg);
	}

	if (is_util_event(aeh)) {
		struct util_event *event = cast_util_event(aeh);
		struct ui_msg_data ui_msg = {
			.module.util = *event
		};

		message_handler(&ui_msg);
	}

	if (is_cloud_event(aeh)) {
		struct cloud_event *event = cast_cloud_event(aeh);
		struct ui_msg_data ui_msg = {
			.module.cloud = *event
		};

		message_handler(&ui_msg);
	}

	if (is_sensor_event(aeh)) {
		struct sensor_event *event = cast_sensor_event(aeh);
		struct ui_msg_data ui_msg = {
			.module.sensor = *event
		};

		message_handler(&ui_msg);
	}

	if (is_lora_event(aeh)) {
		struct lora_event *event = cast_lora_event(aeh);
		struct ui_msg_data ui_msg = {
			.module.lora = *event
		};

		message_handler(&ui_msg);
	}

	return false;
}

/* Static module functions. */
static void update_led_pattern(enum led_state pattern)
{
	BUILD_ASSERT((uint8_t)UI_TURN_OFF == (uint8_t)LED_STATE_TURN_OFF, 
		     "ui_led_pattern and led_state incompatible");
	LOG_DBG("Update the LED pattern %d", pattern);
	ui_led_set_pattern((enum ui_led_pattern)pattern);
}

static struct led_pattern* update_list_pattern(enum led_state state, int16_t duration) 
{
	led_pattern_list[state].led_state = state;
	led_pattern_list[state].duration_msec = duration;
	return &led_pattern_list[state];
}

static void led_pattern_update_work_fn(struct k_work *work)
{
	struct led_pattern *next_pattern;
	static enum led_state previous_led_state = LED_STATE_COUNT;
	sys_snode_t *node = sys_slist_get(&pattern_transition_list);

	if (node == NULL) {
		LOG_DBG("Empty node %d", sub_state_get());
		if (sub_state_get() == SUB_STATE_LTE_CONNECTING) {
			next_pattern = update_list_pattern(LED_STATE_LTE_CONNECTING, HOLD_FOREVER);
		} else if (sub_state_get() == SUB_STATE_LTE_CONNECTED) {
			next_pattern = update_list_pattern(LED_STATE_LTE_CONNECTED, HOLD_FOREVER);
		} else if (sub_state_get() == SUB_STATE_CLOUD_CONNECTED) {
			next_pattern = update_list_pattern(LED_STATE_CLOUD_CONNECTED, HOLD_FOREVER);
		} else if (sub_state_get() == SUB_STATE_LORA_LISTEN) {
			next_pattern = update_list_pattern(LED_STATE_LORA_LISTEN, HOLD_FOREVER);
		}  else if (sub_state_get() == SUB_STATE_CHARGE_BAT_COMPLETE) {
			next_pattern = update_list_pattern(LED_STATE_CHARGE_BATTERY_FULL, HOLD_FOREVER);;
		} else if (sub_state_get() == SUB_STATE_CHARGE_BAT_IN_PROCESS) {
			next_pattern = update_list_pattern(LED_STATE_CHARGE_BATTERY_IN_CHARING, HOLD_FOREVER);
		} else if (sub_state_get() == SUB_STATE_NORMAL_BAT_FULL) {
			next_pattern = update_list_pattern(LED_STATE_BATTERY_FULL, UI_LED_BATTERY_NORMAL_ON_DURATION_MSEC);
			sub_state_set(SUB_STATE_NORMAL);
		} else if (sub_state_get() == SUB_STATE_NORMAL_BAT_MED) {
			next_pattern = update_list_pattern(LED_STATE_BATTERY_MED, UI_LED_BATTERY_NORMAL_ON_DURATION_MSEC);
			sub_state_set(SUB_STATE_NORMAL);
		} else if (sub_state_get() == SUB_STATE_NORMAL_BAT_LOW) {
			next_pattern = update_list_pattern(LED_STATE_BATTERY_EMPTY, UI_LED_BATTERY_NORMAL_ON_DURATION_MSEC);
			sub_state_set(SUB_STATE_NORMAL);
		} else {
			next_pattern = &led_pattern_list[LED_STATE_TURN_OFF];
			enum sub_state_type _last_sub_state = last_sub_state_get();
			if (_last_sub_state == SUB_STATE_NORMAL_BAT_LOW ||
				_last_sub_state == SUB_STATE_NORMAL_BAT_MED ||
				_last_sub_state == SUB_STATE_NORMAL_BAT_FULL) {
				update_list_pattern(LED_STATE_TURN_OFF, UI_LED_BATTERY_NORMAL_OFF_DURATION_MSEC);
				sub_state_set(_last_sub_state);
			} else {
				update_list_pattern(LED_STATE_TURN_OFF, HOLD_FOREVER);
			}
		}
	} else {
		next_pattern = CONTAINER_OF(node, struct led_pattern, header);
	}

	LOG_DBG("led update: %s, %d s, prev: %s", get_led_state_type_str(next_pattern->led_state), 
		next_pattern->duration_msec, get_led_state_type_str(previous_led_state));
	/* Prevent the same LED led_state from being scheduled twice in a row. */
	if (next_pattern->led_state != previous_led_state) {
		update_led_pattern(next_pattern->led_state);
		previous_led_state = next_pattern->led_state;
	}

	/* - Skip HOLD_FOREVER states if there is another item in the list
	 * - If LED is being turned off or an ON state is being held forever,
	     don't reschedule handler
	 * - If LED is on and duration is specified (not HOLD_FOREVER),
	 *   reschedule handler to turn off LED after specified time (save power)
	 */
	if (!sys_slist_is_empty(&pattern_transition_list) || 
		((next_pattern->duration_msec != HOLD_FOREVER) && 
			(next_pattern->led_state == LED_STATE_TURN_OFF)) || 
		((next_pattern->duration_msec != HOLD_FOREVER) && 
			((next_pattern->led_state != LED_STATE_CHARGE_BATTERY_IN_CHARING) && 
			(next_pattern->led_state != LED_STATE_CHARGE_BATTERY_FULL) && 
			(next_pattern->led_state != LED_STATE_LTE_CONNECTING) && 
			(next_pattern->led_state != LED_STATE_LTE_CONNECTED) && 
			(next_pattern->led_state != LED_STATE_CLOUD_CONNECTED) && 
			(next_pattern->led_state != LED_STATE_LORA_LISTEN)))) {
		LOG_DBG("k_work_reschedule led work");
		if (next_pattern->duration_msec > 0) {
			k_work_reschedule(&led_pattern_update_work, K_MSEC(next_pattern->duration_msec));
		} else {
			k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		}
	}
}

static void ui_module_send(void)
{
	struct ui_event *event = new_ui_event();
	event->type = UI_EVT_INPUT_DATA_READY;
	APP_EVENT_SUBMIT(event);
}

static void ui_input_handler(enum etc_interface_event_type type) {
	if (type == ETC_INTERFACE_EVENT_RTC) {
		LOG_INF("UI -> ETC_INTERFACE_EVENT_RTC");
	} else if (type == ETC_INTERFACE_EVENT_HALL) {
		LOG_INF("UI -> ETC_INTERFACE_EVENT_HALL");
	} else {
		/* No action required */
	}
}

static int setup(void)
{
	etc_interface_register_event_handler(ui_input_handler);
	return 0;
}

/* Function that clears LED pattern transition list. */
static void transition_list_clear(void)
{
	struct led_pattern *transition, *next_transition = NULL;

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&pattern_transition_list,
					  transition,
					  next_transition,
					  header) {
		sys_slist_remove(&pattern_transition_list, NULL, &transition->header);
	};
}

/* Function that appends a LED state and a corresponding duration to the
 * LED pattern transition list.
 */
static void transition_list_append(enum led_state led_state, int16_t duration_msec)
{
	update_list_pattern(led_state, duration_msec);
	/* Force to remove if it is here */
	(void)sys_slist_find_and_remove(&pattern_transition_list, &led_pattern_list[led_state].header);	
	sys_slist_append(&pattern_transition_list, &led_pattern_list[led_state].header);
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct ui_msg_data *msg)
{
	if (IS_EVENT(msg, app, APP_EVT_START)) {
		int err = module_start(&self);

		if (err) {
			LOG_ERR("Failed starting module, error: %d", err);
			SEND_ERROR(ui, UI_EVT_ERROR, err);
		}

		state_set(STATE_RUNNING);
		sub_state_set(SUB_STATE_NORMAL);
	}
}

/* Message handler for STATE_RUNNING. */
static void on_state_running(struct ui_msg_data *msg)
{
	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_AQUIRING)) {
		transition_list_append(LED_STATE_SENSOR_AQUIRING, UI_LED_WAIT_NORMAL_DURATION_MSEC);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTING)) {
		transition_list_clear();
		sub_state_set(SUB_STATE_LTE_CONNECTING);
		transition_list_append(LED_STATE_LTE_CONNECTING, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED_READY)) {
		transition_list_clear();
		sub_state_set(SUB_STATE_LTE_CONNECTED);
		transition_list_append(LED_STATE_LTE_CONNECTED, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}	

	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_DISCONNECTED) ||
	    IS_EVENT(msg, modem, MODEM_EVT_PSM_ENTERED)) {
		transition_list_clear();
		sub_state_set(last_battery_state);
		k_work_reschedule(&led_pattern_update_work,  K_NO_WAIT);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_ERROR)) {
		transition_list_clear();
		sub_state_set(last_battery_state);
		transition_list_append(LED_STATE_LTE_ERROR, 4 * UI_LED_ERROR_BASE_DURATION_MSEC);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_DISCONNECTED)) {
		transition_list_clear();
		sub_state_set(last_battery_state);
		k_work_reschedule(&led_pattern_update_work,  K_NO_WAIT);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTING)) {
		transition_list_clear();
		sub_state_set(last_battery_state);
		transition_list_append(LED_STATE_CLOUD_CONNECTING, UI_LED_WAIT_NORMAL_DURATION_MSEC);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
		transition_list_clear();
		sub_state_set(SUB_STATE_CLOUD_CONNECTED);
		transition_list_append(LED_STATE_CLOUD_CONNECTED, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_ERROR)) {
		transition_list_clear();
		sub_state_set(last_battery_state);
		transition_list_append(LED_STATE_CLOUD_ERROR, 1 * UI_LED_ERROR_BASE_DURATION_MSEC);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, lora, LORA_EVT_RX_READY)) {
		transition_list_clear();
		sub_state_set(SUB_STATE_LORA_LISTEN);
		transition_list_append(LED_STATE_LORA_LISTEN, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, lora, LORA_EVT_RX_DATA_READY)) {
		/* RX listen is done. Back to battery */
		transition_list_clear();
		sub_state_set(last_battery_state);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, lora, LORA_EVT_SEND)) {
		transition_list_clear();
		transition_list_append(LED_STATE_LORA_SEND, 2000);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, lora, LORA_EVT_NACK)) {
		transition_list_clear();
		sub_state_set(last_battery_state);
		transition_list_append(LED_STATE_LORA_NACK, 2000);
		k_work_reschedule(&led_pattern_update_work,  K_NO_WAIT);
	}
	
	if (IS_EVENT(msg, lora, LORA_EVT_ERROR)) {
		transition_list_clear();
		sub_state_set(last_battery_state);
		transition_list_append(LED_STATE_LORA_ERROR, 6 * UI_LED_ERROR_BASE_DURATION_MSEC);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_START)) {
		transition_list_clear();
		transition_list_append(LED_STATE_FOTA_DOWNLOADING, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		state_set(STATE_FOTA_UPDATE);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_ERROR)) {
		transition_list_clear();
		transition_list_append(LED_STATE_FOTA_ERROR, 5 * UI_LED_ERROR_BASE_DURATION_MSEC);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		state_set(STATE_FOTA_UPDATE);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_ERROR)) {
		transition_list_append(LED_STATE_CHARGE_BATTERY_ERROR, 7 * UI_LED_ERROR_BASE_DURATION_MSEC);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		sub_state_set(SUB_STATE_CHARGE_BAT_IN_PROCESS);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_IN_CHARGING)) {
		if (is_sub_state_higher_priority()) {
			last_sub_state_set(SUB_STATE_CHARGE_BAT_IN_PROCESS);
		} else {
			transition_list_append(LED_STATE_CHARGE_BATTERY_IN_CHARING, HOLD_FOREVER);
			k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
			sub_state_set(SUB_STATE_CHARGE_BAT_IN_PROCESS);
		}
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_CHARGE_COMPLETE)) {
		if (is_sub_state_higher_priority()) {
			last_sub_state_set(SUB_STATE_CHARGE_BAT_COMPLETE);
		} else {
			transition_list_append(LED_STATE_CHARGE_BATTERY_FULL, HOLD_FOREVER);
			k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
			sub_state_set(SUB_STATE_CHARGE_BAT_COMPLETE);
		}
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_NORMAL_FULL)) {
		if (is_sub_state_higher_priority()) {
			last_sub_state_set(SUB_STATE_NORMAL_BAT_FULL);
		} else {
			transition_list_clear();
			transition_list_append(LED_STATE_BATTERY_FULL, UI_LED_BATTERY_NORMAL_ON_DURATION_MSEC);
			transition_list_append(LED_STATE_TURN_OFF, UI_LED_BATTERY_NORMAL_OFF_DURATION_MSEC);
			k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
			sub_state_set(SUB_STATE_NORMAL_BAT_FULL);
		}
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_NORMAL_MED)) {
		if (is_sub_state_higher_priority()) {
			last_sub_state_set(SUB_STATE_NORMAL_BAT_MED);
		} else {
			transition_list_clear();
			transition_list_append(LED_STATE_BATTERY_MED, UI_LED_BATTERY_NORMAL_ON_DURATION_MSEC);
			transition_list_append(LED_STATE_TURN_OFF, UI_LED_BATTERY_NORMAL_OFF_DURATION_MSEC);
			k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
			sub_state_set(SUB_STATE_NORMAL_BAT_MED);
		}
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_NORMAL_LOW)) {
		if (is_sub_state_higher_priority()) {
			last_sub_state_set(SUB_STATE_NORMAL_BAT_LOW);
		} else {
			transition_list_clear();
			transition_list_append(LED_STATE_BATTERY_EMPTY, UI_LED_BATTERY_NORMAL_ON_DURATION_MSEC);
			transition_list_append(LED_STATE_TURN_OFF, UI_LED_BATTERY_NORMAL_OFF_DURATION_MSEC);
			k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
			sub_state_set(SUB_STATE_NORMAL_BAT_LOW);
		}
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_FUNCTIONAL_TEST_START) || 
	    IS_EVENT(msg, sensor, SENSOR_EVT_FUNCTIONAL_UI_TEST_START)) {
		transition_list_append(LED_STATE_FUNCTIONAL_TEST_IN_PROGRESS, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_FUNCTIONAL_TEST);
	}
}

/* Message handler for STATE_FOTA_UPDATING. */
static void on_state_fota_update(struct ui_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_ERROR)) {
		transition_list_clear();
		transition_list_append(LED_STATE_FOTA_ERROR, 5 * UI_LED_ERROR_BASE_DURATION_MSEC);
		transition_list_append(LED_STATE_TURN_OFF, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		state_set(STATE_RUNNING);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_DONE)) {
		transition_list_clear();
		transition_list_append(LED_STATE_TURN_OFF, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_RUNNING);
	}
}

static void on_state_functional_test(struct ui_msg_data *msg)
{
	if (IS_EVENT(msg, data, DATA_EVT_FUNCTIONAL_TEST_COMPLETE) ||
	    IS_EVENT(msg, data, DATA_EVT_FUNCTIONAL_UI_TEST_COMPLETE)) {
		if (msg->module.data.data.test_result == FUNC_TEST_SUCCESS) {
			transition_list_append(LED_STATE_FUNCTIONAL_TEST_PASS, HOLD_FOREVER);
		} else {
			transition_list_append(LED_STATE_FUNCTIONAL_TEST_FAIL, HOLD_FOREVER);
		}
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_FUNCTIONAL_TEST_END) || 
	    IS_EVENT(msg, sensor, SENSOR_EVT_FUNCTIONAL_UI_TEST_END)) {
		transition_list_clear();
		transition_list_append(LED_STATE_TURN_OFF, UI_LED_WAIT_SWITCH_STATE_MSEC);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_RUNNING);
	}
}

/* Message handler for all states. */
static void on_all_states(struct ui_msg_data *msg)
{
	if (IS_EVENT(msg, data, DATA_EVT_TEST_DATA_READY)) {
		if (ui_module_gen_num_of_sample != 0) {
			ui_module_gen_num_of_sample = ui_module_gen_num_of_sample - 1;
			struct ui_event *event = new_ui_event();
			event->type = UI_EVT_TEST_DATA_READY;
			APP_EVENT_SUBMIT(event);
		}
	}
	
	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(ui, UI_EVT_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
	}

	if (state != STATE_RUNNING) {
		if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_IN_CHARGING)) {
			last_sub_state_set(SUB_STATE_NORMAL_BAT_FULL);
		}

		if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_CHARGE_COMPLETE)) {
			last_sub_state_set(SUB_STATE_NORMAL_BAT_FULL);
		}

		if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_NORMAL_FULL)) {
			last_sub_state_set(SUB_STATE_NORMAL_BAT_FULL);
		}

		if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_NORMAL_MED)) {
			last_sub_state_set(SUB_STATE_NORMAL_BAT_MED);
		}

		if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_NORMAL_LOW)) {
			last_sub_state_set(SUB_STATE_NORMAL_BAT_LOW);
		}
	}
}

static void message_handler(struct ui_msg_data *msg)
{
	switch (state) {
	case STATE_INIT:
		on_state_init(msg);
		break;
	case STATE_RUNNING:
		on_state_running(msg);
		break;
	case STATE_FOTA_UPDATE:
		on_state_fota_update(msg);
		break;
	case STATE_FUNCTIONAL_TEST:
		on_state_functional_test(msg);
		break;
	case STATE_SHUTDOWN:
		/* The shutdown state has no transition. */
		break;
	default:
		LOG_WRN("Unknown ui module state.");
		break;
	}

	on_all_states(msg);
}

void ui_module_test_data_request(int num_of_sample)
{
	if (num_of_sample == 0) return;
	struct ui_event *event = new_ui_event();
	event->type = UI_EVT_TEST_DATA_READY;
	ui_module_gen_num_of_sample = num_of_sample - 1;
	APP_EVENT_SUBMIT(event);
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, app_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, data_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, modem_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, util_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, cloud_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, lora_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, sensor_event);

SYS_INIT(setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);


#ifdef CONFIG_UI_MODULE_SHELL
#include <zephyr/shell/shell.h>

static int cmd_ui_status(const struct shell *shell, size_t argc, char **argv)
{
	return 0;
}

static int cmd_ui_sensor_aquiring(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(sensor, SENSOR_EVT_ENVIRONMENTAL_AQUIRING);
	return 0;
}

static int cmd_ui_lte_connecting(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(modem, MODEM_EVT_LTE_CONNECTING);
	return 0;
}

static int cmd_ui_lte_connected(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(modem, MODEM_EVT_LTE_CONNECTED);
	return 0;
}

static int cmd_ui_lte_psm(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(modem, MODEM_EVT_PSM_ENTERED);
	return 0;
}

static int cmd_ui_lte_error(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(modem, MODEM_EVT_ERROR);
	return 0;
}

static int cmd_ui_lora_listen(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(lora, LORA_EVT_RX_READY);
	return 0;
}

static int cmd_ui_lora_send(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(lora, LORA_EVT_SEND);
	return 0;
}

static int cmd_ui_lora_nack(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(lora, LORA_EVT_NACK);
	return 0;
}

static int cmd_ui_lora_error(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(lora, LORA_EVT_ERROR);
	return 0;
}

static int cmd_ui_fota_download(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(cloud, CLOUD_EVT_FOTA_START);
	return 0;
}

static int cmd_ui_fota_error(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(cloud, CLOUD_EVT_FOTA_ERROR);
	return 0;
}

static int cmd_ui_battery_normal_full(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(sensor, SENSOR_EVT_BATTERY_NORMAL_FULL);
	return 0;
}

static int cmd_ui_battery_normal_med(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(sensor, SENSOR_EVT_BATTERY_NORMAL_MED);
	return 0;
}

static int cmd_ui_battery_normal_low(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(sensor, SENSOR_EVT_BATTERY_NORMAL_LOW);
	return 0;
}

static int cmd_ui_battery_charge_complete(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(sensor, SENSOR_EVT_BATTERY_CHARGE_COMPLETE);
	return 0;
}

static int cmd_ui_battery_charge_wip(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(sensor, SENSOR_EVT_BATTERY_IN_CHARGING);
	return 0;
}

static int cmd_ui_battery_error(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(sensor, SENSOR_EVT_BATTERY_ERROR);
	return 0;
}

static int cmd_ui_func_test_start(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(sensor, SENSOR_EVT_FUNCTIONAL_UI_TEST_START);
	return 0;
}

static int cmd_ui_func_test_complete_pass(const struct shell *shell, size_t argc, char **argv)
{
	struct data_event *data_event = new_data_event();
	data_event->type = DATA_EVT_FUNCTIONAL_UI_TEST_COMPLETE;
	data_event->data.test_result = FUNC_TEST_SUCCESS;
	APP_EVENT_SUBMIT(data_event);
	return 0;
}

static int cmd_ui_func_test_stop(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(sensor, SENSOR_EVT_FUNCTIONAL_UI_TEST_END);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_ui_event,
	SHELL_CMD(status, NULL, "List status of transition", cmd_ui_status),
	SHELL_CMD(sensor_aquiring, NULL, "Event sensor aquiring", cmd_ui_sensor_aquiring),
	SHELL_CMD(lte_connecting, NULL, "Event lte connecting", cmd_ui_lte_connecting),
	SHELL_CMD(lte_connected, NULL, "Event lte connected", cmd_ui_lte_connected),
	SHELL_CMD(lte_psm, NULL, "Event PSM entered", cmd_ui_lte_psm),
	SHELL_CMD(lte_error, NULL, "Event lte error", cmd_ui_lte_error),
	SHELL_CMD(lora_listen, NULL, "Event lora listen", cmd_ui_lora_listen),
	SHELL_CMD(lora_send, NULL, "Event lora send", cmd_ui_lora_send),
	SHELL_CMD(lora_nack, NULL, "Event lora nack", cmd_ui_lora_nack),
	SHELL_CMD(lora_error, NULL, "Event lora error", cmd_ui_lora_error),
	SHELL_CMD(fota_downloading, NULL, "Event fota downloading", cmd_ui_fota_download),
	SHELL_CMD(fota_error, NULL, "Event fota error", cmd_ui_fota_error),
	SHELL_CMD(bat_full, NULL, "Event normal full", cmd_ui_battery_normal_full),
	SHELL_CMD(bat_med, NULL, "Event normal med", cmd_ui_battery_normal_med),
	SHELL_CMD(bat_low, NULL, "Event normal low", cmd_ui_battery_normal_low),
	SHELL_CMD(bat_complete, NULL, "Event complete charge", cmd_ui_battery_charge_complete),
	SHELL_CMD(bat_charging, NULL, "Event wip charge", cmd_ui_battery_charge_wip),
	SHELL_CMD(bat_error, NULL, "Event error battery", cmd_ui_battery_error),
	SHELL_CMD(self_test_start, NULL, "Event to test self test function start", 
		cmd_ui_func_test_start),
	SHELL_CMD(self_test_complete_pass, NULL, "Event to test self test function complete pass", 
		cmd_ui_func_test_complete_pass),
	SHELL_CMD(self_test_stop, NULL, "Event to test self test function stop", 
		cmd_ui_func_test_stop),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(ui_event, &sub_ui_event, "Trigger app event", NULL);

#endif
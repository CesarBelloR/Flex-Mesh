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
	STATE_SHUTDOWN
} state;

/* UI module sub states. */
static enum sub_state_type {
	SUB_STATE_NORMAL,
	SUB_STATE_CHARGE_IN_PROCESS,
	SUB_STATE_CHARGE_COMPLETE
} sub_state;

static int ui_module_gen_num_of_sample = 0;
static enum sensor_event_type last_battery_sensor_event;
/* Forward declarations */
static void led_pattern_update_work_fn(struct k_work *work);
static void led_battery_update_work_fn(struct k_work *work);

/* Definition used to specify LED patterns that should hold forever. */
#define HOLD_FOREVER -1
#define UI_LED_WAIT_TIME K_MSEC(500)
#define UI_LED_INTERVAL_BATTERY_NORMAL K_SECONDS(10)

/* List of LED patterns supported in the UI module. */
static struct led_pattern {
	/* Variable used to construct a linked list of led patterns. */
	sys_snode_t header;
	/* LED state. */
	enum led_state led_state;
	/* Duration of the LED state. */
	int16_t duration_sec;
} led_pattern_list[LED_STATE_COUNT];

/* Linked list used to schedule multiple LED pattern transitions. */
static sys_slist_t pattern_transition_list = SYS_SLIST_STATIC_INIT(&pattern_transition_list);

/* Delayed work that is used to display and transition to the correct LED pattern depending on the
 * internal state of the module.
 */
static K_WORK_DELAYABLE_DEFINE(led_pattern_update_work, led_pattern_update_work_fn);

static K_WORK_DELAYABLE_DEFINE(led_battery_update_work, led_battery_update_work_fn);
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
	case SUB_STATE_CHARGE_IN_PROCESS:
		return "SUB_STATE_CHARGE_IN_PROCESS";
	case SUB_STATE_CHARGE_COMPLETE:
		return "SUB_STATE_CHARGE_COMPLETE";
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

	sub_state = new_state;
}

static enum sub_state_type sub_state_get(void) {
	return sub_state;
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

static void update_list_pattern(enum led_state state, int16_t duration) 
{
	led_pattern_list[state].led_state = state;
	led_pattern_list[state].duration_sec = duration;
}

static void led_pattern_update_work_fn(struct k_work *work)
{
	struct led_pattern *next_pattern;
	static enum led_state previous_led_state = LED_STATE_COUNT;
	sys_snode_t *node = sys_slist_get(&pattern_transition_list);

	if (node == NULL) {
		LOG_DBG("Empty node");
		if (sub_state_get() == SUB_STATE_CHARGE_COMPLETE) {
			update_list_pattern(LED_STATE_CHARGE_BATTERY_FULL, HOLD_FOREVER);
			next_pattern = &led_pattern_list[LED_STATE_CHARGE_BATTERY_FULL];
		} else if (sub_state_get() == SUB_STATE_CHARGE_IN_PROCESS) {
			update_list_pattern(LED_STATE_CHARGE_BATTERY_IN_CHARING, HOLD_FOREVER);
			next_pattern = &led_pattern_list[LED_STATE_CHARGE_BATTERY_IN_CHARING];
		} else {
			update_list_pattern(LED_STATE_TURN_OFF, HOLD_FOREVER);
			next_pattern = &led_pattern_list[LED_STATE_TURN_OFF];
		}
	} else {
		next_pattern = CONTAINER_OF(node, struct led_pattern, header);
	}

	LOG_DBG("led update: %s, %d s, prev: %s", get_led_state_type_str(next_pattern->led_state), next_pattern->duration_sec,
		get_led_state_type_str(previous_led_state));
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
	    (((next_pattern->led_state != LED_STATE_TURN_OFF) &&
			(next_pattern->led_state != LED_STATE_CHARGE_BATTERY_IN_CHARING) && 
			(next_pattern->led_state != LED_STATE_CHARGE_BATTERY_FULL)) &&
			(next_pattern->duration_sec != HOLD_FOREVER)
		)) {
		LOG_DBG("k_work_reschedule led work");
		if (next_pattern->duration_sec > 0) {
			k_work_reschedule(&led_pattern_update_work, 
					  K_SECONDS(next_pattern->duration_sec));
		} else {
			k_work_reschedule(&led_pattern_update_work, 
					  UI_LED_WAIT_TIME);
		}
	}
}

static void led_battery_update_work_fn(struct k_work *work) {
	if (sub_state_get() == SUB_STATE_NORMAL) {
		SEND_EVENT(sensor, last_battery_sensor_event);
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
static void transition_list_append(enum led_state led_state, int16_t duration_sec)
{
	update_list_pattern(led_state, duration_sec);
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
	if (IS_EVENT(msg, app, APP_EVT_WAKEUP)) {
		enum etc_device_mode mode = etc_device_get_mode();
		enum led_state set_state = LED_STATE_DEV_MODE_LOGGER_PLUS;
		if (mode == ETC_DEVICE_MODE_LTE_LOGGER) {
			set_state = LED_STATE_DEV_MODE_LOGGER_PLUS;
		} else if (mode == ETC_DEVICE_MODE_LORA_LOGGER) {
			set_state = LED_STATE_DEV_MODE_LOGGER;
		} else if (mode == ETC_DEVICE_MODE_RELAY) {
			set_state = LED_STATE_DEV_MODE_RELAY;
		} else {
			set_state = LED_STATE_DEV_MODE_BLE;
		}
		transition_list_clear();
		transition_list_append(set_state, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_ENVIRONMENTAL_DATA_READY)) {
		transition_list_append(LED_STATE_SENSOR_AQUIRING, 5);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTING)) {
		transition_list_append(LED_STATE_LTE_CONNECTING, 5);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_CONNECTED)) {
		transition_list_append(LED_STATE_LTE_CONNECTED, 5);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}	

	if (IS_EVENT(msg, modem, MODEM_EVT_LTE_DISCONNECTED) ||
	    IS_EVENT(msg, modem, MODEM_EVT_PSM_ENTERED)) {
		transition_list_append(LED_STATE_LTE_DISCONNECTED, 5);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, modem, MODEM_EVT_ERROR)) {
		transition_list_clear();
		transition_list_append(LED_STATE_LTE_ERROR, 5);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_DISCONNECTED)) {
		transition_list_clear();
		transition_list_append(LED_STATE_CLOUD_DISCONNECTED, 5);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTING)) {
		transition_list_append(LED_STATE_CLOUD_CONNECTING, 5);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_CONNECTED)) {
		transition_list_append(LED_STATE_CLOUD_CONNECTED, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_ERROR)) {
		transition_list_clear();
		transition_list_append(LED_STATE_CLOUD_ERROR, 5);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, lora, LORA_EVT_RX_READY)) {
		transition_list_append(LED_STATE_LORA_LISTEN, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, lora, LORA_EVT_SEND)) {
		transition_list_append(LED_STATE_LORA_SEND, 2);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, lora, LORA_EVT_NACK)) {
		transition_list_append(LED_STATE_LORA_NACK, 2);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}
	
	if (IS_EVENT(msg, lora, LORA_EVT_ERROR)) {
		transition_list_clear();
		transition_list_append(LED_STATE_LORA_ERROR, 5);
		k_work_reschedule(&led_pattern_update_work,  UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_DATA_SEND_ACK)) {
		if (sub_state_get() == SUB_STATE_CHARGE_IN_PROCESS) {
			transition_list_append(LED_STATE_CHARGE_BATTERY_IN_CHARING, HOLD_FOREVER);
		} else if (sub_state_get() == SUB_STATE_CHARGE_COMPLETE) {
			transition_list_append(LED_STATE_CHARGE_BATTERY_FULL, HOLD_FOREVER);
		} else {
			transition_list_append(LED_STATE_TURN_OFF, HOLD_FOREVER);
		}
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_START)) {
		transition_list_clear();
		transition_list_append(LED_STATE_FOTA_DOWNLOADING, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		state_set(STATE_FOTA_UPDATE);
	}

	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_ERROR)) {
		transition_list_clear();
		transition_list_append(LED_STATE_FOTA_ERROR, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		state_set(STATE_FOTA_UPDATE);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_ERROR)) {
		transition_list_append(LED_STATE_CHARGE_BATTERY_ERROR, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		k_work_cancel_delayable(&led_battery_update_work);
		sub_state_set(SUB_STATE_CHARGE_IN_PROCESS);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_IN_CHARGING)) {
		transition_list_append(LED_STATE_CHARGE_BATTERY_IN_CHARING, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		k_work_cancel_delayable(&led_battery_update_work);
		sub_state_set(SUB_STATE_CHARGE_IN_PROCESS);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_CHARGE_COMPLETE)) {
		transition_list_append(LED_STATE_CHARGE_BATTERY_FULL, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		k_work_cancel_delayable(&led_battery_update_work);
		sub_state_set(SUB_STATE_CHARGE_COMPLETE);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_NORMAL_FULL)) {
		last_battery_sensor_event = SENSOR_EVT_BATTERY_NORMAL_FULL;
		transition_list_append(LED_STATE_BATTERY_FULL, 1);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		k_work_reschedule(&led_battery_update_work, UI_LED_INTERVAL_BATTERY_NORMAL);
		sub_state_set(SUB_STATE_NORMAL);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_NORMAL_MED)) {
		last_battery_sensor_event = SENSOR_EVT_BATTERY_NORMAL_MED;
		transition_list_append(LED_STATE_BATTERY_MED, 1);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		k_work_reschedule(&led_battery_update_work, UI_LED_INTERVAL_BATTERY_NORMAL);
		sub_state_set(SUB_STATE_NORMAL);
	}

	if (IS_EVENT(msg, sensor, SENSOR_EVT_BATTERY_NORMAL_LOW)) {
		last_battery_sensor_event = SENSOR_EVT_BATTERY_NORMAL_LOW;
		transition_list_append(LED_STATE_BATTERY_EMPTY, 1);
		k_work_reschedule(&led_pattern_update_work, UI_LED_WAIT_TIME);
		k_work_reschedule(&led_battery_update_work, UI_LED_INTERVAL_BATTERY_NORMAL);
		sub_state_set(SUB_STATE_NORMAL);
	}
}

/* Message handler for STATE_CLOUD_CONNECTING. */
static void on_state_cloud_connecting(struct ui_msg_data *msg)
{
}

/* Message handler for STATE_CLOUD_ASSOCIATING. */
static void on_state_cloud_associating(struct ui_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_DATA_SEND_ACK)) {
		transition_list_append(LED_STATE_CLOUD_CONNECTED, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_RUNNING);
	}
}

/* Message handler for STATE_LORA_TRANSMITTING. */
static void on_state_lora_transmitting(struct ui_msg_data *msg)
{
	// if (IS_EVENT(msg, lora, LORA_EVT_TX_READY)) {
	// 	transition_list_clear();
	// 	transition_list_append(LED_STATE_LORA_TRANSMITTING, HOLD_FOREVER);
	// 	k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
	// 	state_set(STATE_RUNNING);
	// }
}

/* Message handler for STATE_LORA_RECEIVING. */
static void on_state_lora_receiving(struct ui_msg_data *msg)
{
	if (IS_EVENT(msg, lora, LORA_EVT_RX_READY)) {
		transition_list_clear();
		transition_list_append(LED_STATE_LORA_LISTEN, HOLD_FOREVER);
		k_work_reschedule(&led_pattern_update_work, K_NO_WAIT);
		state_set(STATE_RUNNING);
	}
}

/* Message handler for STATE_FOTA_UPDATING. */
static void on_state_fota_update(struct ui_msg_data *msg)
{
	if (IS_EVENT(msg, cloud, CLOUD_EVT_FOTA_ERROR)) {
		transition_list_clear();
		transition_list_append(LED_STATE_FOTA_ERROR, 5);
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


#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

static int cmd_ui_status(const struct shell *shell, size_t argc, char **argv)
{
	return 0;
}


static int cmd_ui_event_wakeup(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(app, APP_EVT_WAKEUP);
	return 0;
}

static int cmd_ui_sensor_aquiring(const struct shell *shell, size_t argc, char **argv)
{
	SEND_EVENT(sensor, SENSOR_EVT_ENVIRONMENTAL_DATA_READY);
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

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_ui_event,
	SHELL_CMD(status, NULL, "List status of transition", cmd_ui_status),
	SHELL_CMD(wakeup, NULL, "Event app wakeup", cmd_ui_event_wakeup),
	SHELL_CMD(sensor_aquiring, NULL, "Event sensor aquiring", cmd_ui_sensor_aquiring),
	SHELL_CMD(lte_connecting, NULL, "Event lte connecting", cmd_ui_lte_connecting),
	SHELL_CMD(lte_connected, NULL, "Event lte connected", cmd_ui_lte_connected),
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
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(ui_event, &sub_ui_event, "Trigger app event", NULL);

#endif
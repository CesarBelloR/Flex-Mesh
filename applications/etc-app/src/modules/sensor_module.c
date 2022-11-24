#include <zephyr/kernel.h>
#include <stdio.h>
#include <math.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <app_event_manager.h>
#include "adc.h"
#include "etc_date_time.h"
#include "etc_interface.h"
#define MODULE sensor_module
#define MODULE_SENSOR_THREAD_STACK_SIZE 512

#include "modules_common.h"
#include "events/app_event.h"
#include "events/data_event.h"
#include "events/sensor_event.h"
#include "events/util_event.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sensor_module, CONFIG_ETC_APP_LOG_LEVEL);

struct sensor_msg_data {
	union {
		struct app_event app;
		struct data_event data;
		struct util_event util;
	} module;
};

/* Sensor module super states. */
static enum state_type {
	STATE_INIT,
	STATE_RUNNING,
	STATE_SHUTDOWN
} state;

static struct k_work_delayable sensor_poll_work;
static struct sensor_data static_sensor_data;

/* Sensor module message queue. */
#define SENSOR_QUEUE_ENTRY_COUNT	10
#define SENSOR_QUEUE_BYTE_ALIGNMENT	4

#define SENSOR_GPIO_SENSE_ENABLE_PIN (13)
#define SENSOR_GPIO_S0_PIN (9)
#define SENSOR_GPIO_S1_PIN (10)

K_MSGQ_DEFINE(msgq_sensor, sizeof(struct sensor_msg_data),
	      SENSOR_QUEUE_ENTRY_COUNT, SENSOR_QUEUE_BYTE_ALIGNMENT);

/* Forward declarations */
static void sensor_poll_work_fn(struct k_work *work);

static bool sensor_is_processing = false;
static struct module_data self = {
	.name = "sensor",
	.msg_q = &msgq_sensor,
	.supports_shutdown = true,
};

const struct device* dev_gpio = NULL;

static void sensor_adc_switch_channel(int8_t channel) {
	gpio_pin_set(dev_gpio, SENSOR_GPIO_SENSE_ENABLE_PIN, 0U);
	gpio_pin_set(dev_gpio, SENSOR_GPIO_S0_PIN, channel & 0x01);
	gpio_pin_set(dev_gpio, SENSOR_GPIO_S1_PIN, (channel >> 1) & 0x01);
}

static void sensor_adc_hw_init(void) {
	dev_gpio = device_get_binding("GPIO_0");
	if (dev_gpio == NULL) {
		return;
	}

	gpio_pin_configure(dev_gpio, SENSOR_GPIO_SENSE_ENABLE_PIN, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(dev_gpio, SENSOR_GPIO_S0_PIN, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(dev_gpio, SENSOR_GPIO_S1_PIN, GPIO_OUTPUT_INACTIVE);
}

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

/* Handlers */
static bool app_event_handler(const struct app_event_header *aeh)
{
	struct sensor_msg_data msg = {0};
	bool enqueue_msg = false;

	if (is_app_event(aeh)) {
		struct app_event *event = cast_app_event(aeh);

		msg.module.app = *event;
		enqueue_msg = true;
	}

	if (is_data_event(aeh)) {
		struct data_event *event = cast_data_event(aeh);

		msg.module.data = *event;
		enqueue_msg = true;
	}

	if (is_util_event(aeh)) {
		struct util_event *event = cast_util_event(aeh);

		msg.module.util = *event;
		enqueue_msg = true;
	}

	if (enqueue_msg) {
		int err = module_enqueue_msg(&self, &msg);

		if (err) {
			LOG_ERR("Message could not be enqueued");
			SEND_ERROR(sensor, SENSOR_EVT_ERROR, err);
		}
	}

	return false;
}

static void sensor_module_send(struct sensor_data* sensor)
{
	struct sensor_event *sensor_event = new_sensor_event();
	sensor_event->type = SENSOR_EVT_ENVIRONMENTAL_DATA_READY;
	sensor_event->data.sensors = sensor;
	APP_EVENT_SUBMIT(sensor_event);
}

static int setup(void)
{
	adc_init();
	sensor_adc_hw_init();
	return 0;
}

#define SENSOR_NTC_NOMINAL_RESISTANCE 10000.0
#define SENSOR_NTC_NOMINAL_TEMP 25.0
#define SENSOR_NTC_BETA 3434.0
#define SENSOR_NTC_RESISTOR_REF 10000.0
#define SENSOR_RAW_ADC_MAX 4095

static float sensor_ntc_converter(int raw_data) {
	raw_data = (int)((float)(raw_data) * 3.6 / 3.3);
	if (raw_data > SENSOR_RAW_ADC_MAX) {
		raw_data = SENSOR_RAW_ADC_MAX;
	}
	if (raw_data == SENSOR_RAW_ADC_MAX) {
		return SENSOR_NTC_NO_CONNECTED;
	}
	float tmp_value = (float)SENSOR_RAW_ADC_MAX / (float)raw_data - 1.0;
	tmp_value = SENSOR_NTC_RESISTOR_REF / tmp_value;
	tmp_value = tmp_value / SENSOR_NTC_NOMINAL_RESISTANCE;
	tmp_value = logf(tmp_value);
	tmp_value = tmp_value / SENSOR_NTC_BETA;
	tmp_value += 1.0 / (SENSOR_NTC_NOMINAL_TEMP + 273.15);
	tmp_value = 1.0 / tmp_value;
	tmp_value -= 273.15;
	return tmp_value;
}

static void sensor_poll_work_fn(struct k_work *work) {
	if (sensor_is_processing) return;
	sensor_is_processing = true;
	struct sensor_data* data = &static_sensor_data;
	data->timestamp = date_time_now_second();
	data->temperature[0] = sensor_ntc_converter(adc_get_channel(0));
	if (fabs(data->temperature[0] - SENSOR_NTC_NO_CONNECTED) > 1.0) {
		LOG_DBG("Ambient temp %2.2f", data->temperature[0]);
	}
	for (int8_t i = 1; i < SENSOR_EVENT_NUM_DEV_MAX; i++) {
		sensor_adc_switch_channel(i - 1);
		k_msleep(50);
		data->temperature[i] = sensor_ntc_converter(adc_get_channel(2));
		if (fabs(data->temperature[i] - SENSOR_NTC_NO_CONNECTED) > 1.0) {
			LOG_DBG("Channel %d temp %f", i - 1, data->temperature[i]);
		} else {
			LOG_DBG("Channel %d doesn't available", i - 1);
		}
	}
	sensor_module_send(data);
	k_work_reschedule(&sensor_poll_work, K_SECONDS(CONFIG_SENSOR_POLL_INTERVAL_SECONDS));
	sensor_is_processing = false;
}

static void sensor_interface_handler(void) {
	k_work_reschedule(&sensor_poll_work, K_NO_WAIT);
}

/* Message handler for STATE_INIT. */
static void on_state_init(struct sensor_msg_data *msg)
{
	if (IS_EVENT(msg, data, DATA_EVT_CONFIG_INIT)) {
		state_set(STATE_RUNNING);
	}
}

/* Message handler for STATE_RUNNING. */
static void on_state_running(struct sensor_msg_data *msg)
{
}

/* Message handler for all states. */
static void on_all_states(struct sensor_msg_data *msg)
{
	if (IS_EVENT(msg, util, UTIL_EVT_SHUTDOWN_REQUEST)) {
		/* The module doesn't have anything to shut down and can
		 * report back immediately.
		 */
		SEND_SHUTDOWN_ACK(sensor, SENSOR_EVT_SHUTDOWN_READY, self.id);
		state_set(STATE_SHUTDOWN);
	}
}

static void module_thread_fn(void)
{
	int err;
	struct sensor_msg_data msg = { 0 };

	self.thread_id = k_current_get();

	err = module_start(&self);
	if (err) {
		LOG_ERR("Failed starting module, error: %d", err);
		SEND_ERROR(sensor, SENSOR_EVT_ERROR, err);
	}

	state_set(STATE_INIT);

	err = setup();
	if (err) {
		LOG_ERR("setup, error: %d", err);
		SEND_ERROR(sensor, SENSOR_EVT_ERROR, err);
	}

	k_work_init_delayable(&sensor_poll_work, sensor_poll_work_fn);
	k_work_reschedule(&sensor_poll_work, K_SECONDS(CONFIG_SENSOR_POLL_INTERVAL_SECONDS));

	etc_interface_register_event_handler(sensor_interface_handler);
	while (true) {
		module_get_next_msg(&self, &msg);

		switch (state) {
		case STATE_INIT:
			on_state_init(&msg);
			break;
		case STATE_RUNNING:
			on_state_running(&msg);
			break;
		case STATE_SHUTDOWN:
			/* The shutdown state has no transition. */
			break;
		default:
			LOG_WRN("Unknown sensor module state.");
			break;
		}

		on_all_states(&msg);
	}
}

K_THREAD_DEFINE(sensor_module_thread, MODULE_SENSOR_THREAD_STACK_SIZE,
		module_thread_fn, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, app_event);
APP_EVENT_SUBSCRIBE(MODULE, data_event);
APP_EVENT_SUBSCRIBE(MODULE, util_event);

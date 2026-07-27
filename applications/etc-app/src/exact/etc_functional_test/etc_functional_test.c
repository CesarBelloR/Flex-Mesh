#include <zephyr/kernel.h>
#include "etc_functional_test.h"
#include "etc_settings.h"
#include "data_codec.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_functional_test, CONFIG_ETC_APP_LOG_LEVEL);

#define FUNC_TEST_MIN_RSRP		-103
#define FUNC_TEST_MIN_BAT_VOLTAGE_MV	4090
#define FUNCTIONAL_TEST_SEND_DELAY_S	5
#define FUNCTIONAL_TEST_TIMEOUT_S 	300

#define RSRP_INVALID -125

K_MUTEX_DEFINE(functional_test_mutex);

#define SENSOR_VALUE_ACCURACY 0.5f
static const float functional_test_values[] = {
	-4.39, 5.02, -4.39, 5.02
};

struct functional_test_data test_data;


/* Work to control functional test timeout */
static void functional_test_timeout_work_fn(struct k_work *work);
static void functional_test_send_data_work_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(functional_test_timeout_work, functional_test_timeout_work_fn);
K_WORK_DELAYABLE_DEFINE(functional_test_send_data_work, functional_test_send_data_work_fn);

static functional_test_evt_handler evt_handler;


static void notify_event(const enum functional_test_evt evt)
{
	if (evt_handler != NULL) {
		evt_handler(evt);
	} else {
		LOG_ERR("Library event handler not registered, or empty event");
	}
}

static void functional_test_timeout_work_fn(struct k_work *work)
{
	functional_test_stop();
	notify_event(FUNC_TEST_EVT_TIMEOUT);
}

static void functional_test_send_data_work_fn(struct k_work *work)
{
	functional_test_set_state(FUNC_TEST_STATE_SENDING_DATA);
	notify_event(FUNC_TEST_EVT_SEND_DATA);
}

/** Check if the sensor values pass the functional test.
 * 
 * @retval true Values pass functional test
 * @retval false Values do not pass functional test 
*/
static bool check_sensor_values(struct sensor_data *data) 
{
	int input_high_index = SENSOR_INPUT_IN4;
	enum etc_device_type device_type = etc_get_device_type();

	if (device_type == ETC_DEVICE_TYPE_EMBEDDABLE) {
		input_high_index = SENSOR_INPUT_IN2;
	} else if (device_type == ETC_DEVICE_TYPE_AMBIENT) {
		input_high_index = SENSOR_INPUT_IN1;
	}

	for (int i = SENSOR_INPUT_IN1; i <= input_high_index; i++) {
		float val = data->sensor[i];
		float expected_val = functional_test_values[i];
		if (val < (expected_val - SENSOR_VALUE_ACCURACY) ||
		    val > (expected_val + SENSOR_VALUE_ACCURACY)) {
			return false;
		}
	}

	return true;
}

/**
 * Mutex needs to be locked before calling this function.
*/
static enum functional_test_result evaluate_functional_test_result(void)
{
	enum functional_test_result result;

	if (!check_sensor_values(&test_data.sensor_data)) {
		result = FUNC_TEST_FAIL_SENSOR;
	} else if (test_data.lte_rsrp < etc_get_functional_test_rsrp_value()) {
		result = FUNC_TEST_FAIL_MODEM;
	} else if (!test_data.battery_connected) {
		result = FUNC_TEST_FAIL_BAT_DISCONNECTED;
	} else if (test_data.sensor_data.battery_mV < FUNC_TEST_MIN_BAT_VOLTAGE_MV) {
		result = FUNC_TEST_FAIL_BAT;
	/* Fail functional test if the device ID hasn't been set */
	} else if (test_data.unset_device_id) {
		result = FUNC_TEST_FAIL_DEVICE_ID;
	} else if (!test_data.ack && test_data.state >= FUNC_TEST_STATE_WAITING_FOR_ACK) {
		result = FUNC_TEST_FAIL_ACK;
	} else {
		result = FUNC_TEST_SUCCESS;
	}

	return result;
}

void functional_test_set_state(enum functional_test_state new_state)
{
	k_mutex_lock(&functional_test_mutex, K_FOREVER);
	test_data.state = new_state;
	k_mutex_unlock(&functional_test_mutex);
}

enum functional_test_state functional_test_get_state(void)
{
	enum functional_test_state state_temp;

	k_mutex_lock(&functional_test_mutex, K_FOREVER);
	state_temp = test_data.state;
	k_mutex_unlock(&functional_test_mutex);
	return state_temp;
}

bool functional_test_schedule_send(void)
{
	if (functional_test_get_state() == FUNC_TEST_STATE_COLLECTING_DATA) {
		k_work_reschedule(&functional_test_send_data_work,
				K_SECONDS(FUNCTIONAL_TEST_SEND_DELAY_S));
		LOG_DBG("Scheduled send in %u seconds", FUNCTIONAL_TEST_SEND_DELAY_S);
		return true;
	}

	return false;
}

int track_functional_test(enum functional_test_data_type type,
				  void *data)
{
	__ASSERT_NO_MSG(data != NULL);
	int retval = 0;

	k_mutex_lock(&functional_test_mutex, K_FOREVER);

	if (test_data.state == FUNC_TEST_STATE_NOT_STARTED ||
	    test_data.state == FUNC_TEST_STATE_COMPLETE) {
		k_mutex_unlock(&functional_test_mutex);
		return 0;
	}

	switch (type) {
	case DATA_TYPE_SENSOR:
		struct sensor_data *sensor_data = (struct sensor_data *)data;
		memcpy(&test_data.sensor_data, sensor_data, sizeof(struct sensor_data));
		break;
	case DATA_TYPE_MODEM:
		int rsrp = *((int16_t *)data);
		test_data.lte_rsrp = rsrp;
		test_data.rsrp_valid = true;
		break;
	case DATA_TYPE_ACK:
		bool ack = *((bool *)data);
		test_data.ack = ack;
		break;
	case DATA_TYPE_DEVICE_ID_DEFAULT:
		bool default_id = *((bool *)data);
		test_data.unset_device_id = default_id;
		break;
	case DATA_TYPE_BATTERY_CONNECTED:
		bool battery_connected = *((bool *)data);
		test_data.battery_connected = battery_connected;
		break;
	default:
	}

	k_mutex_unlock(&functional_test_mutex);

	return retval;
}

void functional_test_start(functional_test_evt_handler event_handler)
{
	/* Reset functional test data. */
	memset(&test_data, 0, sizeof(test_data));
	test_data.lte_rsrp = RSRP_INVALID;
	test_data.rsrp_valid = false;
	test_data.result = FUNC_TEST_FAIL_UNKNOWN;

	functional_test_set_state(FUNC_TEST_STATE_COLLECTING_DATA);
	k_work_reschedule(&functional_test_timeout_work, K_SECONDS(FUNCTIONAL_TEST_TIMEOUT_S));
	evt_handler = event_handler;
}

bool functional_test_stop(void)
{
	k_mutex_lock(&functional_test_mutex, K_FOREVER);
	/* Only stop test is functional test is running */
	if (test_data.state == FUNC_TEST_STATE_NOT_STARTED ||
	    test_data.state == FUNC_TEST_STATE_COMPLETE) {
		k_mutex_unlock(&functional_test_mutex);
		return false;
	}

	test_data.result = evaluate_functional_test_result();
	LOG_INF("Test result: %u", test_data.result);
	if (test_data.result == FUNC_TEST_SUCCESS) {
		etc_set_power_mode(ETC_POWER_MODE_PROBE);
		etc_settings_sync_config();
	}

	test_data.state = FUNC_TEST_STATE_COMPLETE;
	k_mutex_unlock(&functional_test_mutex);

	k_work_cancel_delayable(&functional_test_timeout_work);
	k_work_cancel_delayable(&functional_test_send_data_work);

	return true;
}

enum functional_test_result functional_test_get_result(void)
{
	enum functional_test_result result;
	
	k_mutex_lock(&functional_test_mutex, K_FOREVER);
	result = test_data.result;
	k_mutex_unlock(&functional_test_mutex);
	
	return result;
}

void functional_test_get_data(struct functional_test_data *data)
{
	__ASSERT_NO_MSG(data != NULL);

	k_mutex_lock(&functional_test_mutex, K_FOREVER);
	test_data.result = evaluate_functional_test_result();
	memcpy(data, &test_data, sizeof(test_data));
	k_mutex_unlock(&functional_test_mutex);
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
static int cmd_functional_test_set_rsrp(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 2) {
		char *end;
		int16_t rsrp = (int16_t)strtol(argv[1], &end, 10);
		if (data_codec_rsrp_is_valid(rsrp)) {
			int ret = etc_set_functional_test_rsrp_value(rsrp);
			if (ret) {
				shell_error(shell, "[Functional Test] Error writing RSRP %d", ret);
				return -1;
			} else {
				shell_print(shell, "[Functional Test] Wrote RSRP %d success", rsrp);
				return 0;
			}
		} else {
			shell_error(shell, "[Functional Test] Invalid input value (out of range)");
			return -1;
		}
	}

	shell_print(shell, "Usage: %s <rsrp>\n"
			   "  Write the RSRP used by functional test", argv[0]);
	return -1;
}

static int cmd_functional_test_get_rsrp(const struct shell *shell, size_t argc, char **argv)
{
	shell_print(shell, "[Functional Test] Get RSRP: %d", etc_get_functional_test_rsrp_value());
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_functional_test, 
	SHELL_CMD_ARG(set_rsrp, NULL, "Set the RSRP value used by the functional test", cmd_functional_test_set_rsrp, 1, 1),
	SHELL_CMD(get_rsrp, NULL, "Get the RSRP value used by the functional test", cmd_functional_test_get_rsrp),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(functional_test, &sub_functional_test, "Add sub functional test", NULL);
#endif

#ifndef ETC_FUNCTIONAL_TEST_H_
#define ETC_FUNCTIONAL_TEST_H_

#include "events/sensor_event.h"

enum functional_test_data_type {
	DATA_TYPE_SENSOR,
	DATA_TYPE_MODEM,
	DATA_TYPE_ACK,
	DATA_TYPE_DEVICE_ID_DEFAULT,
	DATA_TYPE_BATTERY_CONNECTED
};

enum functional_test_state {
	FUNC_TEST_STATE_NOT_STARTED,
	FUNC_TEST_STATE_COLLECTING_DATA,
	FUNC_TEST_STATE_SENDING_DATA,
	FUNC_TEST_STATE_WAITING_FOR_ACK,
	FUNC_TEST_STATE_COMPLETE
};

enum functional_test_result {
	FUNC_TEST_SUCCESS,
	FUNC_TEST_FAIL_ACK,
	FUNC_TEST_FAIL_SENSOR,
	FUNC_TEST_FAIL_BAT,
	FUNC_TEST_FAIL_MODEM,
	FUNC_TEST_FAIL_DEVICE_ID,
	FUNC_TEST_FAIL_UNKNOWN,
	/* Appended to keep the existing values stable, as the result is sent
	 * directly as the LwM2M status code. */
	FUNC_TEST_FAIL_BAT_DISCONNECTED
};

enum functional_test_evt {
	FUNC_TEST_EVT_SEND_DATA,
	FUNC_TEST_EVT_TIMEOUT
};

struct functional_test_data {
	struct sensor_data sensor_data;
	int16_t lte_rsrp;
	bool ack;
	bool unset_device_id;
	bool battery_connected;
	enum functional_test_state state;
	/* Result of the test (pass/fail) */
	enum functional_test_result result;
};

typedef void (*functional_test_evt_handler)(const enum functional_test_evt evt);

/**
 * Get the current functional test's state.
 * 
 * @return Current state of the functional test.
*/
enum functional_test_state functional_test_get_state(void);

/**
 * Set the current functional test's state.
*/
void functional_test_set_state(enum functional_test_state new_state);
/**
 * Start the functional test.
 * 
 * @param event_handler Event handler callback function that functional test events
 * will be sent to.
*/
void functional_test_start();

/** Stop functional test and evaluate result. Set device mode to probe mode if
 * passed.
 * 
*/
bool functional_test_stop(void);

/**
 * Retrieve functional test result.
 * 
 * @return Functional test result.
*/
enum functional_test_result functional_test_get_result(void);

/**
 * Get the functional test data.
 * 
 * @param data Buffer that functional test data will be written to.
*/
void functional_test_get_data(struct functional_test_data *data);

/** Track/collect functional test data.
 * 
 * @param type Type of data bassed in data
 * @param data Pointer to data in correct format according to type
*/
int track_functional_test(enum functional_test_data_type type,
			  void *data);

/**
 * Schedule sending a functional test message.
 * 
 * @retval true Send scheduled.
 * @retval false Send not scheduled.
*/
bool functional_test_schedule_send(void);

#endif /* ETC_FUNCTIONAL_TEST_H_ */
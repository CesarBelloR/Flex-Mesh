#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <date_time.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_calibration, CONFIG_ETC_CALIBRATION_LOG_LEVEL);

#include "events/app_event.h"
#include "etc_device.h"
#include "etc_sensor.h"
#include "etc_sensor_helper.h"
#include "etc_calibration.h"
#include "app_module_helper.h"

// Enumration for switch state list
enum switch_state {
	SW_STATE_ADC_OFFSET,
	SW_STATE_TEMP_0_2,
	SW_STATE_TEMP_25_0,
	SW_STATE_TEMP_44_6,
	SW_STATE_TEMP_70_4,
	SW_STATE_ADC_HIGH
};

// Macro to define a range around a given value.
#define RANGE(value) {(value) - CONFIG_TEMP_TOLERANCE, (value) + CONFIG_TEMP_TOLERANCE}

// Structure representing a temperature range with minimum and maximum values.
struct temperature_range {
	int32_t min;
	int32_t max;
};

// Structure representing an ADC range with minimum and maximum values.
struct adc_range {
	int min;
	int max;
};

// Enumration for temperature calibration list
enum temperature_range_list {
	TEMP_RANGE_AMBIENT,
	TEMP_RANGE_SW2,
	TEMP_RANGE_SW3,
	TEMP_RANGE_SW4,
	TEMP_RANGE_SW5
};

// Constant array of temperature ranges used for calibration testing.
static const struct temperature_range list_temperature[] = {
	{CONFIG_TEMP_RANGE_SPECIFIC_MIN, CONFIG_TEMP_RANGE_SPECIFIC_MAX},
	RANGE(CONFIG_TEMP_RANGE_SWITCH2_REF),
	RANGE(CONFIG_TEMP_RANGE_SWITCH3_REF),
	RANGE(CONFIG_TEMP_RANGE_SWITCH4_REF),
	RANGE(CONFIG_TEMP_RANGE_SWITCH5_REF)};

// Enumration for analog calibration list switch
enum analog_range_list {
	ADC_RANGE_SW1,
	ADC_RANGE_SW6
};

// Constant array of ADC ranges used for calibration testing.
static const struct adc_range list_adc[] = {{-100, 100}, {3914, 4114}};

// Constant value for battery valid to do calibration
static const int batt_valid_mV = 3300;

// Macro to check if a given temperature is within a specified temperature range index in
// list_temperature.
#define IS_TEMP_IN_RANGE(index, temp)                                                              \
	({                                                                                         \
		int32_t temp_cd = (int32_t)((temp) * 100.0f);                                      \
		((temp_cd) >= list_temperature[(index)].min &&                                     \
		 (temp_cd) <= list_temperature[(index)].max);                                      \
	})

// Macro to check if a given ADC value is within a specified ADC range index in list_adc.
#define IS_ADC_IN_RANGE(index, adc) ((adc) >= list_adc[index].min && (adc) <= list_adc[index].max)

// Mutex to block the sensor processing
K_MUTEX_DEFINE(etc_calibration_mutex);

// Work delayable to handle timeout
static void etc_calibration_timeout_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(etc_calibration_timeout, etc_calibration_timeout_handler);

// Boolean flag indicating whether the calibration process is ready to proceed.
static bool etc_calibration_ready = false;

// Calibrator SN
static uint32_t etc_calibrator_sn = 0;
static struct etc_sensor_calibration_status_info calibration_status = {0};

void etc_calibration_init(void)
{
	memset(&calibration_status, 0, sizeof(calibration_status));
	calibration_status.status = ETC_SENSOR_CALIB_IDLE;
	calibration_status.result = ETC_SENSOR_CALIB_NO_STATUS;
}

int etc_calibration_check(void)
{
	etc_calibration_lock();
	if (!etc_calibration_ready) {
		etc_calibration_ready = true;
		etc_calibration_init();
	}

	etc_sensor_calibration_enter();
	int rc = 0;
	uint16_t current_bat_mV = etc_sensor_get_battery();
	if (current_bat_mV < batt_valid_mV) {
		LOG_ERR("Low power to handle calibration");
		rc = -EINVAL;
		calibration_status.result = ETC_SENSOR_CALIB_BATTERY_LOW;
		goto done;
	}
	rc = etc_sensor_calibration_scan();
	if (rc == 0) {
		LOG_ERR("No calibration tool is here");
		rc = -ENOENT;
		goto done;
	}
	LOG_DBG("Number of sensor %d", rc);
	rc = etc_sensor_calibration_read_sn();
	if (rc < ETC_CALIB_MIN_SN || rc > ETC_CALIB_MAX_SN) {
		LOG_ERR("No calibration code!");
		goto done;
	}
	LOG_DBG("Code sensor %d", rc);
	etc_calibrator_sn = rc;
	calibration_status.status = ETC_SENSOR_CALIB_PRECALIB_VALUE_CHECK;
	/* TODO: need to know the calibrator code */
	rc = 0;
	k_work_schedule(&etc_calibration_timeout, K_SECONDS(CONFIG_CALIBRATION_TIMEOUT));
done:
	/* Reset & exit calibration when not ready */
	if (rc) {
		calibration_status.status = ETC_SENSOR_CALIB_IDLE;
		etc_sensor_calibration_exit();
	}
	etc_calibration_unlock();
	return rc;
}

static int etc_calibration_packet_status(struct etc_sensor_adc_calibration_info *calibration_info,
					 uint16_t *hw_raw_adc, char *adj_msg, int adj_size)
{
	int rc = 0;
	int adj_val_len = 0;
	calibration_info->loaded = true;
	struct etc_sensor_adc_raw_data raw_data = {0};

	for (int i = TEMP_RANGE_SW2; i <= TEMP_RANGE_SW5; i++) {
		etc_sensor_calibration_set_gpio_mask(i);
		rc = etc_sensor_calibration_read_adc(&raw_data);
		for (int j = SENSOR_INPUT_IN1; j <= SENSOR_INPUT_IN4; j++) {
			float temp_value = etc_sensor_calibration_convert_temperature(
				raw_data.port[j], hw_raw_adc, calibration_info);
			adj_val_len += snprintf(adj_msg + adj_val_len, adj_size - adj_val_len,
						"%.1f,", temp_value);
		}
		/* Adjust 1 byte to remove extra , */
		adj_msg[adj_val_len - 1] = '|';
	}
	if (adj_val_len + 1 <= adj_size) {
		/* Adjust 1 byte to remove extra | */
		adj_msg[adj_val_len - 1] = '\0';
	} else {
		return -ENOMEM;
	}

	return 0;
}

static int etc_calibration_packet_no_status(char *adj_msg, int adj_size)
{
	memset(adj_msg, 0, adj_size);
	adj_msg[0] = '\0';
	return 0;
}

static int etc_calibration_packet_ref(char *ref_msg, int ref_size)
{
	int32_t refs[] = {CONFIG_TEMP_RANGE_SWITCH2_REF, CONFIG_TEMP_RANGE_SWITCH3_REF,
			  CONFIG_TEMP_RANGE_SWITCH4_REF, CONFIG_TEMP_RANGE_SWITCH5_REF};
	const int num_refs = sizeof(refs) / sizeof(refs[0]);
	ref_msg[0] = '\0';
	for (int i = 0; i < num_refs; i++) {
		float temp_c = refs[i] / 100.0f;
		char temp_str[8];
		snprintf(temp_str, sizeof(temp_str), "%.1f", temp_c);
		if (i > 0) {
			strncat(ref_msg, "|", ref_size - strlen(ref_msg) - 1);
		}
		strncat(ref_msg, temp_str, ref_size - strlen(ref_msg) - 1);
		if (strlen(ref_msg) >= ref_size - 1) {
			return -ENOMEM;
		}
	}
	return 0;
}

static int etc_calibration_packet_calibrator_sn(char *id_msg, int id_size)
{
	snprintf(id_msg, id_size, "%d", etc_calibrator_sn);
	if (strlen(id_msg) >= id_size - 1) {
		return -ENOMEM;
	}
	return 0;
}

static void etc_calibration_timeout_handler(struct k_work *work)
{
	k_mutex_lock(&etc_calibration_mutex, K_FOREVER);
	calibration_status.status = ETC_SENSOR_CALIB_IDLE;
	etc_sensor_calibration_exit();
	app_module_notify_calibration_timeout();
	k_mutex_unlock(&etc_calibration_mutex);
}

int etc_calibration_run(void)
{
	int rc = 0;
	uint16_t hw_raw_adc = 0;
	int64_t time_now;
	struct etc_sensor_adc_calibration_info calibration_info = {
		.high = 0.0, .loaded = true, .offset = 0.0, .ref = 3948.75};
	struct etc_sensor_adc_calibration_info previous_calibration_info = {0};
	struct etc_sensor_adc_raw_data raw_data = {0};
	etc_calibration_lock();

	/* Cancel timeout work */
	k_work_cancel_delayable(&etc_calibration_timeout);

	if (!etc_calibration_ready) {
		etc_calibration_ready = true;
		etc_calibration_init();
	}

	uint16_t current_bat_mV = etc_sensor_get_battery();
	if (current_bat_mV < batt_valid_mV) {
		LOG_ERR("Low power to handle calibration");
		rc = -EINVAL;
		calibration_status.result = ETC_SENSOR_CALIB_BATTERY_LOW;
		goto done;
	}

	float current_temp = etc_sensor_calibration_read_temperature_from_sensor();
	if (!IS_TEMP_IN_RANGE(TEMP_RANGE_AMBIENT, current_temp)) {
		LOG_ERR("The current temperature is not in range");
		rc = -EINVAL;
		calibration_status.result = ETC_SENSOR_CALIB_AMBIENT_TEMP_OUT_OF_RANGE;
		goto done;
	}

	etc_sensor_calibration_set_gpio_mask(SW_STATE_ADC_OFFSET);
	rc = etc_sensor_calibration_read_adc(NULL);
	if (!IS_ADC_IN_RANGE(ADC_RANGE_SW1, rc)) {
		LOG_ERR("The current adc is not in range [%d]: %d", ADC_RANGE_SW1, rc);
		rc = -EINVAL;
		calibration_status.result = ETC_SENSOR_CALIB_AMBIENT_TEMP_OUT_OF_RANGE;
		goto done;
	}
	/* Save in RAM offselt */
	calibration_info.offset = (float)(rc);

	etc_sensor_calibration_set_gpio_mask(SW_STATE_ADC_HIGH);
	rc = etc_sensor_calibration_read_adc(NULL);
	if (!IS_ADC_IN_RANGE(ADC_RANGE_SW6, rc)) {
		LOG_ERR("The current adc is not in range [%d]: %d", ADC_RANGE_SW6, rc);
		rc = -EINVAL;
		calibration_status.result = ETC_SENSOR_CALIB_AMBIENT_TEMP_OUT_OF_RANGE;
		goto done;
	}

	/* Save in RAM high */
	calibration_info.high = (float)rc;
	/* Save in RAM ADC version HW */
	hw_raw_adc = etc_sensor_calibration_get_hw_version_adc();
	etc_sensor_calibration_save_temperature_compensation(hw_raw_adc);
	/* Reload the calibration */
	rc = etc_calibration_load_config(&previous_calibration_info, true);
	if (rc) {
		rc = etc_calibration_load_config(&previous_calibration_info, false);
		if (rc) {
			etc_calibration_packet_no_status(calibration_status.pre_adjustment,
							 sizeof(calibration_status.pre_adjustment));
		}
	}

	if (rc == 0) {
		/* Get pre-adjustment without calibration */
		rc = etc_calibration_packet_status(
			&previous_calibration_info, &hw_raw_adc,
			calibration_status.pre_adjustment,
			sizeof(calibration_status.pre_adjustment));
		__ASSERT_NO_MSG(rc == 0);
	}

	/* Get post-adjustment with calibration */
	rc = etc_calibration_packet_status(&calibration_info, &hw_raw_adc,
					   calibration_status.post_adjustment,
					   sizeof(calibration_status.post_adjustment));
	__ASSERT_NO_MSG(rc == 0);

	/* Write reference values */
	rc = etc_calibration_packet_ref(calibration_status.reference,
		sizeof(calibration_status.reference));
	__ASSERT_NO_MSG(rc == 0);

	/* Get current calibration time to report */
	date_time_now(&time_now);
	calibration_info.time = time_now / 1000;

	calibration_status.status = ETC_SENSOR_CALIB_IN_PROCESS;
	int msg_len = 0;
	for (int i = TEMP_RANGE_SW2; i <= TEMP_RANGE_SW5; i++) {
		etc_sensor_calibration_set_gpio_mask(i);
		rc = etc_sensor_calibration_read_adc(&raw_data);
		for (int j = SENSOR_INPUT_IN1; j <= SENSOR_INPUT_IN4; j++) {
			float temp_value = etc_sensor_calibration_convert_temperature(
				raw_data.port[j], &hw_raw_adc, &calibration_info);
			LOG_DBG("[%d] ADC %d - Temp %f", i, raw_data.port[j], temp_value);
			if (!IS_TEMP_IN_RANGE(i, temp_value)) {
				LOG_ERR("The temperature %f - switch %d is not valid range",
					temp_value, i);
				rc = -EINVAL;
				calibration_status.status = ETC_SENSOR_CALIB_POSTCALIB_VALUE_CHECK;
				calibration_status.result = ETC_SENSOR_CALIB_CALIB_FAIL;
				goto done;
			}
		}
	}

	etc_device_write_setting(ETC_CALIBRATION_USER_OFFSET_ID, &calibration_info.offset,
				 sizeof(calibration_info.offset));
	etc_device_write_setting(ETC_CALIBRATION_USER_RAWHIGH_ID, &calibration_info.high,
				 sizeof(calibration_info.high));
	etc_device_write_setting(ETC_CALIBRATION_USER_REF_ID, &calibration_info.ref,
				 sizeof(calibration_info.ref));
	etc_device_write_setting(ETC_CALIBRATION_USER_TIME_REF_ID, &calibration_info.time,
				 sizeof(calibration_info.time));
	etc_calibration_packet_calibrator_sn(calibration_info.id, sizeof(calibration_info.id));
	etc_device_write_setting(ETC_CALIBRATOR_USER_ID, calibration_info.id, sizeof(calibration_info.id));
	calibration_status.result = ETC_SENSOR_CALIB_SUCCESS;
done:
	calibration_status.status = ETC_SENSOR_CALIB_DATA_UPLOAD;
	etc_calibration_unlock();
	return rc;
}

void etc_calibration_lock(void)
{
	k_mutex_lock(&etc_calibration_mutex, K_FOREVER);
}

void etc_calibration_unlock(void)
{
	k_mutex_unlock(&etc_calibration_mutex);
}

int etc_calibration_load_config(struct etc_sensor_adc_calibration_info *info, bool user)
{
	__ASSERT_NO_MSG(info != NULL);
	int rc = 0;
	if (user) {
		rc = etc_device_read_setting(ETC_CALIBRATION_USER_OFFSET_ID, &info->offset,
					     sizeof(info->offset));
		if (rc) {
			LOG_ERR("Can't load the user calibration for offset");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_USER_RAWHIGH_ID, &info->high,
					     sizeof(info->high));
		if (rc) {
			LOG_ERR("Can't load the user calibration for raw high offset");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_USER_REF_ID, &info->ref,
					     sizeof(info->ref));
		if (rc) {
			LOG_ERR("Can't load the user calibration for reference");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_USER_TIME_REF_ID, &info->time,
					     sizeof(info->time));
		if (rc) {
			LOG_ERR("Can't load the user calibration for date/time");
			rc = 0;
		}
		rc = etc_device_read_setting(ETC_CALIBRATOR_USER_ID, &info->id, sizeof(info->id));
		if (rc) {
			LOG_ERR("Can't load the user calibration for calibrator id");
			memset(info->id, 0, sizeof(info->id));
			rc = 0;
		}
	} else {
		rc = etc_device_read_setting(ETC_CALIBRATION_OFFSET_ID, &info->offset,
					     sizeof(info->offset));
		if (rc) {
			LOG_ERR("Can't load the factory calibration for offset");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_RAWHIGH_ID, &info->high,
					     sizeof(info->high));
		if (rc) {
			LOG_ERR("Can't load the factory calibration for raw high offset");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_REF_ID, &info->ref, sizeof(info->ref));
		if (rc) {
			LOG_ERR("Can't load the factory calibration for reference");
			return rc;
		}
		rc = etc_device_read_setting(ETC_CALIBRATOR_ID, &info->id, sizeof(info->id));
		if (rc) {
			LOG_ERR("Can't load the factory calibration for calibrator id");
			memset(info->id, 0, sizeof(info->id));
			rc = 0;
		}
		rc = etc_device_read_setting(ETC_CALIBRATION_TIME_ID, &info->time,
					     sizeof(info->time));
		if (rc) {
			LOG_ERR("No factory calibration for date/time");
			info->time = 0;
			rc = 0;
		}
	}
	return rc;
}

void etc_calibration_get_current_status(struct etc_sensor_calibration_status_info *status)
{
	k_mutex_lock(&etc_calibration_mutex, K_FOREVER);
	memcpy(status, &calibration_status, sizeof(calibration_status));
	k_mutex_unlock(&etc_calibration_mutex);
}

int etc_calibration_get_calibration_status(void)
{
	int current_status = 0;
	k_mutex_lock(&etc_calibration_mutex, K_FOREVER);
	current_status = calibration_status.status;
	k_mutex_unlock(&etc_calibration_mutex);
	return current_status;
}

int etc_calibration_get_calibration_result(void)
{
	int current_result = 0;
	k_mutex_lock(&etc_calibration_mutex, K_FOREVER);
	current_result = calibration_status.result;
	k_mutex_unlock(&etc_calibration_mutex);
	return current_result;
}

void etc_calibration_exit(void)
{
	k_mutex_lock(&etc_calibration_mutex, K_FOREVER);
	calibration_status.status = ETC_SENSOR_CALIB_IDLE;
	etc_sensor_calibration_exit();
	k_mutex_unlock(&etc_calibration_mutex);
}

#ifdef CONFIG_CALIBRATION_MODULE_SHELL
#include <zephyr/shell/shell.h>

/**
 * @brief Initialize the calibration process.
 *
 * This command initializes the calibration module and provides feedback to the shell.
 */
static int cmd_calibration_init(const struct shell *shell, size_t argc, char **argv)
{
	shell_print(shell, "Initialized!");
	etc_calibration_init();
	etc_calibration_check();
	return 0;
}

/**
 * @brief Run the calibration process.
 *
 * This command starts executing the calibration processes and provides feedback to the shell.
 */
static int cmd_calibration_run(const struct shell *shell, size_t argc, char **argv)
{
	shell_print(shell, "Run!");
	etc_calibration_run();
	return 0;
}

/**
 * @brief Test calibration I/O (toggle the LED for calibration 3)
 *
 * Placeholder function for I/O testing during calibration.
 */
static int cmd_calibration_io_test(const struct shell *shell, size_t argc, char **argv)
{
	etc_sensor_calibration_set_gpio_mask(atoi(argv[1]));
	return 0;
}

/**
 * @brief Test calibration ADC.
 *
 * This command sets a GPIO mask and reads the temperature from the ADC.
 */
static int cmd_calibration_adc_test(const struct shell *shell, size_t argc, char **argv)
{
	struct etc_sensor_adc_raw_data raw_data = {0};
	for (int i = 0; i <= 5; i++) {
		etc_sensor_calibration_set_gpio_mask(i);
		k_msleep(10);
		etc_sensor_calibration_read_adc(&raw_data);
		LOG_DBG("%d %d %d %d", raw_data.port[0], raw_data.port[1], raw_data.port[2], raw_data.port[3]);
	}
	return 0;
}

/**
 * @brief Read temperature from sensor via OneWire interface.
 *
 * This command retrieves the temperature from the sensor using a OneWire protocol.
 */
static int cmd_calibration_sensor_onewire(const struct shell *shell, size_t argc, char **argv)
{
	shell_print(shell, "Temperature sensor one-wire %f",
		    etc_sensor_calibration_read_temperature_from_sensor());
	return 0;
}

static int cmd_calibration_sensor_dump_status(const struct shell *shell, size_t argc, char **argv)
{
	shell_print(shell, "Pre %s", calibration_status.pre_adjustment);
	shell_print(shell, "Post %s", calibration_status.post_adjustment);
	shell_print(shell, " Ref %s", calibration_status.reference);
	return 0;
}

/* Define shell commands and their descriptions */
SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_calibration,
	SHELL_CMD(init, NULL, "Initialize the calibration module.", cmd_calibration_init),
	SHELL_CMD(run, NULL, "Execute the calibration sequence.", cmd_calibration_run),
	SHELL_CMD(io, NULL, "Perform a calibration I/O test.", cmd_calibration_io_test),
	SHELL_CMD(adc, NULL, "Test calibration using ADC readings. Usage: adc <gpio_mask>",
		  cmd_calibration_adc_test),
	SHELL_CMD(report, NULL, "Test calibration using ADC readings. Usage: adc <gpio_mask>",
		  cmd_calibration_sensor_dump_status),
	SHELL_CMD(sensor, NULL, "Read temperature from the sensor using OneWire.",
		  cmd_calibration_sensor_onewire),
	SHELL_SUBCMD_SET_END);

/* Register the shell command set */
SHELL_CMD_REGISTER(calibration, &sub_calibration, "Command set for running calibration tasks.",
		   NULL);

#endif
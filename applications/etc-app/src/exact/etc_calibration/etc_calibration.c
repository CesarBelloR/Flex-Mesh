#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_calibration, CONFIG_ETC_CALIBRATION_LOG_LEVEL);

#include "etc_device.h"
#include "etc_sensor.h"
#include "etc_sensor_helper.h"
#include "etc_calibration.h"

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
#define RANGE(value) { (value) - 0.24, (value) + 0.24 }

// Structure representing a temperature range with minimum and maximum values.
struct temperature_range {
    float min;
    float max;
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
	{18.0, 28.0}, // Specific range with explicit values.
	RANGE(0.2),   // Range around 0.2 with ±0.24 tolerance for switch 2
	RANGE(25.0),  // Range around 25.0 with ±0.24 tolerance for switch 3
	RANGE(44.6),  // Range around 44.6 with ±0.24 tolerance for switch 4
	RANGE(70.4),  // Range around 70.4 with ±0.24 tolerance for switch 5
};

// Enumration for analog calibration list switch
enum analog_range_list {
	ADC_RANGE_SW1,
	ADC_RANGE_SW6
};

// Constant array of ADC ranges used for calibration testing.
static const struct adc_range list_adc[] = {
    { -100, 100}, 
    { 3914, 4114}
};

// Macro to check if a given temperature is within a specified temperature range index in list_temperature.
#define IS_TEMP_IN_RANGE(index, temp) \
    ((temp) >= list_temperature[index].min && (temp) <= list_temperature[index].max)

// Macro to check if a given ADC value is within a specified ADC range index in list_adc.
#define IS_ADC_IN_RANGE(index, adc) \
    ((adc) >= list_adc[index].min && (adc) <= list_adc[index].max)

// Mutex to block the sensor processing
K_MUTEX_DEFINE(etc_calibration_mutex);

// Boolean flag indicating whether the calibration process is ready to proceed.
static bool etc_calibration_ready = false;

void etc_calibration_init(void) 
{
	etc_sensor_calibration_enter();
}

int etc_calibration_check(void) 
{
	etc_calibration_lock();
	if (!etc_calibration_ready) {
		etc_calibration_ready = true;
		etc_calibration_init();
	}

	int rc = etc_sensor_calibration_scan();
	if (rc == 0) {
		LOG_ERR("No calibration tool is here");
		goto done;
	}

	rc = etc_sensor_calibration_read_code();
	if (rc) {
		LOG_ERR("No calibration code!");
	}

done:
	if (rc) {
		etc_calibration_unlock();
	}
	return 0;
}

int etc_calibration_run(void) 
{
	
	int rc = 0;
	struct etc_sensor_adc_calibration_info calibration_info = {
		.high = 0.0,
		.loaded = true, // Override for calibrator
		.offset = 0.0,
		.ref = 3948.75};
	
	uint16_t hw_raw_adc = 0;

	etc_calibration_lock();

	if (!etc_calibration_ready) {
		etc_calibration_ready = true;
		etc_calibration_init();
	}

	float current_temp = etc_sensor_calibration_read_temperature_from_sensor();
	if (!IS_TEMP_IN_RANGE(TEMP_RANGE_AMBIENT, current_temp)) {
		LOG_ERR("The current temperature is not in range");
		rc = -EINVAL;
		goto done;
	}
	etc_sensor_calibration_set_gpio_mask(SW_STATE_ADC_OFFSET);
	rc = etc_sensor_calibration_read_adc();
	if (!IS_ADC_IN_RANGE(ADC_RANGE_SW1, rc)) {
		LOG_ERR("The current adc is not in range");
		rc = -EINVAL;
		goto done;
	}
	/* Save in RAM offset */
	calibration_info.offset = (float)(rc);

	etc_sensor_calibration_set_gpio_mask(SW_STATE_ADC_HIGH);
	rc = etc_sensor_calibration_read_adc();
	if (!IS_ADC_IN_RANGE(ADC_RANGE_SW6, rc)) {
		LOG_ERR("The current adc is not in range");
		rc = -EINVAL;
		goto done;
	}
	/* Save in RAM high */
	calibration_info.high = (float)rc;
	/* Save in RAM ADC version HW */
	hw_raw_adc = etc_sensor_calibration_get_hw_version_adc();
	etc_sensor_calibration_save_temperature_compensation(hw_raw_adc);

	for (int i = TEMP_RANGE_SW2; i <= TEMP_RANGE_SW5; i++) {
		etc_sensor_calibration_set_gpio_mask(i);
		rc = etc_sensor_calibration_read_adc();
		float temp_value = etc_sensor_calibration_convert_temperature(rc, &hw_raw_adc,
									      &calibration_info);
		LOG_DBG("[%d] ADC %d - Temp %f", i, rc, temp_value);
		if (!IS_TEMP_IN_RANGE(i, temp_value)) {
			LOG_ERR("The temperature %f - switch %d is not valid range", temp_value, i);
			rc = -EINVAL;
			goto done;
		}
	}

	etc_device_write_setting(ETC_CALIBRATION_USER_OFFSET_ID, &calibration_info.offset,
				 sizeof(calibration_info.offset));
	etc_device_write_setting(ETC_CALIBRATION_USER_RAWHIGH_ID, &calibration_info.high,
				 sizeof(calibration_info.high));
	etc_device_write_setting(ETC_CALIBRATION_USER_REF_ID, &calibration_info.ref,
				 sizeof(calibration_info.ref));
	rc = 0;
done:
	etc_calibration_unlock();
	return rc;
}

void etc_calibration_exit(void) 
{
	etc_calibration_lock();
	if (etc_calibration_ready) {
		etc_calibration_ready = false;
		etc_sensor_calibration_exit();
	}
	etc_calibration_unlock();
}

void etc_calibration_lock(void)
{
	k_mutex_lock(&etc_calibration_mutex, K_FOREVER);
}

void etc_calibration_unlock(void)
{
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
    if (argc > 1) {
        etc_sensor_calibration_set_gpio_mask(atoi(argv[1]));
        shell_print(shell, "ADC channel %d - %d",  atoi(argv[1]), etc_sensor_calibration_read_adc());
    } else {
        shell_print(shell, "Error: Please provide a GPIO mask.");
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
    shell_print(shell, "Temperature sensor one-wire %f", etc_sensor_calibration_read_temperature_from_sensor());
    return 0;
}

/* Define shell commands and their descriptions */
SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_calibration,
    SHELL_CMD(init, NULL, "Initialize the calibration module.", cmd_calibration_init),
    SHELL_CMD(run, NULL, "Execute the calibration sequence.", cmd_calibration_run),
    SHELL_CMD(io, NULL, "Perform a calibration I/O test.", cmd_calibration_io_test),
    SHELL_CMD(adc, NULL, "Test calibration using ADC readings. Usage: adc <gpio_mask>", cmd_calibration_adc_test),
    SHELL_CMD(sensor, NULL, "Read temperature from the sensor using OneWire.", cmd_calibration_sensor_onewire),
    SHELL_SUBCMD_SET_END
);

/* Register the shell command set */
SHELL_CMD_REGISTER(calibration, &sub_calibration, "Command set for running calibration tasks.", NULL);

#endif
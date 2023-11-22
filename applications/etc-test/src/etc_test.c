#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <version.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/usb/usb_device.h>
#include <ctype.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>
#include <stdio.h>
#include "pcf85263a.h"
#include "adc.h"
#include "sensor.h"
#include "ds2484.h"
#include "ds18b20.h"
#include "etc_cape.h"
#include "watchdog.h"
#ifdef CONFIG_BQ25618
#include "bq25618.h"
#endif
#ifdef CONFIG_BQ24195
#include "bq24195.h"
#endif
#include "etc_device.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test, CONFIG_ETC_TEST_LOG_LEVEL);

#define DEFAULT_RADIO_NODE DT_ALIAS	(lora0)
#define DEFAULT_LORA_FREQUENCY_HZ	915000000LU
#define LORA_FREQ_US_MIN_HZ		902300000LU
#define LORA_FREQ_US_MAX_HZ		927500000LU

 
/* Outputs */
static const struct gpio_dt_spec hall_dt =
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(hall_int), control_gpios, 0);
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
static const struct gpio_dt_spec sense_enable_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sense_enable), control_gpios, 0);
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
static const struct gpio_dt_spec onewire_slpz_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(onewire_slpz), control_gpios, 0);
#endif
static const struct gpio_dt_spec s0_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
static const struct gpio_dt_spec s1_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel1), control_gpios, 0);
static const struct gpio_dt_spec vsens_enable_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(vsens_enable), control_gpios, 0);
#if DT_NODE_HAS_PROP(DT_NODELABEL(quectel_bg95), mdm_on_off_gpios)
static const struct gpio_dt_spec lte_on_off_gpio_dt =
		GPIO_DT_SPEC_GET(DT_NODELABEL(quectel_bg95), mdm_on_off_gpios);
#endif
static const struct gpio_dt_spec power_gpio_dt =
		GPIO_DT_SPEC_GET(DT_NODELABEL(quectel_bg95), mdm_power_gpios);
static const struct gpio_dt_spec pon_trig_gpio_dt =
		GPIO_DT_SPEC_GET(DT_NODELABEL(quectel_bg95), mdm_pon_trig_gpios);
#if DT_NODE_EXISTS(DT_NODELABEL(modem_uart_oe))
static const struct gpio_dt_spec modem_uart_oe_dt =
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(modem_uart_oe), control_gpios, 0);
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
static const struct gpio_dt_spec hw_wdt_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(hw_wdt), control_gpios, 0);
#endif
static const struct device *ext_flash = DEVICE_DT_GET(DT_NODELABEL(mx25r1635));
static const struct pwm_dt_spec pwm_led0 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));
static const struct pwm_dt_spec pwm_led1 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led1));
static const struct pwm_dt_spec pwm_led2 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2));

/* Inputs */
static const struct gpio_dt_spec rtc_int_dt =
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(rtc_int), control_gpios, 0);
static const struct gpio_dt_spec psm_ind_gpio_dt =
		GPIO_DT_SPEC_GET(DT_NODELABEL(quectel_bg95), mdm_psm_ind_gpios);

const struct device* dev_gpio = NULL;
const struct device* dev_lora = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
static struct k_mutex lora_mutex;
static struct gpio_callback hall_cb;
static struct k_sem lora_sem;

volatile enum lora_action {
	LORA_ACTION_HALL_TRIGGERED,
	LORA_ACTION_RX,
	LORA_ACTION_RX_TX,
} lora_action;

struct adc_calibration_info {
	float offset;
	float high;
	float ref;
};

static struct adc_calibration_info adc_calib_info;
static int cmd_lora_tx_rx(const struct shell *shell, size_t argc, char **argv);
static int lora_rx(void);

void lora_tx_rx_fn() {
	k_sem_init(&lora_sem, 0, 1);

	while (k_sem_take(&lora_sem, K_FOREVER) == 0) {
		if (lora_action == LORA_ACTION_HALL_TRIGGERED) {
			LOG_INF("Hall triggered");
#if IS_ENABLED(CONFIG_LORA_MSG_IN_HALL_EVENT)
			LOG_INF("Send Lora message");
			shell_execute_cmd(shell_backend_uart_get_ptr(), "lora");
#endif
		} else if (lora_action == LORA_ACTION_RX) {
			lora_rx();
		}
	}
}

K_THREAD_DEFINE(lora_tx_rx, 2048, lora_tx_rx_fn, NULL, NULL, NULL, 5, 0, 0);

static void hall_cb_fn(const struct device *dev,
		struct gpio_callback *cb, uint32_t pins)
{
	lora_action = LORA_ACTION_HALL_TRIGGERED;
	k_sem_give(&lora_sem);
}


void etc_test_init(void) 
{
	int ret;

	if (!device_is_ready(dev_lora)) {
		return;
	}

	sensor_init();
	k_mutex_init(&lora_mutex);
	// Configure hall interrupt
	gpio_pin_configure_dt(&hall_dt, GPIO_INPUT | GPIO_ACTIVE_LOW);
	gpio_pin_configure_dt(&vsens_enable_dt, GPIO_OUTPUT_ACTIVE);
	gpio_init_callback(&hall_cb, hall_cb_fn, BIT(hall_dt.pin));
	ret = gpio_add_callback(hall_dt.port, &hall_cb);
	if (ret < 0) {
		LOG_ERR("Failed to set gpio callback!");
	}
	gpio_pin_interrupt_configure_dt(&hall_dt, GPIO_INT_EDGE_TO_ACTIVE);
	
	sensor_adc_switch_channel(SENSOR_INPUT_AMBIENT);
}

static int cmd_version(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "ETC test version %s", CONFIG_TEST_VERSION);

	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_version, NULL, "Show kernel version", cmd_version, 1, 0);

static void adc_print_channel(const struct shell *shell, int channel, bool converted)
{
	int adc_raw = sensor_get_raw_value(channel);
	int val_mv;
	float val;

	if ((channel == ETC_ADC_CHANNEL_AMB) || (channel == ETC_ADC_CHANNEL_SENSOR)) {
#if defined(CONFIG_NTC_USE_TABLE)
		extern const float table_ntc_resistance_temp[];
		extern const int table_offset;
		extern const int table_length;
		val =  sensor_ntc_converter(table_ntc_resistance_temp, table_length, table_offset, adc_raw);
#else
		val = sensor_ntc_converter(channel, adc_raw);
#endif
		if (converted) {
			shell_print(shell, "ADC Channel %d - Value %d - Temperature %.2f deg C", 
					channel, adc_raw, val);
		} else {
			shell_print(shell, "ADC Channel %d - Value %d:  %d", 
					channel, channel, adc_raw);
		}
	} else {
		val_mv = adc_raw;
		adc_get_raw_to_millivolts(channel, &val_mv);
		val = (float)val_mv / 1000.0f;
		if (converted) {
			shell_print(shell, "ADC Channel %d - Value %d - Voltage %.2f V",
					channel, adc_raw, val);
		} else {
			shell_print(shell, "ADC Channel %d - Value %d: %d",
					channel, channel, adc_raw);
		}

	}
}

static void adc_print_channel_raw_calibration(const struct shell *shell, int channel, bool converted)
{
	int adc_raw = sensor_get_raw_value(channel);
	int calibrated_value = (int)(((float)(adc_raw) - adc_calib_info.offset) / 
		(adc_calib_info.high - adc_calib_info.offset) * adc_calib_info.ref);
	int val_mv;
	float val;

	if ((channel == ETC_ADC_CHANNEL_AMB) || (channel == ETC_ADC_CHANNEL_SENSOR)) {
#if defined(CONFIG_NTC_USE_TABLE)
		extern const float table_ntc_resistance_temp[];
		extern const int table_offset;
		extern const int table_length;
		val =  sensor_ntc_converter(table_ntc_resistance_temp, table_length, table_offset, calibrated_value);
#else
		val = sensor_ntc_converter(channel, calibrated_value);
#endif
		if (converted) {
			shell_print(shell, "Calibrated ADC Channel %d - Value %d - Temperature %.2f deg C", 
					channel, calibrated_value, val);
		} else {
			shell_print(shell, "Calibrated ADC Channel %d - Value %d: %d ", 
					channel, channel,  calibrated_value);
		}
	} else {
		val_mv = calibrated_value;
		adc_get_raw_to_millivolts(channel, &val_mv);
		val = (float)val_mv / 1000.0f;
		if (converted) {
			shell_print(shell, "Calibrated ADC Channel %d - Value %d - Voltage %.2f V",
					channel, calibrated_value, val);
		} else {
			shell_print(shell, "Calibrated ADC Channel %d - Value %d: %d",
					channel, channel, calibrated_value);
		}

	}
}

struct args_index {
	uint8_t port;
	uint8_t index;
	uint8_t mode;
	uint8_t value;
};

static const struct args_index args_indx = {
	.port = 1,
	.index = 2,
	.mode = 3,
	.value = 3,
};

static int cmd_etc_io(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 3) {
		shell_print(shell, "Syntax: etc_io <io id> 0/1");
		shell_print(shell, "Active-low I/Os will be set to active state on 1 (low output)");
		shell_print(shell, "IO as below:");
		shell_print(shell, "\t SENS_ENABLE[0.2.0]    -> ID: 0");
		shell_print(shell, "\t 1W_SLPZ[0.3.0]        -> ID: 0");
		shell_print(shell, "\t LTE_PWRKEY            -> ID: 1");
		shell_print(shell, "\t VSEN_EN               -> ID: 2");
		shell_print(shell, "\t LTE_PON_TRIG          -> ID: 3");
		shell_print(shell, "\t MODEM_UART_OE         -> ID: 4");
		shell_print(shell, "\t SENS_SEL0             -> ID: 5");
		shell_print(shell, "\t SENS_SEL1             -> ID: 6");
		shell_print(shell, "\t LTE_ON_OFF[0.2.0]     -> ID: 7");
		shell_print(shell, "\t HW_WDT[0.3.0]         -> ID: 7");
		shell_print(shell, "\t LTE_PON_TRIG          -> ID: 8");
		return 0;
	}

	int id = atoi(argv[1]);
	int level = atoi(argv[2]);
	
	if (level != 0 && level != 1) {
		shell_error(shell, "Unsupported level %d", level);
		return 0;
	}

	const struct gpio_dt_spec *gpio_dt;
	char* name = NULL;
	switch (id) {
	case 0:
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
		gpio_dt = &sense_enable_dt;
		name = "SENS_ENABLE";
		break;
#elif DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
		gpio_dt = &onewire_slpz_dt;
		name = "1W_SLPZ";
		break;
#else
		shell_error(shell, "Not supported");
		return 0;
#endif
	case 1:
		gpio_dt = &power_gpio_dt;
		name = "LTE_PWRKEY";
		break;
	case 2:
		gpio_dt = &vsens_enable_dt;
		name = "VSEN_EN";
		break;
	case 3:
		gpio_dt = &pon_trig_gpio_dt;
		name = "LTE_PON_TRIG";
		break;
	case 4:
#if DT_NODE_EXISTS(DT_NODELABEL(modem_uart_oe))
		gpio_dt = &modem_uart_oe_dt;
		name = "GPIO24";
		break;
#else
		shell_error(shell, "Not supported");
		return 0;
#endif
	case 5:
		gpio_dt = &s0_dt;
		name = "SENS_SEL0";
		break;
	case 6:
		gpio_dt = &s1_dt;
		name = "SENS_SEL1";
		break;
	case 7:
#if DT_NODE_HAS_PROP(DT_NODELABEL(quectel_bg95), mdm_on_off_gpios)
		gpio_dt = &lte_on_off_gpio_dt;
		name = "LTE_ON_OFF";
		break;
#elif DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
		gpio_dt = &hw_wdt_dt;
		name = "HW_WDT";
		break;
#else
		shell_error(shell, "Not supported");
		return 0;
#endif
	case 8:
		gpio_dt = &pon_trig_gpio_dt;
		name = "LTE_PON_TRIG";
		break;
	default:
		shell_error(shell, "Invalid ID %d", id);
		return 0;
	}

	shell_print(shell, "Set %s (%s.%02d) level %d", name, gpio_dt->port->name, gpio_dt->pin, level);
	gpio_pin_configure_dt(gpio_dt, GPIO_OUTPUT);
	gpio_pin_set_dt(gpio_dt, level == 0 ? 0 : 1U);
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_io, NULL, "Set IO", cmd_etc_io, 0, 0);

#if 0
static int cmd_set_etc_gpio(const struct shell *sh,
			    size_t argc, char **argv)
{
	const struct device *dev;
	uint8_t index = 0U;
	uint8_t value = 0U;

	if (argc != 4) {
		shell_print(sh, "Usage:\n"
				"  %s <port> <pin> <value>\n"
				"  value: 1 disconnect, 0 drive low",
			    argv[0]);
		return -EINVAL;
	}

	if (isdigit((unsigned char)argv[args_indx.index][0]) &&
	    isdigit((unsigned char)argv[args_indx.value][0])) {
		index = (uint8_t)atoi(argv[args_indx.index]);
		value = (uint8_t)atoi(argv[args_indx.value]);
	} else {
		shell_print(sh, "Wrong parameters for set");
		return -EINVAL;
	}
	dev = device_get_binding(argv[args_indx.port]);

	if (dev != NULL) {
		index = (uint8_t)atoi(argv[2]);
		if (value) {
			gpio_pin_configure(dev, index, GPIO_DISCONNECTED);
			shell_print(sh, "Disconnecting %s pin %d",
				    argv[args_indx.port], index);
		} else {
			gpio_pin_configure(dev, index, GPIO_OUTPUT);
			gpio_pin_set(dev, index, value);
			shell_print(sh, "Writing to %s pin %d",
				    argv[args_indx.port], index);
		}
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_etc_gpio,
			       SHELL_CMD_ARG(set, NULL, "Set GPIO: 1 disconnect, 0 drive low", cmd_set_etc_gpio, 1, 3),
			       SHELL_SUBCMD_SET_END /* Array terminated. */
			       );
SHELL_CMD_REGISTER(etc_gpio, &sub_etc_gpio, "ETC GPIO commands", NULL);	
#endif		       

static void adc_print_all_channels(const struct shell *shell, bool converted) 
{
	for (int chan = 0; chan < ETC_ADC_CHANNEL_MAX; chan++) {
		if (chan == ETC_ADC_CHANNEL_SENSOR) {
			for (int input = 0; input < (SENSOR_INPUT_MAX - SENSOR_INPUT_IN1); input++) {
				sensor_adc_switch_channel(input);
				k_msleep(100);
				shell_print(shell, "Probe %u:", (input + SENSOR_INPUT_IN1));
				adc_print_channel(shell, chan, converted);
			}
		} else {
			adc_print_channel(shell, chan, converted);
		}
	}
}

static void adc_print_all_channels_calibration_raw(const struct shell *shell) 
{
	for (int chan = 0; chan < ETC_ADC_CHANNEL_MAX; chan++) {
		if (chan == ETC_ADC_CHANNEL_SENSOR) {
			for (int input = 0; input < (SENSOR_INPUT_MAX - SENSOR_INPUT_IN1); input++) {
				sensor_adc_switch_channel(input);
				k_msleep(100);
				shell_print(shell, "Probe %u:", (input + SENSOR_INPUT_IN1));
				adc_print_channel_raw_calibration(shell, chan, false);
			}
		} else {
			adc_print_channel_raw_calibration(shell, chan, false);
		}
	}
}

static void adc_print_all_channels_calibration_converted(const struct shell *shell) 
{
	for (int chan = 0; chan < ETC_ADC_CHANNEL_MAX; chan++) {
		if (chan == ETC_ADC_CHANNEL_SENSOR) {
			for (int input = 0; input < (SENSOR_INPUT_MAX - SENSOR_INPUT_IN1); input++) {
				sensor_adc_switch_channel(input);
				k_msleep(100);
				shell_print(shell, "Sensor input %u:", input);
				adc_print_channel_raw_calibration(shell, chan, true);
			}
		} else {
			adc_print_channel_raw_calibration(shell, chan, true);
		}
	}
}

static int cmd_adc_request(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_print(shell, 
			    "Usage:\n"
			    "%s <channel>\n"
			    "channel: - all\n"
			    "         - value between 0 and 3", argv[0]);
		return -EINVAL;
	}

	if (strstr(argv[1], "all") != NULL) {
		adc_print_all_channels_calibration_raw(shell);
	} else {
		int channel = atoi(argv[1]);
		adc_print_channel_raw_calibration(shell, channel, false);
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_adc, NULL, "Get ADC raw data", cmd_adc_request, 1, 1);

static int cmd_adc_start_calibration(const struct shell *shell, size_t argc, char **argv)
{
	/* Start calibration to CHANNEL AMB */
	/* It will process 2 events -> CALIBRATION DONE -> EVENT END */
	int adc = adc_set_start_calibration(ETC_ADC_CHANNEL_AMB);
	shell_print(shell, "Calibration status %s", adc != -1 ? "okay" : "not okay");
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_adc_cal, NULL, "Start ADC calibration process", cmd_adc_start_calibration, 0, 1);

static int cmd_adc_load_calibration(const struct shell *shell, size_t argc, char **argv)
{
	int rc = 0;
	rc = etc_device_read_setting(ETC_CALIBRATION_OFFSET_ID, &adc_calib_info.offset, sizeof(adc_calib_info.offset));
	if (rc) {
		adc_calib_info.offset = 0;
		etc_device_write_setting(ETC_CALIBRATION_OFFSET_ID, &adc_calib_info.offset, sizeof(adc_calib_info.offset));
	}
	shell_print(shell, "The offset calibration: %.6f", adc_calib_info.offset);
	rc = etc_device_read_setting(ETC_CALIBRATION_RAWHIGH_ID, &adc_calib_info.high, sizeof(adc_calib_info.high));
	if (rc) {
		adc_calib_info.high = 4014.548130;
		etc_device_write_setting(ETC_CALIBRATION_RAWHIGH_ID, &adc_calib_info.high, sizeof(adc_calib_info.high));
	} 
	shell_print(shell, "The high raw calibration: %.6f", adc_calib_info.high);
	rc = etc_device_read_setting(ETC_CALIBRATION_REF_ID, &adc_calib_info.ref, sizeof(adc_calib_info.ref));
	if (rc) {
		adc_calib_info.ref = 4014.548130;
		etc_device_write_setting(ETC_CALIBRATION_REF_ID, &adc_calib_info.ref, sizeof(adc_calib_info.ref));
	} 
	shell_print(shell, "The reference calibration: %.6f", adc_calib_info.ref);

	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_adc_load_cal, NULL, "Load ADC calibration information", cmd_adc_load_calibration, 0, 1);

static int cmd_adc_set_offset(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_print(shell, 
			    "Usage:\n"
			    "%s <offset>\n"
			    "offset: The offset value for calibration calculation", argv[0]);
		return -EINVAL;
	}

	adc_calib_info.offset = atof(argv[1]);
	int rc = etc_device_write_setting(ETC_CALIBRATION_OFFSET_ID, &adc_calib_info.offset, sizeof(adc_calib_info.offset));
	if (rc) {
		shell_error(shell, "Failed to save calibration for offset to NVS %d", rc);
	} else {
		shell_print(shell, "Saved the calibration for offset to NVS successful");
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_adc_offset, NULL, "Set the offset value for the calibration calculation", cmd_adc_set_offset, 1, 1);

static int cmd_adc_set_high(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_print(shell, 
			    "Usage:\n"
			    "%s <high>\n"
			    "high: The raw high value for calibration calculation", argv[0]);
		return -EINVAL;
	}

	adc_calib_info.high = atof(argv[1]);
	int rc = etc_device_write_setting(ETC_CALIBRATION_RAWHIGH_ID, &adc_calib_info.high, sizeof(adc_calib_info.high));
	if (rc) {
		shell_error(shell, "Failed to save calibration for high to NVS %d", rc);
	} else {
		shell_print(shell, "Saved the calibration for high to NVS successful");
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_adc_high, NULL, "Set the RAW high value for the calibration calculation", cmd_adc_set_high, 1, 1);

static int cmd_adc_set_ref(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_print(shell, 
			    "Usage:\n"
			    "%s <ref>\n"
			    "ref: The raw ref value for calibration calculation", argv[0]);
		return -EINVAL;
	}

	adc_calib_info.ref = atof(argv[1]);
	int rc = etc_device_write_setting(ETC_CALIBRATION_REF_ID, &adc_calib_info.ref, sizeof(adc_calib_info.ref));
	if (rc) {
		shell_error(shell, "Failed to save calibration for reference to NVS %d", rc);
	} else {
		shell_print(shell, "Saved the calibration for reference to NVS successful");
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_adc_ref, NULL, "Set the reference value for the calibration calculation", cmd_adc_set_ref, 1, 1);

static int cmd_adc_get_raw(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_print(shell, 
			    "Usage:\n"
			    "%s <channel>\n"
			    "channel: - all\n"
			    "         - value between 0 and 3", argv[0]);
		return -EINVAL;
	}

	if (strstr(argv[1], "all") != NULL) {
		adc_print_all_channels(shell, false);
	} else {
		int channel = atoi(argv[1]);
		adc_print_channel(shell, channel, false);
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_adc_raw, NULL, "Get the RAW adc value", cmd_adc_get_raw, 0, 1);

static int cmd_adc_get_calibrated(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_print(shell, 
			    "Usage:\n"
			    "%s <channel>\n"
			    "channel: - all\n"
			    "         - value between 0 and 3", argv[0]);
		return -EINVAL;
	}

	if (strstr(argv[1], "all") != NULL) {
		adc_print_all_channels_calibration_converted(shell);
	} else {
		int channel = atoi(argv[1]);
		adc_print_channel_raw_calibration(shell, channel, true);
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_adc_calibrated, NULL, "Get the ADC result based on calibrated value", cmd_adc_get_calibrated, 0, 1);

static int cmd_vsens_enable(const struct shell *shell, size_t argc, char **argv) 
{
	int enable;

	if (argc != 2) {
		shell_print(shell, "Usage:\n"
				   "%s 0/1",
			    argv[0]);
		return -EINVAL;
	}

	enable = atoi(argv[1]);
	gpio_pin_set_dt(&vsens_enable_dt, enable);

	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_vsens_en, NULL, "Enable/disable VCC_SENS", cmd_vsens_enable, 1, 1);

static int cmd_ds2484_write(const struct shell *shell, size_t argc, char **argv)
{
	const char *usage =
		"Usage: write <byte>\n"
		"Hex and decimal format is accepted, e.g. 0x10 or 16";
	int ret = -EINVAL;
	uint8_t byte;
	char *p_end;

	if (argc != 2) {
		goto error;
	}

	byte = (uint8_t)strtol(argv[1], &p_end, 0);
	if (p_end == argv[1]) {
		goto error;
	}
	ret = ds2484_write_byte(byte);

	return ret;
error:
	shell_print(shell, "%s", usage);
	return ret;
}

static int cmd_ds2484_read(const struct shell *shell, size_t argc, char **argv)
{
	int ret;
	uint8_t byte;

	ret = ds2484_read_byte(&byte);
	if (ret == 0) {
		shell_print(shell, "Read: 0x%02X", byte);
	} else {
		shell_print(shell, "Error %d", ret);
	}

	return ret;
}

static int cmd_ds2484_req_reset(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	ret = ds2484_request_reset();
	if (ret != 0) {
		shell_print(shell, "Error %d", ret);
	}

	return ret;
}

static int cmd_ds2484_req_select(const struct shell *shell, size_t argc, char **argv)
{
	const char *usage =
		"Usage: select <rom>\n"
		"rom: Unique device address in hex or decimal\n"
		"     e.g. 0x31 or 231";
	int ret = -EINVAL;
	uint8_t rom;
	char *p_end;

	if (argc != 2) {
		goto error;
	}

	rom = (uint8_t)strtol(argv[1], &p_end, 0);
	if (p_end == argv[1]) {
		goto error;
	}

	ret = ds2484_request_select(&rom);

	return ret;
error:
	shell_print(shell, "%s", usage);
	return ret;
}

static int cmd_ds2484_search(const struct shell *shell, size_t argc, char **argv)
{
	int ret;
	char rom[DS2484_ROM_MAX_SIZE];

	ret = ds2484_request_search(rom);

	if (ret == 0) {
		shell_fprintf(shell, SHELL_NORMAL, "Found 0x");
		for (int i = DS2484_ROM_MAX_SIZE - 1; i >= 0; i--) {
			shell_fprintf(shell, SHELL_NORMAL, "%02X", rom[i]);
		}
		shell_print(shell, "");
	} else {
		shell_print(shell, "Error %d", ret);
	}

	return ret;
}

static int cmd_ds2484_status(const struct shell *shell, size_t argc, char **argv)
{
	int ret;
	uint8_t status;

	ret = ds2484_read_status(&status);
	if (ret == 0) {
		shell_print(shell, "Status 0x%02X", status);
	} else {
		shell_print(shell, "Error %d", ret);
	}

	return ret;
}

static int cmd_ds2484_reset(const struct shell *shell, size_t argc, char **argv)
{
	bool ret;
	ret = ds2484_device_reset();

	if (ret != 0) {
		shell_print(shell, "Error %d", ret);
	}
	return ret;
}

static int cmd_ds2484_config(const struct shell *shell, size_t argc, char **argv)
{
	const char *usage =
		"Usage: config [<bit> <1/0>]\n"
		"Bit values from 0 to 3 are valid.\n"
		"If used with bit and value, configuration is set.\n"
		"Otherwise, configuration is read and printed.";
	int ret = -EINVAL;
	int bit, enable;

	/* Read and print configuration register if only one argument given */
	if (argc == 1) {
		uint8_t config;
		ret = ds2484_get_config(&config);
		if (ret != 0) {
			shell_print(shell, "Error retrieving config");
			return ret;
		}
		shell_print(shell, "Config 0x%02X", config);
		return 0;
	}

	if (argc != 3) {
		goto error;
	}

	bit = atoi(argv[1]);
	enable = atoi(argv[2]);

	if ((bit > 3) || (bit < 0)) {
		goto error;
	}

	if (enable) {
		ds2484_set_config((ds248x_config_t)(1 << bit));
	} else {
		ds2484_clear_config((ds248x_config_t)(1 << bit));
	}

	return 0;
error:
	shell_print(shell, "%s", usage);
	return ret;
}

static int cmd_ds2484_enable(const struct shell *shell, size_t argc, char **argv)
{
	int ret;
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	gpio_pin_set_dt(&sense_enable_dt, 1U);
#endif
	k_sleep(K_SECONDS(1));

	ret = ds2484_init(DEVICE_DT_GET(DT_NODELABEL(i2c1)));
	shell_print(shell, "DS2484 enabled");
	if (ret == 0) {
		shell_print(shell, "Initialized");
	} else {
		shell_print(shell, "Initialization failed");
	}

	return 0;
}

static int cmd_ds2484_convert_temp(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t data[9];
	uint8_t MAX_ATTEMPTS = 10;
	uint8_t attempt = 0;
	bool success = false;
	uint16_t temperature;
	int8_t resolution, digit, minus = 0;
	float decimal;

	if (argc > 1) {
		MAX_ATTEMPTS = atoi(argv[1]);
	}

	while (attempt < MAX_ATTEMPTS && !success) {
		attempt++;
		if (ds2484_request_reset() != 0) {
			shell_print(shell, "Error issuing reset to 1-wire device");
			/* If reset fails, skip trying to read temperature and try again. */
			continue;
		}
		/* Send command to all devices*/
		if (ds2484_request_skip() != 0) {shell_print(shell, "Error sending ROM skip command");}		
		if (ds2484_write_byte(0x44) != 0) {shell_print(shell, "Error requesting temperature measurement");}
		if (ds2484_set_config((ds248x_config_t)(1 << 2)) != 0) {shell_print(shell, "Error setting strong pullup");}
		k_sleep(K_MSEC(800));
		if (ds2484_request_reset() != 0) {shell_print(shell, "Error issuing reset to 1-wire device");}
		/* Send command to all devices */
		if (ds2484_request_skip() != 0) {shell_print(shell, "Error sending ROM skip command");}	
		/* read scratch pad *this will not work if there is more than one device on the bus */
		if (ds2484_write_byte(0xBE) != 0) {shell_print(shell, "Error requesting data from scratch pad");}
		if (ds2484_read_bytes(data, 9) != 0) {shell_print(shell, "Error reading data from scratch pad");}
		
		/* check CRC to ensure reading was valid */
		uint8_t crc = 0;
		uint8_t len = 8;
		uint8_t *addr = data;
	
		while (len--) {
			uint8_t inbyte = *addr++;
			for (uint8_t i = 8; i; i--) {
				uint8_t mix = (crc ^ inbyte) & 0x01;
				crc >>= 1;
				if (mix) crc ^= 0x8C;
				inbyte >>= 1;
			}
		}
		
		if (crc != data[8]) {
			shell_print(shell, "Invalid CRC value returned: %u", crc);
			k_sleep(K_MSEC(10));
		} else {
			success = true;
		}
	}

	if (!success) {
		shell_print(shell, "Reading temperature failed!");
		return -1;
	}

	/* First two bytes of scratchpad are temperature values */
	temperature = data[0] | data[1] << 8;

	/* Check if temperature is negative */
	if (temperature & 0x8000) {
		/* Two's complement, temperature is negative */
		temperature = ~temperature + 1;
		minus = 1;
	}

	/* Get sensor resolution */
	resolution = ((data[4] & 0x60) >> 5) + 9;

	/* Store temperature integer digits and decimal digits */
	digit = temperature >> 4;
	digit |= ((temperature >> 8) & 0x7) << 4;

	/* Store decimal digits */
	switch (resolution) {
	case 9:
		decimal = (temperature >> 3) & 0x01;
		decimal *= (float)DS18B20_DECIMAL_STEPS_9BIT;
		break;
	case 10:
		decimal = (temperature >> 2) & 0x03;
		decimal *= (float)DS18B20_DECIMAL_STEPS_10BIT;
		break;
	case 11:
		decimal = (temperature >> 1) & 0x07;
		decimal *= (float)DS18B20_DECIMAL_STEPS_11BIT;
		break;
	case 12:
		decimal = temperature & 0x0F;
		decimal *= (float)DS18B20_DECIMAL_STEPS_12BIT;
		break;
	default:
		decimal = 0xFF;
		digit = 0;
	}

	/* Check for negative part */
	decimal = digit + decimal;
	if (minus) {
		decimal = 0 - decimal;
	}

	shell_print(shell, "Temperature: %02f°C --> %u attempts", decimal, attempt);

	if (success) {
		return 0;
	} else {
		shell_print(shell, "ERROR: Invalid temperature conversion");

		for (u_int8_t i = 0; i < 9; i++) {
			shell_print(shell, "data[%u]: %u", i, data[i]);
		}
		
		return -1;
	}
}

SHELL_STATIC_SUBCMD_SET_CREATE(ds2484_sub,
	SHELL_CMD(enable, NULL, "Enable DS2484", cmd_ds2484_enable),
	SHELL_CMD(status, NULL, "Get status register", cmd_ds2484_status),
	SHELL_CMD(search, NULL, "Start/continue search for devices", cmd_ds2484_search),
	SHELL_CMD_ARG(select, NULL, "Set the 1-wire address", cmd_ds2484_req_select, 1, 1),
	SHELL_CMD_ARG(write, NULL, "Write byte to 1-wire device", cmd_ds2484_write, 1, 1),
	SHELL_CMD(read, NULL, "Read byte from 1-wire device", cmd_ds2484_read),
	SHELL_CMD_ARG(config, NULL, "Get/set the config register", cmd_ds2484_config, 1, 2),
	SHELL_CMD(reset, NULL, "Reset DS2484", cmd_ds2484_reset),
	SHELL_CMD(req_reset, NULL, "Request a 1-wire reset", cmd_ds2484_req_reset),
	SHELL_CMD_ARG(get_temp, NULL, "Request temperature from a one device bus", cmd_ds2484_convert_temp, 1, 1),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(ds2484, &ds2484_sub, "DS2484 1-wire commands", NULL);

#ifdef CONFIG_BQ25618
static const struct device *bq25618_dev = DEVICE_DT_GET(DT_NODELABEL(bq25618));

static int cmd_bq25618_read_all(const struct shell *shell, size_t argc, char **argv)
{
	bq25618_print_all_registers(bq25618_dev);

	return 0;
}


static int cmd_bq25618_set_charge_current(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(shell, "Syntax: %s <current in mA>", argv[0]);
		return 0;
	}
	int ret;
	int current_ma = atoi(argv[1]);
	
	ret = bq25618_set_charge_current(bq25618_dev, current_ma);
	if (ret != 0) {
		shell_error(shell, "Error setting value");
	}
	
	return 0;
}

static int cmd_bq25618_set_input_current(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(shell, "Syntax: %s <current in mA>", argv[0]);
		return 0;
	}
	int ret;
	int current_ma = atoi(argv[1]);
	
	ret = bq25618_set_input_current_limit(bq25618_dev, current_ma);
	if (ret != 0) {
		shell_error(shell, "Error setting value");
	}
	
	return 0;
}


SHELL_STATIC_SUBCMD_SET_CREATE(bq25618_sub,
	SHELL_CMD(read_all, NULL, "Read and print all registers", cmd_bq25618_read_all),
	SHELL_CMD_ARG(set_charge_current, NULL, "Set charge current in mA", cmd_bq25618_set_charge_current, 1, 1),
	SHELL_CMD_ARG(set_input_current, NULL, "Set charge current in mA", cmd_bq25618_set_input_current, 1, 1),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(bq25618, &bq25618_sub, "BQ25618/9 PMIC commands", NULL);
#endif

#if CONFIG_BQ24195
static const struct device *bq24195_dev = DEVICE_DT_GET(DT_NODELABEL(bq24195));

static int cmd_bq24195_read_all(const struct shell *shell, size_t argc, char **argv)
{
	bq24195_print_all_registers(bq24195_dev);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(bq24195_sub,
	SHELL_CMD(read_all, NULL, "Read and print all registers", cmd_bq24195_read_all),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(bq24195, &bq24195_sub, "BQ25618/9 PMIC commands", NULL);
#endif

static int cmd_ui_request(const struct shell *shell, size_t argc, char **argv)
{	
	if (argv < 4) {
		shell_error(shell, "Failed to set the LED - Syntax error");
		shell_print(shell, "Syntax: etc_ui <red> <green> <blue>");
		return 0;
	}

	int red = atoi(argv[1]);
	int green = atoi(argv[2]);
	int blue = atoi(argv[3]);

	if ((0 <= red && red <= 255) && (0 <= blue && blue <= 255) || (0 <= green && green <= 255)) {
		pwm_set_dt(&pwm_led0, PWM_USEC(255), PWM_USEC(red));
		pwm_set_dt(&pwm_led1, PWM_USEC(255), PWM_USEC(green));
		pwm_set_dt(&pwm_led2, PWM_USEC(255), PWM_USEC(blue));
		shell_print(shell, "Set R %d - G %d - B %d", red, green, blue);
		return 0;
	} else {
		shell_error(shell, "Out of range for input value [0, 255] - %d %d %d", red, green, blue);
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_ui, NULL, "Set color UI", cmd_ui_request, 4, 0);

static int cmd_pcf85263_set_time(const struct shell *shell, size_t argc, char **argv)
{
	int utc_time = atoi(argv[1]);
	shell_print(shell, "Set time UTC %d", utc_time);
	pcf85263a_rtc_set_time(utc_time);
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_set_time, NULL, "Set RTC time in seconds since epoch", cmd_pcf85263_set_time, 2, 0);

static int cmd_pcf85263_get_time(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	time_t utc_time = 0;
	pcf85263a_rtc_get_time(&utc_time);
	shell_print(shell, "Get time UTC %d", (int)utc_time);
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_get_time, NULL, "Get RTC time in seconds since epoch", cmd_pcf85263_get_time, 1, 0);

#if DT_HAS_COMPAT_STATUS_OKAY(jedec_spi_nor)
#define FLASH_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(jedec_spi_nor)
#elif DT_HAS_COMPAT_STATUS_OKAY(nordic_qspi_nor)
#define FLASH_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(nordic_qspi_nor)
#else
#error Unsupported flash driver
#define FLASH_NODE DT_INVALID_NODE
#endif

static int cmd_external_flash_get_info(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	const struct device *dev = DEVICE_DT_GET(FLASH_NODE);

	if (!device_is_ready(dev)) {
		shell_error(shell, "%s: device not ready", dev->name);
		return 0;
	}

	uint8_t id[3] = {0x00};
	int rc = flash_read_jedec_id(dev, id);
	if (rc == 0) {
		shell_print(shell, "jedec-id = [%02x %02x %02x];\n",
		       id[0], id[1], id[2]);
	} else {
		shell_error(shell, "JEDEC ID read failed: %d", rc);
	}

	return 0;
}

static int cmd_external_flash_erase(const struct shell *shell, size_t argc, char **argv)
{
	struct flash_pages_info info;
	size_t page_count;
	int ret;

	if (!device_is_ready(ext_flash)) {
		shell_error(shell, "%s: device not ready", ext_flash->name);
		return 0;
	}

	ret = flash_get_page_info_by_idx(ext_flash, 0, &info);	
	if (ret) {
		LOG_ERR("Unable to get page info");
		goto error;
	}

	page_count = flash_get_page_count(ext_flash);
	if (page_count == 0) {
		LOG_ERR("Unable to get page count");
		goto error;
	}

	ret = flash_erase(ext_flash, 0x00, info.size * page_count);
	if (ret < 0) {
		goto error;
	}

	shell_print(shell, "External flash erased");
	return 0;

error:
	shell_error(shell, "Error erasing flash");
	return -1;
}

SHELL_STATIC_SUBCMD_SET_CREATE(etc_flash_sub,
	SHELL_CMD_ARG(info, NULL, "Get information", cmd_external_flash_get_info, 1, 0),
	SHELL_CMD_ARG(erase, NULL, "Wipe the external flash", cmd_external_flash_erase, 1, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(etc_flash, &etc_flash_sub, "External flash commands", NULL);

const struct device *lora_dev = NULL;
static struct lora_modem_config etc_lora_rx_config = {
	.frequency = DEFAULT_LORA_FREQUENCY_HZ,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_7,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 20,
	.tx = false,
};

static struct lora_modem_config etc_lora_tx_config  = {
	.frequency = DEFAULT_LORA_FREQUENCY_HZ,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_7,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 20,
	.tx = true,
};

static int send_lora_message(void)
{
	int ret;
	char msg_buf[sizeof("S,#####,MT1,99984,3.70,###,*,*,19.8,*,*,21.8,*,")];
	char encr_buf[sizeof(msg_buf) + 1];
	uint8_t msg_len;
	static uint8_t count = 0;

	ret = lora_config(dev_lora, &etc_lora_tx_config);
	if (ret < 0) {
		LOG_ERR("lora_config failed error %d", ret);
		return -1;
	}

	ret = snprintf(msg_buf, sizeof(msg_buf),
		       "S,9970,MT1,99984,3.70,%u,*,*,19.8,*,*,21.8,*,", count);			//placeholder device ID 99984
	msg_len = ret > sizeof(msg_buf) ? sizeof(msg_buf) : ret;
	etc_cape_encrypt(msg_buf, encr_buf, msg_len, 21);

	ret = lora_send(dev_lora, (uint8_t *)encr_buf, msg_len + 1);
	if (ret < 0) {
		LOG_ERR("lora_send failed error %d", ret);
	} else {
		LOG_DBG("Transmit data success, count %u", count);
	}

	count++;
	if (count >= 100) {
		count = 0;
	}
	return 0;
}

static int send_lora_message1(void)
{
	int ret;
	char msg_buf[sizeof("S,#####,MT2,AAAA,3.70,###,*,*,19.8,*,*,21.8,*,")];
	char encr_buf[sizeof(msg_buf) + 1];
	uint8_t msg_len;
	static uint8_t count = 0;

	ret = lora_config(dev_lora, &etc_lora_tx_config);
	if (ret < 0) {
		LOG_ERR("lora_config failed error %d", ret);
		return -1;
	}

	ret = snprintf(msg_buf, sizeof(msg_buf),
		       "S,AAAA,MT2,99984,3.70,%u,*,*,19.8,*,*,21.8,*,", count);			//placeholder device ID 99984
	msg_len = ret > sizeof(msg_buf) ? sizeof(msg_buf) : ret;
	etc_cape_encrypt(msg_buf, encr_buf, msg_len, 21);

	ret = lora_send(dev_lora, (uint8_t *)encr_buf, msg_len + 1);
	if (ret < 0) {
		LOG_ERR("lora_send failed error %d", ret);
	} else {
		LOG_DBG("Transmit data success, count %u", count);
	}

	count++;
	if (count >= 100) {
		count = 0;
	}
	return 0;
}

static int send_lora_message2(void)
{
	int ret;
	char msg_buf[sizeof("99984,##,AAAA,1683136417,0,0,")];
	char encr_buf[sizeof(msg_buf) + 1];
	uint8_t msg_len;
	static uint8_t count = 10;

	ret = lora_config(dev_lora, &etc_lora_tx_config);
	if (ret < 0) {
		LOG_ERR("lora_config failed error %d", ret);
		return -1;
	}

	ret = snprintf(msg_buf, sizeof(msg_buf),
		       "99984,%u,AAAA,1683136417,0,0,", count);			//placeholder device ID 99984
	msg_len = ret > sizeof(msg_buf) ? sizeof(msg_buf) : ret;
	etc_cape_encrypt(msg_buf, encr_buf, msg_len, 21);

	ret = lora_send(dev_lora, (uint8_t *)encr_buf, msg_len + 1);
	if (ret < 0) {
		LOG_ERR("lora_send failed error %d", ret);
	} else {
		LOG_DBG("Transmit data success, count %u", count);
	}

	count++;
	if (count >= 100) {
		count = 10;
	}
	return 0;
}

static int cmd_lora_tx(const struct shell *shell, size_t argc, char **argv) {
	uint32_t t0 = k_uptime_get_32();
	uint32_t freq = DEFAULT_LORA_FREQUENCY_HZ;
	uint8_t no_samples = 0;
	uint8_t count = 0;
	uint8_t dev_id[16];
	uint8_t tx_buf[128];
	int offset = 0;
	ssize_t hwinfo_length;

	if ((argc != 1) && (argc != 3)) {
		shell_print(shell, "Usage: %s <number of samples> <frequency in Hz>",
			    argv[0]);
		return -1;
	}

	if (argc == 3) {
		no_samples = atoi(argv[1]);
		freq = atoi(argv[2]);
		if ((freq < LORA_FREQ_US_MIN_HZ) || (freq > LORA_FREQ_US_MAX_HZ) ||
		    (no_samples == 0) || (no_samples > 100)) {
			shell_print(shell, "Number of samples must be between 1 and 100");
			shell_print(shell, "Lora freq must be between %lu and %lu",
				    LORA_FREQ_US_MIN_HZ, LORA_FREQ_US_MAX_HZ);
			return -1;
		}
	}

	etc_lora_tx_config.frequency = freq;
	int ret = lora_config(dev_lora, &etc_lora_tx_config);
	if (ret < 0) {
		shell_error(shell, "lora_config failed error %d", ret);
		return 0;
	}
	
	hwinfo_length = hwinfo_get_device_id(dev_id, sizeof(dev_id));
	for (int i = 0 ; i < hwinfo_length ; i++) {
		offset += snprintf(tx_buf + offset, sizeof(tx_buf) - offset,"%02X", dev_id[i]);
	}
	if (offset < 0) {
		shell_print(shell, "Error: Out of memory for buffer get device id");
		return -ENOMEM;
	}

	while (((argc == 3) && (count < no_samples)) ||
	       ((argc == 1) && (k_uptime_get_32() - t0 < 10000))) {
		ret = lora_send(dev_lora, tx_buf, offset);
		if (ret < 0) {
			shell_error(shell, "%u: error %d", count, ret);
			continue;
		} else {
			shell_print(shell, "%u: %s", count, tx_buf);
		}
		count++;
		k_sleep(K_MSEC(10));
	}
	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_lora_tx, NULL, "Transmit a message over Lora", cmd_lora_tx, 1, 2);

#define ACKUNCRYPT 49 

static int lora_rx(void)
{
	uint32_t t0 = k_uptime_get_32();
	int ret = lora_config(dev_lora, &etc_lora_rx_config);
	if (ret < 0) {
		LOG_ERR("lora_config failed error %d", ret);
		return 0;
	}
	int16_t rssi;
	int8_t snr;
	uint8_t rx_buf[128] = {0x00};
	LOG_INF("Start receiving LoRa messages");
	//while (k_uptime_get_32() - t0 < (1000UL * 60UL * 60UL)) {
	while (1) {
		ret = lora_recv(dev_lora, rx_buf, sizeof(rx_buf), K_SECONDS(1), &rssi, &snr);
		if (ret < 0) {
			continue;
		} else {
			char RXString[128] = {0};
  			etc_cape_decrypt(rx_buf, RXString, ret); //decrypt recevied data
			RXString[ret] = '\0';
			if (strstr(RXString, "AAAA")) {		//placeholder device ID 99984
				LOG_INF("Monitor: %s,%d,%d", RXString, rssi, snr);
				//shell_print(shell, "Send to another Monitor");
				ret = send_lora_message2();
				if (ret != 0) {
					continue;
				}
				ret = lora_config(dev_lora, &etc_lora_rx_config);
				if (ret < 0) {
					LOG_INF("lora_config failed error %d", ret);
				}
			}
		}
		k_sleep(K_SECONDS(1));
	}
	LOG_INF("lora_rx done.");
}

static int cmd_lora_rx(const struct shell *shell, size_t argc, char **argv) {
	int ret;

	if (argc == 1) {
		shell_print(shell, "Starting LoRa rx thread if not already running.\n"
			"Log messages will report on the status.");
		lora_action = LORA_ACTION_RX;
		etc_lora_rx_config.frequency = DEFAULT_LORA_FREQUENCY_HZ;
		k_sem_give(&lora_sem);
	}

	if (argc != 3) {
		shell_print(shell, "Usage: %s <number of samples> <frequency in Hz>",
			    argv[0]);
		return -1;
	}
	uint8_t no_samples = atoi(argv[1]);
	uint32_t freq_hz = atoi(argv[2]);

	if ((no_samples == 0) || (no_samples > 100) ||
	    (freq_hz < LORA_FREQ_US_MIN_HZ) || (freq_hz > LORA_FREQ_US_MAX_HZ)) {
		shell_print(shell, "Number of samples must be between 1 and 100");
		shell_print(shell, "Lora freq must be between %lu and %lu",
				LORA_FREQ_US_MIN_HZ, LORA_FREQ_US_MAX_HZ);
	}

	etc_lora_rx_config.frequency = freq_hz;
	ret = lora_config(dev_lora, &etc_lora_rx_config);
	if (ret < 0) {
		LOG_ERR("lora_config failed error %d", ret);
		return 0;
	}
	int16_t rssi;
	int8_t snr;
	uint8_t rx_buf[128] = {0x00};
	uint8_t count = 0;
	shell_print(shell, "Start receiving LoRa messages");

	while (count < no_samples) {
		ret = lora_recv(dev_lora, rx_buf, sizeof(rx_buf), K_SECONDS(1), &rssi, &snr);
		if (ret < 0) {
			shell_print(shell, "%u: timeout", count);
		} else {
			rx_buf[MIN(ret, sizeof(rx_buf) - 1)] = '\0';
			shell_print(shell, "%u: %d", count, rssi);
			shell_hexdump(shell, rx_buf, ret);
		}
		count++;
	}

	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_lora_rx, NULL, "Receive message over Lora", cmd_lora_rx, 1, 2);

static int cmd_lora_tx_rx(const struct shell *shell, size_t argc, char **argv) {
	uint32_t t0 = k_uptime_get_32();
	int ret;

	ret = k_mutex_lock(&lora_mutex, K_SECONDS(1));
	if (ret != 0) {
		shell_error(shell, "Can't lock lora mutex");
		return -1;
	}

	shell_print(shell, "Starting LoRa receive...");

	int16_t rssi;
	int8_t snr;
	uint8_t rx_buf[128] = {0x00};
	uint8_t counter = 0;
	while (k_uptime_get_32() - t0 < (1000UL * 60UL * 3UL)) {
		//shell_print(shell, "Send to Relay");
		counter++;
		ret = send_lora_message();
		if (ret != 0) {
			continue;
		}
		ret = lora_config(dev_lora, &etc_lora_rx_config);
		if (ret < 0) {
			shell_error(shell, "lora_config failed error %d", ret);
			goto exit;
		}
		ret = lora_recv(dev_lora, rx_buf, sizeof(rx_buf), K_SECONDS(1), &rssi, &snr);
		if (ret < 0) {
			//continue;
		} else {
			char RXString[128] = {0};
  			etc_cape_decrypt(rx_buf, RXString, ret); //decrypt recevied data
			RXString[ret] = '\0';
			if (strstr(RXString, "99984")) {									//placeholder device ID 99984
				shell_print(shell, "Rel,%d,%s,%d,%d", counter, RXString, rssi, snr);
			}
		}
		k_sleep(K_SECONDS(1));
		//shell_print(shell, "Send to another Monitor");
		ret = send_lora_message1();
		if (ret != 0) {
			continue;
		}
		ret = lora_config(dev_lora, &etc_lora_rx_config);
		if (ret < 0) {
			shell_error(shell, "lora_config failed error %d", ret);
			goto exit;
		}
		ret = lora_recv(dev_lora, rx_buf, sizeof(rx_buf), K_SECONDS(1), &rssi, &snr);
		if (ret < 0) {
			//continue;
		} else {
			char RXString[128] = {0};
  			etc_cape_decrypt(rx_buf, RXString, ret); //decrypt recevied data
			RXString[ret] = '\0';
			if (strstr(RXString, "99984")) {									//placeholder device ID 99984
				shell_print(shell, "Mon,%d,%s%d,%d", counter, RXString, rssi, snr);
			}
		}
		k_sleep(K_SECONDS(1));
		if (counter >= 60) break;
	}
	k_mutex_unlock(&lora_mutex);
	return 0;
exit:
	k_mutex_unlock(&lora_mutex);
	return ret;
}
SHELL_CMD_ARG_REGISTER(lora, NULL, "Receive message over Lora", cmd_lora_tx_rx, 1, 0);

static struct gpio_callback watchdog_cb_data;
void gpio_watchdog_interrupt_event(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	LOG_INF("Watchdog triggered interrupt pin");
}

static int cmd_stop_feed_wdt(const struct shell *shell, size_t argc, char **argv) 
{
	etc_watchdog_stop_work();
	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_stop_wdt, NULL, "Stop feeding hardware watchdog", cmd_stop_feed_wdt, 1, 0);

static int cmd_start_feed_wdt(const struct shell *shell, size_t argc, char **argv) 
{
	if (argc == 2) {
		uint16_t interval = atoi(argv[1]);
		etc_watchdog_set_timeout(interval);
		shell_print(shell, "WDT feed interval set to %u seconds", interval);
	}
	etc_watchdog_start_work();
	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_start_wdt, NULL, "Start feeding hardware watchdog", cmd_start_feed_wdt, 1, 1);

static int cmd_ble_active(const struct shell *shell, size_t argc, char **argv) 
{
#if IS_ENABLED(CONFIG_MCUMGR_TRANSPORT_BT)
	extern void start_smp_bluetooth(void);
	start_smp_bluetooth();
	shell_print(shell, "Enable the BLE MCUMGR");
	return 0;
#endif	
	shell_error(shell, "BLE is not supported");
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_ble_active, NULL, "Active the BLE MCUMGR", cmd_ble_active, 1, 0);

static int cmd_ble_deactive(const struct shell *shell, size_t argc, char **argv) 
{
#if IS_ENABLED(CONFIG_MCUMGR_TRANSPORT_BT)
	extern void stop_smp_bluetooth(void);
	stop_smp_bluetooth();
	shell_print(shell, "Deactive the BLE");
	return 0;
#endif	
	shell_error(shell, "BLE is not supported");
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_ble_deactive, NULL, "Deactive the BLE MCUMGR", cmd_ble_deactive, 1, 0);

static int cmd_last_reset_reason(const struct shell *shell, size_t argc, char **argv) 
{
	uint32_t reason;
	hwinfo_get_reset_cause(&reason);
	shell_print(shell, "Reboot reason 0x%08x", reason);
	if (reason & RESET_PIN) {
		shell_print(shell, " -> Reset from pin-reset detected");
	} 
	if (reason & RESET_SOFTWARE) {
		shell_print(shell, " -> Reset from soft-reset detected");
	} 
	if (reason & RESET_WATCHDOG) {
		shell_print(shell, " -> Reset from watch-dog detected");
	} 
	if (reason & RESET_CPU_LOCKUP) {
		shell_print(shell, " -> Reset from cpu lock-up detected");
	} 
	if (reason & RESET_LOW_POWER_WAKE) {
		shell_print(shell, " -> Reset due to wakeup from System Off mode when wakeup is triggered from DETECT signal from GPIO");
	} 
	if (reason & RESET_DEBUG) {
		shell_print(shell, " -> Reset due to wakeup from System Off mode when wakeup is triggered from entering info debug interface");
	}

	hwinfo_clear_reset_cause();
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_reset_reason, NULL, "Get the reset reason", cmd_last_reset_reason, 1, 0);

void etc_sleep(void)
{
	static const struct device *pm_devs[] = {
#ifdef CONFIG_BOARD_ETC_0_3_0
		DEVICE_DT_GET(DT_NODELABEL(spi3)),
#else
		DEVICE_DT_GET(DT_NODELABEL(spi1)),
#endif
		DEVICE_DT_GET(DT_NODELABEL(spi2)),
		DEVICE_DT_GET(DT_NODELABEL(pwm0)),
#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart0), okay)
		DEVICE_DT_GET(DT_NODELABEL(uart0)),
#endif
		DEVICE_DT_GET(DT_NODELABEL(uart1)),
		DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0))
	};
	const struct gpio_dt_spec rtc_int = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(rtc_int), control_gpios, 0);
	int ret;

	/* Set all GPIOs to consume the least amount of power. */
#if !defined(CONFIG_BOARD_ETC_0_3_0)
	gpio_pin_configure_dt(&sense_enable_dt, GPIO_OUTPUT_LOW);
#endif
	gpio_pin_configure_dt(&hall_dt, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(&vsens_enable_dt, GPIO_OUTPUT_HIGH);
	gpio_pin_configure_dt(&s0_dt, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&s1_dt, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&power_gpio_dt, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(&pon_trig_gpio_dt, GPIO_DISCONNECTED);
	gpio_pin_configure_dt(&rtc_int, GPIO_INPUT);
#if DT_NODE_EXISTS(DT_NODELABEL(modem_uart_oe))
	gpio_pin_configure_dt(&modem_uart_oe_dt, GPIO_ACTIVE_LOW | GPIO_OUTPUT_INACTIVE);
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
	gpio_pin_configure_dt(&onewire_slpz_dt, GPIO_OUTPUT_ACTIVE);
#endif

	gpio_pin_interrupt_configure_dt(&hall_dt, GPIO_INT_DISABLE);	

	for (int i = 0; i < ARRAY_SIZE(pm_devs); i++) {
		ret = pm_device_action_run(pm_devs[i], PM_DEVICE_ACTION_SUSPEND);
	}
}

static int cmd_etc_sleep(const struct shell *shell, size_t argc, char **argv)
{
	etc_sleep();
}

SHELL_CMD_ARG_REGISTER(etc_sleep, NULL, "Put the system into sleep mode", cmd_etc_sleep, 1, 1);

#define ETC_SETTINGS_DEVICE_ID_LEN (32)
static char tmp_device_id[ETC_SETTINGS_DEVICE_ID_LEN];

static int cmd_set_device_id(const struct shell *shell, size_t argc, char **argv)
{
	if ((argc == 2) && (strlen(argv[1]) != 0)) {
		size_t input_len = strlen(argv[1]);
		if (input_len > 6 || input_len < 1) {
			shell_error(shell, "Device ID Input exceeds maximum number of digits");
			return 0;
		}

		char *input = argv[1];
		for (int i = 0; i < input_len; i++) {
			if (!isdigit((unsigned char)input[i])) {
				shell_error(shell, "Invalid input, non-numeric characters detected");
				return 0;
			}
		}
		shell_print(shell, "OK");
		snprintf(tmp_device_id, sizeof(tmp_device_id), "%02d%06d", CONFIG_PRODUCTION_GROUP_VALUE, atoi(argv[1]));
		etc_device_write_setting(ETC_SETTING_DEVICE_ID, (char *)tmp_device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	} else {
		shell_error(shell, "Invalid device id");
	}

	return 0;
}

static int cmd_get_device_id(const struct shell *shell, size_t argc, char **argv)
{
	memset(tmp_device_id, 0, sizeof(tmp_device_id));
	etc_device_read_setting(ETC_SETTING_DEVICE_ID, (char *)tmp_device_id, ETC_SETTINGS_DEVICE_ID_LEN);
	shell_print(shell, "Device ID %s", tmp_device_id);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_settings, 
	SHELL_CMD(set_device_id, NULL, "Set device ID", cmd_set_device_id),
	SHELL_CMD(get_device_id, NULL, "Get device ID", cmd_get_device_id),
	SHELL_SUBCMD_SET_END);
/* Creating root (level 0) command "demo" */
SHELL_CMD_REGISTER(settings, &sub_settings, "ETC Settings", NULL);

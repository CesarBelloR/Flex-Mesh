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
#include <stdio.h>
#include "pcf85263a.h"
#include "adc.h"
#include "ui.h"
#include "sensor.h"
#include "ds2484.h"
#include "ds18b20.h"
#include "etc_cape.h"
#ifdef CONFIG_BQ25618
#include "bq25618.h"
#endif
#ifdef CONFIG_BQ24195
#include "bq24195.h"
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test, CONFIG_ETC_TEST_LOG_LEVEL);

#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)
 
/* Outputs */
static const struct gpio_dt_spec hall_dt =
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(hall_int), control_gpios, 0);
static const struct gpio_dt_spec sense_enable_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sense_enable), control_gpios, 0);
static const struct gpio_dt_spec s0_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
static const struct gpio_dt_spec s1_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel1), control_gpios, 0);
static const struct gpio_dt_spec vsens_enable_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(vsens_enable), control_gpios, 0);
static const struct gpio_dt_spec lte_on_off_gpio_dt =
		GPIO_DT_SPEC_GET(DT_NODELABEL(quectel_bg95), mdm_on_off_gpios);
static const struct gpio_dt_spec power_gpio_dt =
		GPIO_DT_SPEC_GET(DT_NODELABEL(quectel_bg95), mdm_power_gpios);
static const struct gpio_dt_spec pon_trig_gpio_dt =
		GPIO_DT_SPEC_GET(DT_NODELABEL(quectel_bg95), mdm_pon_trig_gpios);
#if DT_NODE_EXISTS(DT_NODELABEL(modem_uart_oe))
static const struct gpio_dt_spec modem_uart_oe_dt =
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(modem_uart_oe), control_gpios, 0);
#endif

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

static int cmd_lora_tx_rx(const struct shell *shell, size_t argc, char **argv);
static int lora_rx(void);

void lora_tx_rx_fn() {
	k_sem_init(&lora_sem, 0, 1);

	while (k_sem_take(&lora_sem, K_FOREVER) == 0) {
		if (lora_action == LORA_ACTION_HALL_TRIGGERED) {
			LOG_INF("Hall triggered");
			shell_execute_cmd(shell_backend_uart_get_ptr(), "lora");
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

#define HW_WDT_FEED_INTERVAL	K_SECONDS(10 * 60)

static void hw_wdt_feed(void)
{
	gpio_pin_configure_dt(&s0_dt, GPIO_OUTPUT);
	gpio_pin_set_dt(&s0_dt, 0U);
	k_busy_wait(10);
	gpio_pin_set_dt(&s0_dt, 1U);
	/* Minimum required pulse width according to datasheet is 100 ns. */
	k_busy_wait(1);
	gpio_pin_set_dt(&s0_dt, 0U);
}

void hw_wdt_work_handler(struct k_work *work) 
{
	struct k_work_delayable *work_delayable =
		CONTAINER_OF(work, struct k_work_delayable, work);
	hw_wdt_feed();
	k_work_schedule(work_delayable, HW_WDT_FEED_INTERVAL);
}

K_WORK_DELAYABLE_DEFINE(hw_wdt_work, hw_wdt_work_handler);

static void hw_wdt_start_feed(void)
{
	hw_wdt_feed();
	k_work_schedule(&hw_wdt_work, HW_WDT_FEED_INTERVAL);
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

	hw_wdt_start_feed();

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

static void adc_print_channel(const struct shell *shell, int channel)
{
	int adc_raw = sensor_get_raw_value(channel);
	float val;

	if ((channel == ETC_ADC_CHANNEL_AMB) || (channel == ETC_ADC_CHANNEL_SENSOR)) {
		val = sensor_ntc_converter(channel, adc_raw);
		shell_print(shell, "ADC Channel %d - Value %d - Temperature %.2f deg C", 
			    channel, adc_raw, val);
	} else {
		adc_get_raw_to_millivolts(channel, &adc_raw);
		val = (float)adc_raw / 1000.0f;
		shell_print(shell, "ADC Channel %d - Value %d - Voltage %.2f V",
			    channel, adc_raw, val);
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
		shell_print(shell, "\t SENS_ENABLE    -> ID: 0");
		shell_print(shell, "\t LTE_PWRKEY     -> ID: 1");
		shell_print(shell, "\t VSEN_EN        -> ID: 2");
		shell_print(shell, "\t LTE_PON_TRIG   -> ID: 3");
		shell_print(shell, "\t MODEM_UART_OE  -> ID: 4");
		shell_print(shell, "\t SENS_SEL0      -> ID: 5");
		shell_print(shell, "\t SENS_SEL1      -> ID: 6");
		shell_print(shell, "\t LTE_ON_OFF     -> ID: 7");
		shell_print(shell, "\t LTE_PON_TRIG   -> ID: 8");
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
		gpio_dt = &sense_enable_dt;
		name = "SENS_ENABLE";
		break;
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
		gpio_dt = &lte_on_off_gpio_dt;
		name = "LTE_ON_OFF";
		break;
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

static void adc_print_all_channels(const struct shell *shell) 
{
	for (int chan = 0; chan < ETC_ADC_CHANNEL_MAX; chan++) {
		if (chan == ETC_ADC_CHANNEL_SENSOR) {
			for (int input = 0; input < SENSOR_INPUT_MAX; input++) {
				sensor_adc_switch_channel(input);
				k_msleep(100);
				shell_print(shell, "Sensor input %u:", input);
				adc_print_channel(shell, chan);
			}
		} else {
			adc_print_channel(shell, chan);
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
		adc_print_all_channels(shell);
	} else {
		int channel = atoi(argv[1]);
		adc_print_channel(shell, channel);
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_adc, NULL, "Get ADC raw data", cmd_adc_request, 1, 1);

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

	gpio_pin_set_dt(&sense_enable_dt, 1U);
	k_sleep(K_SECONDS(1));

	ret = ds2484_init();
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
	const uint8_t MAX_ATTEMPTS = 10;
	uint8_t attempt = 0;
	bool success = false;
	uint16_t temperature;
	int8_t resolution, digit, minus = 0;
	float decimal;

	while (attempt < MAX_ATTEMPTS && !success) {
		if (ds2484_request_reset() != 0) {shell_print(shell, "Error issuing reset to 1-wire device");}
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
			attempt++;
			k_sleep(K_MSEC(10));
		} else {
			success = true;
		}
		//if (ds2484_request_reset() != 0) {shell_print(shell, "Error issuing reset to 1-wire device");}
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

	shell_print(shell, "Temperature: %02f°C --> %u attempts", decimal, attempt + 1);

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
	SHELL_CMD(get_temp, NULL, "Request temperature from a one device bus", cmd_ds2484_convert_temp),
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
	int pattern = atoi(argv[1]);
	shell_print(shell, "Set UI color %d", pattern);
	if (pattern == 4) {
		ui_led_set_color(255, 0, 0);
	} else if (pattern == 5) {
		ui_led_set_color(0, 255, 0);
	} else if (pattern == 6) {
		ui_led_set_color(0, 0, 255);
	} else {
		ui_led_set_pattern((enum ui_led_pattern)pattern);
	}
	
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_ui, NULL, "Set color UI", cmd_ui_request, 2, 0);

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

SHELL_CMD_ARG_REGISTER(etc_flash_info, NULL, "Get information of external flash", cmd_external_flash_get_info, 1, 0);

const struct device *lora_dev = NULL;
static struct lora_modem_config etc_lora_rx_config = {
	.frequency = 915000000,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_7,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
	.tx = false,
};

static struct lora_modem_config etc_lora_tx_config  = {
	.frequency = 915000000,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_7,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
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
	uint8_t tx_buf[] = "Hello World";
	int ret = lora_config(dev_lora, &etc_lora_tx_config);
	if (ret < 0) {
		shell_error(shell, "lora_config failed error %d", ret);
		return 0;
	}
	while (k_uptime_get_32() - t0 < 10000) {
		ret = lora_send(dev_lora, tx_buf, strlen(tx_buf));
		if (ret < 0) {
			shell_error(shell, "lora_send failed error %d", ret);
			break;
		} else {
			shell_print(shell, "Transmit data success %s", tx_buf);
		}
		k_sleep(K_SECONDS(1));
	}
	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_lora_tx, NULL, "Transmit a message over Lora", cmd_lora_tx, 1, 0);

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
	shell_print(shell, "Starting LoRa rx thread if not already running.\n"
		    "Log messages will report on the status.");
	lora_action = LORA_ACTION_RX;
	k_sem_give(&lora_sem);
	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_lora_rx, NULL, "Receive message over Lora", cmd_lora_rx, 1, 0);

static int cmd_lora_tx_rx(const struct shell *shell, size_t argc, char **argv) {
	uint32_t t0 = k_uptime_get_32();
	int ret;

	ret = k_mutex_lock(&lora_mutex, K_SECONDS(1));
	if (ret != 0) {
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
	k_work_cancel_delayable(&hw_wdt_work);
	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_stop_wdt, NULL, "Stop feeding hardware watchdog", cmd_stop_feed_wdt, 1, 0);

static int cmd_start_feed_wdt(const struct shell *shell, size_t argc, char **argv) 
{
	hw_wdt_start_feed();
	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_start_wdt, NULL, "Start feeding hardware watchdog", cmd_start_feed_wdt, 1, 0);

static int cmd_ble_active(const struct shell *shell, size_t argc, char **argv) 
{
#ifdef CONFIG_MCUMGR_SMP_BT
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
#ifdef CONFIG_MCUMGR_SMP_BT
	extern void stop_smp_bluetooth(void);
	stop_smp_bluetooth();
	shell_print(shell, "Deactive the BLE");
	return 0;

#endif	
	shell_error(shell, "BLE is not supported");
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_ble_deactive, NULL, "Deactive the BLE MCUMGR", cmd_ble_deactive, 1, 0);


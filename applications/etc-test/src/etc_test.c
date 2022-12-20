#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
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
#include "pcf85263a.h"
#include "adc.h"
#include "ui.h"
#include "ds18b20.h"
#include "sensor.h"
#include "ds2484.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test, CONFIG_ETC_TEST_LOG_LEVEL);

#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)

const struct device* dev_gpio = NULL;
const struct device* dev_lora = DEVICE_DT_GET(DEFAULT_RADIO_NODE);

void etc_test_init(void) {
	dev_gpio = device_get_binding("GPIO_0");
	if (dev_gpio == NULL) {
		return;
	}

	if (!device_is_ready(dev_lora)) {
		return;
	}

	sensor_init();

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
	uint16_t adc_raw = sensor_get_raw_value(channel);
	float val;
	if ((channel == ETC_ADC_CHANNEL_AMB) || (channel == ETC_ADC_CHANNEL_SENSOR)) {
		val = sensor_ntc_converter(channel, adc_raw);
		shell_print(shell, "ADC Channel %d - Value %d - Temperature %.2f deg C", 
			    channel, adc_raw, val);
	} else {
		int val_mv;
		adc_get_raw_to_millivolts(channel, &val_mv);
		val = (float)val_mv / 1000.0f;
		shell_print(shell, "ADC Channel %d - Value %d - Voltage %.2f V",
			    channel, adc_raw, val);
	}
}

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

static int cmd_ds18b20_request(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ds18b20_init();
	k_sleep(K_SECONDS(1));
	if (ds18b20_manualconvert()) {
		shell_print(shell, "DS18B20 return %d", (int)ds18b20[0].temperature);
	} else {
		shell_print(shell, "DS18B20 error");
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_ds18b20, NULL, "Get DS18B20 temperature sensor", cmd_ds18b20_request, 1, 0);

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
		shell_print(shell, "Read: %X", byte);
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
	char rom;

	ret = ds2484_request_search(&rom);

	if (ret == 0) {
		shell_print(shell, "Found %X", rom);
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
		shell_print(shell, "Status %X", status);
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
		"Usage: config <bit> <1/0>\n"
		"Bit values from 0 to 3 are valid.";
	int ret = -EINVAL;
	int bit, enable;

	if (argc != 3) {
		goto error;
	}

	bit = atoi(argv[1]);
	enable = atoi(argv[2]);

	if ((bit > 3) || (bit < 0)) {
		goto error;
	}

	if (enable) {
		ds2484_set_config((ds248x_config_t)bit);
	} else {
		ds2484_clear_config((ds248x_config_t)bit);
	}
error:
	shell_print(shell, "%s", usage);
	return ret;
}

static int cmd_ds2484_enable(const struct shell *shell, size_t argc, char **argv)
{
	int ret;

	gpio_pin_set(dev_gpio, GPIO_SENSE_ENABLE_PIN, 1U);
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

SHELL_STATIC_SUBCMD_SET_CREATE(ds2484_sub,
	SHELL_CMD(enable, NULL, "Enable DS2484", cmd_ds2484_enable),
	SHELL_CMD(status, NULL, "Get status register", cmd_ds2484_status),
	SHELL_CMD(search, NULL, "Start/continue search for devices", cmd_ds2484_search),
	SHELL_CMD_ARG(select, NULL, "Set the 1-wire address", cmd_ds2484_req_select, 1, 1),
	SHELL_CMD_ARG(write, NULL, "Write byte to 1-wire device", cmd_ds2484_write, 1, 1),
	SHELL_CMD(read, NULL, "Read byte from 1-wire device", cmd_ds2484_read),
	SHELL_CMD_ARG(config, NULL, "Set the config register", cmd_ds2484_config, 1, 2),
	SHELL_CMD(reset, NULL, "Reset DS2484", cmd_ds2484_reset),
	SHELL_CMD(req_reset, NULL, "Request a 1-wire reset", cmd_ds2484_req_reset),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(ds2484, &ds2484_sub, "DS2484 1-wire commands", NULL);

static int cmd_ui_request(const struct shell *shell, size_t argc, char **argv)
{
	int pattern = atoi(argv[1]);
	shell_print(shell, "Set UI color %d", pattern);
	ui_led_set_pattern((enum ui_led_pattern)pattern);
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

SHELL_CMD_ARG_REGISTER(etc_set_time, NULL, "Set time in PCF85263", cmd_pcf85263_set_time, 2, 0);

static int cmd_pcf85263_get_time(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	time_t utc_time = 0;
	pcf85263a_rtc_get_time(&utc_time);
	shell_print(shell, "Get time UTC %d", (int)utc_time);
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_get_time, NULL, "Get time in PCF85263", cmd_pcf85263_get_time, 1, 0);

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

#define GPIO_USER_BUTTON_PIN 5
static int cmd_button_pull_module(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	const struct device *dev = device_get_binding("GPIO_1");
	if (dev == NULL) {
		shell_print(shell, "Can't get GPIO_1 for button");
		return 0;
	} else {
		gpio_pin_configure(dev, GPIO_USER_BUTTON_PIN, GPIO_INPUT | GPIO_PULL_UP);
		k_usleep(50);
		uint32_t t0 = k_uptime_get_32();
		while(k_uptime_get_32() - t0 < 10000) {
			int btn_status = gpio_pin_get(dev, GPIO_USER_BUTTON_PIN);
			if (btn_status == 0) {
				shell_print(shell, "Button User pressed");
				break;
			}
		}
	}
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_button_user, NULL, "Check Button User", cmd_button_pull_module, 1, 0);

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

static int cmd_lora_rx(const struct shell *shell, size_t argc, char **argv) {
	uint32_t t0 = k_uptime_get_32();
	int ret = lora_config(dev_lora, &etc_lora_rx_config);
	if (ret < 0) {
		shell_error(shell, "lora_config failed error %d", ret);
		return 0;
	}
	int16_t rssi;
	int8_t snr;
	uint8_t rx_buf[128] = {0x00};
	while (k_uptime_get_32() - t0 < (1000UL * 60UL * 2UL)) {
		ret = lora_recv(dev_lora, rx_buf, sizeof(rx_buf), K_SECONDS(1), &rssi, &snr);
		if (ret < 0) {
			continue;
		} else {
			char RXString[128] = {0};
  			etc_cape_decrypt(rx_buf, RXString, ret); //decrypt recevied data
			shell_print(shell, "Received data: RSSI:%ddBm, SNR:%ddBm", rssi, snr);
			//shell_hexdump(shell, RXString, ret);
			RXString[ret] = '\0';
			shell_print(shell, "%s", RXString);
		}
		//k_sleep(K_MSEC(500));
	}
	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_lora_rx, NULL, "Receive message over Lora", cmd_lora_rx, 1, 0);

#define GPIO_RTC_INT_PIN 3
static struct gpio_callback watchdog_cb_data;
void gpio_watchdog_interrupt_event(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	LOG_INF("Watchdog triggered interrupt pin");
}

static int cmd_hw_wdt(const struct shell *shell, size_t argc, char **argv) {
	pcf85263a_interrupt_flag_t flag_a = {0};
	pcf85263a_interrupt_flag_t flag_b = {0};
	flag_b.enable_wdg = 1;
	flag_a.enable_wdg = 1;
	const struct device *dev = device_get_binding("GPIO_0");
	if (dev == NULL) {
		shell_print(shell, "Can't get GPIO_0 for button");
		return 0;
	} else {
		gpio_pin_configure(dev, GPIO_RTC_INT_PIN, GPIO_INPUT | GPIO_PULL_UP);
		gpio_pin_interrupt_configure(dev, GPIO_RTC_INT_PIN, GPIO_INT_EDGE_FALLING);
		gpio_init_callback(&watchdog_cb_data, gpio_watchdog_interrupt_event, BIT(GPIO_RTC_INT_PIN));
		gpio_add_callback(dev, &watchdog_cb_data);
	}
	/* Interrupt channel A - INTA*/
	pcf85263a_set_interrupt_a_io(true);
	/* Interrupt channel B - TS */
	pcf85263a_set_interrupt_b_io(true);
	pcf85263a_interrupt_a_enable(flag_a);
	pcf85263a_interrupt_b_enable(flag_b);
	pcf85263a_watchdog_init();
	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_hw_wdt, NULL, "Enable hardware watchdog from PCF85", cmd_hw_wdt, 1, 0);

static int cmd_stop_wdt(const struct shell *shell, size_t argc, char **argv) {
	pcf85263a_watchdog_stop_feed();
	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_stop_wdt, NULL, "Stop feeding hardware watchdog", cmd_stop_wdt, 1, 0);
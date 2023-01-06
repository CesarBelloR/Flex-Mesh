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
#include "pcf85263a.h"
#include "adc.h"
#include "ui.h"
#include "ds18b20.h"
#include "ds2484.h"
#include "etc_cape.h"
#include <stdio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test, CONFIG_ETC_TEST_LOG_LEVEL);

#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)
#define GPIO_HALL_PIN (28)
#define GPIO_SENSE_ENABLE_PIN (13)
#define GPIO_S0_PIN (9)
#define GPIO_S1_PIN (10)
const struct device* dev_gpio = NULL;
const struct device* dev_lora = DEVICE_DT_GET(DEFAULT_RADIO_NODE);
static struct k_mutex lora_mutex;
static struct gpio_callback hall_cb;
static struct k_sem lora_sem;

static int cmd_lora_tx_rx(const struct shell *shell, size_t argc, char **argv) ;

static void adc_switch_channel(uint8_t channel) {
	gpio_pin_set(dev_gpio, GPIO_SENSE_ENABLE_PIN, 0U);
	gpio_pin_set(dev_gpio, GPIO_S0_PIN, 0U);
	gpio_pin_set(dev_gpio, GPIO_S1_PIN, 0U);
}

void lora_tx_rx_fn() {
	k_sem_init(&lora_sem, 0, 1);

	while (k_sem_take(&lora_sem, K_FOREVER) == 0) {
		shell_execute_cmd(shell_backend_uart_get_ptr(), "etc_lora_tx_rx");
		//cmd_lora_tx_rx(shell_backend_uart_get_ptr(), 0, NULL);
	}
}

K_THREAD_DEFINE(lora_tx_rx, 2048, lora_tx_rx_fn, NULL, NULL, NULL, 5, 0, 0);

static void hall_cb_fn(const struct device *dev,
		struct gpio_callback *cb, uint32_t pins)
{
	k_sem_give(&lora_sem);
}

void etc_test_init(void) 
{
	int ret;
	dev_gpio = device_get_binding("GPIO_0");
	if (dev_gpio == NULL) {
		return;
	}

	if (!device_is_ready(dev_lora)) {
		return;
	}

	k_mutex_init(&lora_mutex);
	// Configure hall interrupt
	gpio_pin_configure(dev_gpio, GPIO_HALL_PIN, GPIO_INPUT | GPIO_ACTIVE_LOW);
	gpio_init_callback(&hall_cb, hall_cb_fn, BIT(GPIO_HALL_PIN));
	ret = gpio_add_callback(dev_gpio, &hall_cb);
	if (ret < 0) {
		LOG_ERR("Failed to set gpio callback!");
	}
	gpio_pin_interrupt_configure(dev_gpio, GPIO_HALL_PIN, GPIO_INT_EDGE_TO_ACTIVE);

	gpio_pin_configure(dev_gpio, GPIO_SENSE_ENABLE_PIN, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(dev_gpio, GPIO_S0_PIN, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure(dev_gpio, GPIO_S1_PIN, GPIO_OUTPUT_INACTIVE);

	adc_switch_channel(0);
}

static int cmd_version(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "ETC test version %s", CONFIG_TEST_VERSION);

	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_version, NULL, "Show kernel version", cmd_version, 1, 0);

const float nodepoints[34] = {
  195.652,
  148.171,
  113.347,
  87.559,
  68.237,
  53.650,
  42.506,
  33.892,
  27.219,
  22.021,
  17.926,
  14.674,
  12.081,
  10.000,
  8.315,
  6.948,
  5.834,
  4.917,
  4.161,
  3.535,
  3.014,
  2.586,
  2.228,
  1.925,
  1.669,
  1.452,
  1.268,
  1.110,
  0.974,
  0.858,
  0.758,
  0.672,
  0.596,
  0.531,
};

#define SERIESRESISTOR 10000 //on board series resistor - 10kohm

float reMap(const float pts[34], float input) { //maps resistance to temperature lookup table. 
  float mm = 0;
  for (unsigned char nn = 0; nn < 33; nn++) {
    if (input <= pts[nn] && input >= pts[nn + 1]) {
      mm = ( (-40 + (nn * 5)) - (-40 + ((nn + 1) * 5)) ) / ( pts[nn] - pts[nn + 1] );
      mm = mm * (input - pts[nn]);
      mm = mm +  (-40 + (nn * 5));
    }
  }
  return (mm);
}

static int cmd_adc_request(const struct shell *shell, size_t argc, char **argv)
{
	int channel = atoi(argv[1]);
	gpio_pin_set(dev_gpio, GPIO_SENSE_ENABLE_PIN, 0U);
	k_sleep(K_SECONDS(1));
	uint16_t adc_raw = adc_get_channel(channel);
	shell_print(shell, "ADC Channel %d - Value %d", channel, adc_raw);
	return 0;
}

SHELL_CMD_ARG_REGISTER(etc_adc, NULL, "Get ADC raw data", cmd_adc_request, 2, 0);

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

static int cmd_ds2484_convert_temp(const struct shell *shell, size_t argc, char **argv)
{
	char data[9];
	uint16_t temperature;
	int8_t resolution, digit, minus = 0;
	float decimal;

	if (ds2484_request_reset() != 0) {shell_print(shell, "Error issuing reset to 1-wire device");}
	if (ds2484_request_skip() != 0) {shell_print(shell, "Error sending ROM skip command");}		//Send command to all devices
	if (ds2484_write_byte(0x44) != 0) {shell_print(shell, "Error requesting temperature measurement");}
	if (ds2484_set_config((ds248x_config_t)(1 << 2)) != 0) {shell_print(shell, "Error setting strong pullup");}
	k_sleep(K_MSEC(800));
	if (ds2484_request_reset() != 0) {shell_print(shell, "Error issuing reset to 1-wire device");}
	if (ds2484_request_skip() != 0) {shell_print(shell, "Error sending ROM skip command");}		//Send command to all devices
	if (ds2484_write_byte(0xBE) != 0) {shell_print(shell, "Error requesting data from scratch pad");}		//read scratch pad *this will not work if there is more than one device on the bus
	if (ds2484_read_bytes(data, 9) != 0) {shell_print(shell, "Error reading data from scratch pad");}
	if (ds2484_request_reset() != 0) {shell_print(shell, "Error issuing reset to 1-wire device");}

	/* First two bytes of scratchpad are temperature values */
	temperature = data[0] | data[1] << 8;

	/* Check if temperature is negative */
	if (temperature & 0x8000)
	{
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
	switch (resolution)
	{
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
	if (minus)
		decimal = 0 - decimal;

	shell_fprintf(shell, SHELL_NORMAL, "Temperature: %02f", decimal);
	//shell_fprintf(shell, SHELL_NORMAL, "%02f", decimal);
	shell_print(shell, "°C");

	return 0;
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

static int send_lora_message(void)
{
	int ret;
	char msg_buf[sizeof("S,#####,MT1,21831,3.70,###,*,*,19.8,*,*,21.8,*,")];
	char encr_buf[sizeof(msg_buf) + 1];
	uint8_t msg_len;
	static uint8_t count = 0;

	ret = lora_config(dev_lora, &etc_lora_tx_config);
	if (ret < 0) {
		LOG_ERR("lora_config failed error %d", ret);
		return -1;
	}

	ret = snprintf(msg_buf, sizeof(msg_buf),
		       "S,9970,MT1,21831,3.70,%u,*,*,19.8,*,*,21.8,*,", count);
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
			RXString[ret] = '\0';
			shell_print(shell, "%s", RXString);
		}
		k_sleep(K_SECONDS(2));
	}
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

	int16_t rssi;
	int8_t snr;
	uint8_t rx_buf[128] = {0x00};

	while (k_uptime_get_32() - t0 < (1000UL * 60UL * 2UL)) {
		ret = send_lora_message();
		if (ret != 0) {
			continue;
		}
		ret = lora_config(dev_lora, &etc_lora_rx_config);
		if (ret < 0) {
			shell_error(shell, "lora_config failed error %d", ret);
			goto exit;
		}
		ret = lora_recv(dev_lora, rx_buf, sizeof(rx_buf), K_SECONDS(5), &rssi, &snr);
		if (ret < 0) {
			continue;
		} else {
			char RXString[128] = {0};
  			etc_cape_decrypt(rx_buf, RXString, ret); //decrypt recevied data
			RXString[ret] = '\0';
			if (strstr(RXString, "21831")) {
				shell_print(shell, "%s,%d,%d", RXString, rssi, snr);
			}
		}
		k_sleep(K_SECONDS(2));
	}
	k_mutex_unlock(&lora_mutex);
	return 0;
exit:
	k_mutex_unlock(&lora_mutex);
	return ret;
}
SHELL_CMD_ARG_REGISTER(etc_lora_tx_rx, NULL, "Receive message over Lora", cmd_lora_tx_rx, 1, 0);

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
#include <zephyr.h>
#include <shell/shell.h>
#include <version.h>
#include <logging/log.h>
#include <stdlib.h>
#include <drivers/uart.h>
#include <drivers/lora.h>
#include <usb/usb_device.h>
#include <ctype.h>
#include <device.h>
#include <drivers/flash.h>
#include <drivers/gpio.h>
#include "pcf85263a.h"
#include "adc.h"
#include "ui.h"
#include "ds18b20.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(test, CONFIG_ETC_TEST_LOG_LEVEL);

#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)
#define GPIO_SENSE_ENABLE_PIN (13)
#define GPIO_S0_PIN (9)
#define GPIO_S1_PIN (10)
const struct device* dev_gpio = NULL;
const struct device* dev_lora = DEVICE_DT_GET(DEFAULT_RADIO_NODE);

static void adc_switch_channel(uint8_t channel) {
	gpio_pin_set(dev_gpio, GPIO_SENSE_ENABLE_PIN, 0U);
	gpio_pin_set(dev_gpio, GPIO_S0_PIN, 0U);
	gpio_pin_set(dev_gpio, GPIO_S1_PIN, 0U);
}

void etc_test_init(void) {
	dev_gpio = device_get_binding("GPIO_0");
	if (dev_gpio == NULL) {
		return;
	}

	if (!device_is_ready(dev_lora)) {
		return;
	}

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
	shell_print(shell, "Get time UTC %d", utc_time);
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
	while (k_uptime_get_32() - t0 < 10000) {
		ret = lora_recv(dev_lora, rx_buf, sizeof(rx_buf), K_SECONDS(1), &rssi, &snr);
		if (ret < 0) {
			shell_error(shell, "No data received");
			continue;
		} else {
			char RXString[ACKUNCRYPT] = {0};
  			etc_cape_decrypt(rx_buf, RXString, ret); //decrypt recevied data
			LOG_HEXDUMP_INF(RXString, ACKUNCRYPT, "RECV");
			shell_print(shell, "Received data: %s (RSSI:%ddBm, SNR:%ddBm)", rx_buf, rssi, snr);
		}
		k_sleep(K_MSEC(500));
	}
	return 0;
}
SHELL_CMD_ARG_REGISTER(etc_lora_rx, NULL, "Receive message over Lora", cmd_lora_rx, 1, 0);
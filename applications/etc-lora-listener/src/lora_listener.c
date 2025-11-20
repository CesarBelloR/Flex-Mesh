#include <zephyr/kernel.h>
#include <zephyr/console/console.h>
#include <zephyr/drivers/lora.h>
#include "data/etc_cape.h"
#include "lora_listener.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lora, CONFIG_ETC_LORA_LISTENER_LOG_LEVEL);

#define LORA_ACKUNCRYPT_LEN	128
#define LORA_ACKCRYPT_LEN	128

#define CONFIG_ETC_LORA_MODULE_RX_FREQUENCY 915000000

static char decoded_buf[LORA_ACKUNCRYPT_LEN] = {0x00};
static uint8_t lora_rx_buf[LORA_ACKUNCRYPT_LEN] = {0x00};

const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));

static struct lora_modem_config etc_lora_rx_config = {
	.frequency = CONFIG_ETC_LORA_MODULE_RX_FREQUENCY,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_7,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
	.tx = false,
};

const char new_line[] = "\r\n";

static bool contains_unprintable_characters(char *data, uint16_t length)
{
	for (uint16_t i = 0; i < length; i++) {
		if (data[i] < 32 || data[i] > 126) {
			return true;
		}
	}
	return false;
}

static void lora_receive_cb(const struct device *dev, uint8_t *data, uint16_t size, int16_t rssi,
			    int8_t snr)
{
	int decoded_len = 0;
	memset(decoded_buf, 0, sizeof(decoded_buf));
	etc_cape_decrypt((char *)data, decoded_buf, size);
	decoded_len = size - 1;
	if (contains_unprintable_characters(decoded_buf, decoded_len)) {
		LOG_HEXDUMP_DBG(data, size, "Raw data");
	} else {
		console_write(NULL, decoded_buf, decoded_len);
		console_write(NULL, new_line, sizeof(new_line) - 1);
	}
}

void lora_receive(void)
{
	int ret = 0;

	memset(lora_rx_buf, 0, sizeof(lora_rx_buf));
	memset(decoded_buf, 0, sizeof(decoded_buf));
	ret = lora_config(lora_dev, &etc_lora_rx_config);
	if (ret < 0) {
		LOG_ERR("Lora_config failed error %d", ret);
		return;
	}

	lora_recv_async(lora_dev, lora_receive_cb);
}
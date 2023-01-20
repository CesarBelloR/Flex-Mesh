/***************************************************************************/
/*!
\file       etc_lora.c
\brief      Lora management application

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#include <device.h>
#include <drivers/lora.h>
#include <errno.h>
#include <sys/util.h>
#include <zephyr.h>
#include <kernel.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(ETC_LORA, CONFIG_ETC_APP_LOG_LEVEL);

#include "etc_lora.h"
#include "etc_setting.h"

#define CONFIG_LORA_RX_THREAD_STACK_SIZE 1024
#define CONFIG_LORA_RX_SIZE 255

static K_KERNEL_STACK_DEFINE(lora_rx_stack, CONFIG_LORA_RX_THREAD_STACK_SIZE);
#define DEFAULT_RADIO_NODE DT_ALIAS(lora0)
BUILD_ASSERT(DT_NODE_HAS_STATUS(DEFAULT_RADIO_NODE, okay), "No default LoRa radio specified in DT");

static struct k_thread lora_rx_thread;
static etc_lora_rx_callback lora_rx_callback;
const struct device *lora_dev = NULL;
static struct lora_modem_config etc_lora_rx_config = {
	.frequency = 915000000,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_10,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
	.tx = false,
};

static struct lora_modem_config etc_lora_tx_config  = {
	.frequency = 915000000,
	.bandwidth = BW_125_KHZ,
	.datarate = SF_10,
	.preamble_len = 8,
	.coding_rate = CR_4_5,
	.tx_power = 14,
	.tx = true,
};

typedef struct {
	struct lora_modem_config* rx_config;
	struct lora_modem_config* tx_config;
	struct k_mutex lock;
	bool is_tx;
} etc_lora_config_t;

static etc_lora_config_t etc_lora_config; 
static etc_lora_config_t* p_config = &etc_lora_config;
static uint8_t etc_lora_buff[CONFIG_LORA_RX_SIZE] = {0x00};

void etc_lora_rx_thread_handler(void *p1, void *p2, void *p3) {
	int len = 0;
	int16_t rssi;
	int8_t snr;
	while(1) {
		/* Block until data arrives */
		k_mutex_lock(&p_config->lock, K_FOREVER);
		bool is_tx = p_config->is_tx;
		k_mutex_unlock(&p_config->lock);
		if (is_tx) {
			k_sleep(K_MSEC(p_etc_config->tx_delay_msec));
			continue;
		}

		memset(etc_lora_buff, 0, sizeof(etc_lora_buff));
		len = lora_recv(lora_dev, etc_lora_buff, CONFIG_LORA_RX_SIZE, K_SECONDS(p_etc_config->rx_duration_secs),
				&rssi, &snr);
		if (len < 0) {
			LOG_WRN("No data received");
			continue;
		}
		LOG_HEXDUMP_DBG(etc_lora_buff, len, "RECV");
	}
}

int etc_lora_init(void) {
	if (!device_is_ready(lora_dev)) {
		LOG_ERR("%s Device not ready", lora_dev->name);
		return -EINVAL;
	}

	k_mutex_init(&etc_lora_config.lock);
	etc_lora_config.is_tx = false;
	etc_lora_config.rx_config = &etc_lora_rx_config;
	etc_lora_config.tx_config = &etc_lora_tx_config;

	k_thread_create(&lora_rx_thread, lora_rx_stack,
			K_KERNEL_STACK_SIZEOF(lora_rx_stack),
			(k_thread_entry_t) etc_lora_rx_thread_handler,
			NULL, NULL, NULL, K_LOWEST_THREAD_PRIO, 0, K_NO_WAIT);
    return 0;
}

int etc_lora_send(void* data, int length) {
	if (length > CONFIG_LORA_RX_SIZE) return -EINVAL;
	int ret = 0;
	k_mutex_lock(&p_config->lock, K_FOREVER);
	etc_lora_config.is_tx = true;

	ret = lora_config(lora_dev, p_config->tx_config);
	if (ret < 0) {
		LOG_ERR("lora_config failed error %d", ret);
		return ret;
	}

	ret = lora_send(lora_dev, data, length);
	if (ret < 0) {
		LOG_ERR("lora_send failed error %d", ret);
		return ret;
	}

	k_sleep(K_MSEC(p_etc_config->tx_delay_msec));

	etc_lora_config.is_tx = false;
	ret = lora_config(lora_dev, p_config->rx_config);
	if (ret < 0) {
		LOG_ERR("LoRa config failed error %d", ret);
		return ret;
	}

	k_mutex_unlock(&p_config->lock);
	return 0;
}

int etc_lora_receive(etc_lora_rx_callback callback) {
	int ret = 0;
	k_mutex_lock(&p_config->lock, K_FOREVER);

	lora_rx_callback = callback;

	ret = lora_config(lora_dev, p_config->rx_config);
	if (ret < 0) {
		LOG_ERR("LoRa config failed with error %d", ret);
		return ret;
	}

	k_mutex_unlock(&p_config->lock);
	return 0;
}
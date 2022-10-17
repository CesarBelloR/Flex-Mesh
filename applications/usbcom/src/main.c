#include <stdio.h>
#include <string.h>
#include <device.h>
#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <zephyr.h>
#include <sys/ring_buffer.h>

#include <usb/usb_device.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(cdc_acm_composite, LOG_LEVEL_INF);

#define RING_BUF_SIZE	(64 * 2)

uint8_t buffer0[RING_BUF_SIZE];
uint8_t buffer1[RING_BUF_SIZE];

struct serial_peer {
	const struct device *dev;
	struct serial_peer *data;
	struct ring_buf rb;
};

static struct serial_peer peers[2];

static void interrupt_handler(const struct device *dev, void *user_data)
{
	struct serial_peer *peer = user_data;

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		LOG_DBG("dev %p peer %p", dev, peer);

		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[RING_BUF_SIZE];
			size_t read, wrote;
			struct ring_buf *rb = &peer->data->rb;

			read = uart_fifo_read(dev, buf, sizeof(buf));
			if (read) {
				wrote = ring_buf_put(rb, buf, read);
				if (wrote < read) {
					LOG_ERR("Drop %zu bytes", read - wrote);
				}

				uart_irq_tx_enable(peer->dev);

				LOG_DBG("dev %p -> dev %p send %zu bytes",
					dev, peer->dev, wrote);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buf[64];
			size_t wrote, len;

			len = ring_buf_get(&peer->rb, buf, sizeof(buf));
			if (!len) {
				LOG_DBG("dev %p TX buffer empty", dev);
				uart_irq_tx_disable(dev);
			} else {
				wrote = uart_fifo_fill(dev, buf, len);
				LOG_DBG("dev %p wrote len %zu", dev, wrote);
			}
		}
	}
}

static void uart_line_set(const struct device *dev)
{
	uint32_t baudrate;
	int ret;

	/* They are optional, we use them to test the interrupt endpoint */
	ret = uart_line_ctrl_set(dev, UART_LINE_CTRL_DCD, 1);
	if (ret) {
		LOG_DBG("Failed to set DCD, ret code %d", ret);
	}

	ret = uart_line_ctrl_set(dev, UART_LINE_CTRL_DSR, 1);
	if (ret) {
		LOG_DBG("Failed to set DSR, ret code %d", ret);
	}

	/* Wait 1 sec for the host to do all settings */
	k_busy_wait(1000000);

	ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
	if (ret) {
		LOG_DBG("Failed to get baudrate, ret code %d", ret);
	} else {
		LOG_DBG("Baudrate detected: %d", baudrate);
	}
}

#define LTE_POWER_ON_OFF_PIN 4
#define LTE_POWER_KEY_PIN 1

static void main_modem_power_init(void) {
	const struct device* gpio_1 = device_get_binding("GPIO_1");

	if (!device_is_ready(gpio_1)) {
		LOG_ERR("GPIO 1 is not ready");
		return;
	}
	
	gpio_pin_configure(gpio_1, LTE_POWER_KEY_PIN, GPIO_OUTPUT);
	gpio_pin_set(gpio_1, LTE_POWER_KEY_PIN, 0U);
	k_sleep(K_MSEC(500));
	gpio_pin_set(gpio_1, LTE_POWER_KEY_PIN, 1U);
	k_sleep(K_MSEC(1000));
	gpio_pin_set(gpio_1, LTE_POWER_KEY_PIN, 0U);
	k_sleep(K_MSEC(2500));
	LOG_INF("IO Done");
}

void main(void)
{
	uint32_t dtr = 0U;
	int ret;

	peers[0].dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
	if (!device_is_ready(peers[0].dev)) {
		LOG_ERR("CDC ACM device not ready");
		return;
	}

	peers[1].dev = device_get_binding("UART_1");
	if (!device_is_ready(peers[1].dev)) {
		LOG_ERR("Can't find the UART modem interface");
		return;
	}

	ret = usb_enable(NULL);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB");
		return;
	}

	LOG_INF("Wait for DTR");

	while (1) {
		uart_line_ctrl_get(peers[0].dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			break;
		}

		k_sleep(K_MSEC(100));
	}

	LOG_INF("DTR set, start test");
	uart_line_set(peers[0].dev);
	uart_line_set(peers[1].dev);

	peers[0].data = &peers[1];
	peers[1].data = &peers[0];

	ring_buf_init(&peers[0].rb, sizeof(buffer0), buffer0);
	ring_buf_init(&peers[1].rb, sizeof(buffer1), buffer1);

	uart_irq_callback_user_data_set(peers[1].dev, interrupt_handler, &peers[0]);
	uart_irq_callback_user_data_set(peers[0].dev, interrupt_handler, &peers[1]);

	/* Enable rx interrupts */
	uart_irq_rx_enable(peers[0].dev);
	uart_irq_rx_enable(peers[1].dev);
	main_modem_power_init();
}

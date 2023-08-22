#include <getopt.h>
#include <stdio.h>
#include <unistd.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/drivers/uart.h>

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart0), okay)
static const struct device *uart0_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

K_MSGQ_DEFINE(uart0_msgq, 128, 10, 4);

static const char at_cmd_usage_str[] =
    "Usage: gnss <sub-command>\n"
    "\n"
    "where <command> is one of the following:\n"
    "  help:   Show this message\n"
    "  send:   Send GNSS command\n";

static char rx_buf[128];
static int rx_buf_pos;

static void at_send_uart(char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++)
	{
		uart_poll_out(uart0_dev, buf[i]);
	}
	uart_poll_out(uart0_dev, '\r');
	uart_poll_out(uart0_dev, '\n');
}

static void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart0_dev))
	{
		return;
	}

	while (uart_irq_rx_ready(uart0_dev))
	{
		uart_fifo_read(uart0_dev, &c, 1);

		if ((c == '\n' || c == '\r') && rx_buf_pos > 0)
		{
			rx_buf[rx_buf_pos] = '\0';

			/* if queue is full, message is silently dropped */
			k_msgq_put(&uart0_msgq, &rx_buf, K_NO_WAIT);

			/* reset the buffer (it was copied to the msgq) */
			rx_buf_pos = 0;
		}
		else if (rx_buf_pos < (sizeof(rx_buf) - 1))
		{
			rx_buf[rx_buf_pos++] = c;
		}
		/* else: characters beyond buffer size are dropped */
	}
}

void gnss_init()
{
	const struct shell *shell = shell_backend_uart_get_ptr();

#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart0), okay)
	if (!device_is_ready(uart0_dev))
	{
		printk("UART device not found!");
		return;
	}

	uart_irq_callback_user_data_set(uart0_dev, serial_cb, NULL);
	uart_irq_rx_enable(uart0_dev);

	char tx_buf[128];

	while (k_msgq_get(&uart0_msgq, &tx_buf, K_FOREVER) == 0)
	{
		shell_fprintf(shell, SHELL_NORMAL, "%s", tx_buf);
	}
#endif
}

K_THREAD_DEFINE(gnss, 2048, gnss_init, NULL, NULL, NULL, 6, 0, 0);
#endif
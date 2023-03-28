
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(modem, CONFIG_ETC_TEST_LOG_LEVEL);

static const struct device *uart_dev = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(quectel_bg95)));

#define RX_BUF_SIZE	128

K_MSGQ_DEFINE(uart_msgq, RX_BUF_SIZE, 10, 4);

static const struct gpio_dt_spec lte_on_off_gpio_dt =
    GPIO_DT_SPEC_GET(DT_NODELABEL(quectel_bg95), mdm_on_off_gpios);
static const struct gpio_dt_spec power_gpio_dt =
    GPIO_DT_SPEC_GET(DT_NODELABEL(quectel_bg95), mdm_power_gpios);
static const struct gpio_dt_spec pon_trig_gpio_dt =
    GPIO_DT_SPEC_GET(DT_NODELABEL(quectel_bg95), mdm_pon_trig_gpios);
static const struct gpio_dt_spec psm_ind_gpio_dt =
    GPIO_DT_SPEC_GET(DT_NODELABEL(quectel_bg95), mdm_psm_ind_gpios);
#if DT_NODE_EXISTS(DT_NODELABEL(modem_uart_oe))
static const struct gpio_dt_spec modem_uart_oe_dt =
    GPIO_DT_SPEC_GET_OR(DT_NODELABEL(modem_uart_oe), control_gpios, 0);
#endif

static const char at_cmd_usage_str[] =
    "Usage: at <sub-command>\n"
    "\n"
    "where <command> is one of the following:\n"
    "  help:   Show this message\n"
    "  send:   Send AT command\n";

static char rx_buf[RX_BUF_SIZE];
static int rx_buf_pos;

static void at_send_uart(char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++)
	{
		uart_poll_out(uart_dev, buf[i]);
	}
	uart_poll_out(uart_dev, '\r');
	uart_poll_out(uart_dev, '\n');
}

static void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_dev))
	{
		return;
	}

	while (uart_irq_rx_ready(uart_dev))
	{
		uart_fifo_read(uart_dev, &c, 1);

		if ((c == '\n' || c == '\r') && rx_buf_pos > 0)
		{
			rx_buf[rx_buf_pos] = '\0';

			/* if queue is full, message is silently dropped */
			k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);

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

static int at_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int ret = 0;

	if (argc < 2)
	{
		goto show_usage;
	}

	if (strcmp(argv[1], "send") == 0)
	{
		shell_print(shell, "Send command: %s", argv[2]);
		at_send_uart(argv[2]);
	}
	else if (strcmp(argv[1], "help") == 0)
	{
		goto show_usage;
	}
	else
	{
		shell_print(shell, "Unsupported command=%s", argv[1]);
		ret = -EINVAL;
		goto show_usage;
	}

	return 0;

show_usage:
	shell_print(shell, "%s", at_cmd_usage_str);

	return 0;
}

SHELL_CMD_REGISTER(at, NULL, "Commands for interact with modem.",
		   at_shell_cmd);

static int cmd_modem_gpio_set(const struct shell *shell, size_t argc, char **argv)
{
	int enable;

	if (argc != 3)
	{
		goto error;
	}

	if (strcmp(argv[1], "uart_oe") == 0)
	{
#if DT_NODE_EXISTS(DT_NODELABEL(modem_uart_oe))
		enable = atoi(argv[1]);
		gpio_pin_set_dt(&modem_uart_oe_dt, enable);
#else
		shell_print(shell, "Not supported");
		return -ENOTSUP;
#endif
	}
	else if (strcmp(argv[1], "pwrkey") == 0)
	{
		gpio_pin_set_dt(&power_gpio_dt, 1U);
		k_sleep(K_MSEC(1000));
		gpio_pin_set_dt(&power_gpio_dt, 0U);
	}
	else if (strcmp(argv[1], "on_off") == 0)
	{
		enable = atoi(argv[1]);
		gpio_pin_set_dt(&lte_on_off_gpio_dt, enable);
	} else {
		goto error;
	}

	return 0;

error:
	shell_print(shell, "Usage:\n"
			   "%s <pin> 0/1\n"
			   "  valid pins:\n"
			   "  - uart_oe\n"
			   "  - pwrkey (this triggers the full sequence)\n"
			   "  - on_off",
		    argv[0]);
	return -1;
}

SHELL_CMD_ARG_REGISTER(etc_modem_gpio_set, NULL, 
		       "Enable/disable modem UART logic translator", 
		       cmd_modem_gpio_set, 1, 2);

static void pin_init(void)
{

	gpio_pin_configure_dt(&lte_on_off_gpio_dt, GPIO_OUTPUT_ACTIVE);

	gpio_pin_configure_dt(&power_gpio_dt, GPIO_OUTPUT);
	gpio_pin_set_dt(&power_gpio_dt, 0U);
	k_sleep(K_MSEC(500));
	gpio_pin_set_dt(&power_gpio_dt, 1U);
	k_sleep(K_MSEC(1000));
	gpio_pin_set_dt(&power_gpio_dt, 0U);
	k_sleep(K_MSEC(1000));
#if DT_NODE_EXISTS(DT_NODELABEL(modem_uart_oe))
	gpio_pin_configure_dt(&modem_uart_oe_dt, GPIO_OUTPUT_ACTIVE);
#endif
	LOG_INF("IO Done");
}

void modem_init()
{
	pin_init();

	const struct shell *shell = shell_backend_uart_get_ptr();

	uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
	uart_irq_rx_enable(uart_dev);

	char tx_buf[32];

	while (k_msgq_get(&uart_msgq, &tx_buf, K_FOREVER) == 0)
	{
		shell_print(shell, "%s", tx_buf);
	}
}

K_THREAD_DEFINE(modem, 1024, modem_init, NULL, NULL, NULL, 5, 0, 0);
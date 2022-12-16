#include <getopt.h>
#include <stdio.h>
#include <unistd.h>
#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <kernel.h>
#include <shell/shell.h>

static const struct device *uart_dev = NULL;

K_MSGQ_DEFINE(uart0_msgq, 128, 10, 4);

static const char at_cmd_usage_str[] =
    "Usage: gnss <sub-command>\n"
    "\n"
    "where <command> is one of the following:\n"
    "  help:   Show this message\n"
    "  send:   Send GNSS command\n";

static char rx_buf[128];
static int rx_buf_pos;

static void at_send_uart(char *buf) {
  int msg_len = strlen(buf);

  for (int i = 0; i < msg_len; i++) {
    uart_poll_out(uart_dev, buf[i]);
  }
  uart_poll_out(uart_dev, '\r');
  uart_poll_out(uart_dev, '\n');
}

static void serial_cb(const struct device *dev, void *user_data) {
  uint8_t c;
  
  if (!uart_irq_update(uart_dev)) {
    return;
  }

  while (uart_irq_rx_ready(uart_dev)) {
    uart_fifo_read(uart_dev, &c, 1);

    if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
      rx_buf[rx_buf_pos] = '\0';

      /* if queue is full, message is silently dropped */
      k_msgq_put(&uart0_msgq, &rx_buf, K_NO_WAIT);

      /* reset the buffer (it was copied to the msgq) */
      rx_buf_pos = 0;
    } else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
      rx_buf[rx_buf_pos++] = c;
    }
    /* else: characters beyond buffer size are dropped */
  }
}

static int gnss_shell_cmd(const struct shell *shell, size_t argc, char **argv) {
  int ret = 0;
  bool uartconf_option_given = false;

  if (argc < 2) {
    goto show_usage;
  }

  if (strcmp(argv[1], "send") == 0) {
    shell_print(shell, "Send command: %s", argv[2]);
    at_send_uart(argv[2]);
  } else if (strcmp(argv[1], "help") == 0) {
    goto show_usage;
  } else {
    shell_print(shell, "Unsupported command=%s", argv[1]);
    ret = -EINVAL;
    goto show_usage;
  }

  return 0;

show_usage:
  shell_print(shell, "%s", at_cmd_usage_str);

  return 0;
}

void gnss_init() {
  uart_dev = device_get_binding("UART_0");
  const struct shell *shell = shell_backend_uart_get_ptr();

  if (!device_is_ready(uart_dev)) {
    printk("UART device not found!");
    return;
  }

  SHELL_CMD_REGISTER(gnss, NULL, "Commands for interact with gnss.",
                     gnss_shell_cmd);
  uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
  uart_irq_rx_enable(uart_dev);

  char tx_buf[128];

  while (k_msgq_get(&uart0_msgq, &tx_buf, K_FOREVER) == 0) {
    shell_fprintf(shell, SHELL_NORMAL, "%s", tx_buf);
  }
}

K_THREAD_DEFINE(gnss, 2048, gnss_init, NULL, NULL, NULL, 6, 0, 0);
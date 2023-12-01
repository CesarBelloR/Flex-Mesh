#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/shell/shell.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#include "etc_device.h"
#include "etc_device_record.h"

int main(void)
{
	etc_device_record_init();
	etc_device_init();
	return 0;
}

static int cmd_write_record(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 2) {
		int num_of_sample = atoi(argv[1]);
		for (int i = 0; i < num_of_sample; i++) {
			etc_device_write();
		}
	} else {
		shell_error(shell, "Invalid input parameter for generating record");
	}

	return 0;
}

static int cmd_read_record(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 2) {
		int num_of_sample = atoi(argv[1]);
		for (int i = 0; i < num_of_sample; i++) {
			etc_device_read();
		}
	}
	return 0;
}

static int cmd_report_record(const struct shell *shell, size_t argc, char **argv)
{
	etc_device_report(shell);
	return 0;
}

static int cmd_export_record(const struct shell *shell, size_t argc, char **argv)
{
	etc_device_export_old_structure(shell);
	return 0;
}

static int cmd_dump_record(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t* record = etc_device_record_dump();
	shell_hexdump(shell, record, ETC_DEVICE_RECORD_BUF_SIZE);
	return 0;
}

static int cmd_dump_old_record(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t* record = etc_device_dump();
	shell_hexdump(shell, record, MAX_RECORD_NO_OFFSET_ID);
	return 0;
}

static int cmd_clean_record(const struct shell *shell, size_t argc, char **argv)
{
	etc_device_record_clean_up();
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_record,
	SHELL_CMD(write, NULL, "Init", cmd_write_record),
	SHELL_CMD(read, NULL, "Read", cmd_read_record),
	SHELL_CMD(report, NULL, "Report", cmd_report_record),
	SHELL_CMD(export, NULL, "Export", cmd_export_record),
	SHELL_CMD(dump, NULL, "Dump", cmd_dump_record),
	SHELL_CMD(dump_old, NULL, "Dump Old", cmd_dump_old_record),
	SHELL_CMD(clean, NULL, "Clean", cmd_clean_record),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(record, &sub_record, "ETC Record Management", NULL);
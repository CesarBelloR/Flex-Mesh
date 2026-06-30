#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include "etc_device.h"

/* Minimal programmable settings store used by the calibration-load unit test.
 * Each known setting id can be marked present with a float value, or left absent
 * so etc_device_read_setting() reports failure for it. */
#define MOCK_MAX_ENTRIES 16

static struct {
	uint16_t id;
	float value;
} entries[MOCK_MAX_ENTRIES];
static int num_entries;

void mock_etc_device_reset(void)
{
	num_entries = 0;
	memset(entries, 0, sizeof(entries));
}

void mock_etc_device_set_float(uint16_t id, float value)
{
	if (num_entries >= MOCK_MAX_ENTRIES) {
		return;
	}
	entries[num_entries].id = id;
	entries[num_entries].value = value;
	num_entries++;
}

int etc_device_read_setting(uint16_t setting_id, void *setting, int setting_size)
{
	for (int i = 0; i < num_entries; i++) {
		if (entries[i].id == setting_id) {
			memcpy(setting, &entries[i].value, setting_size);
			return 0;
		}
	}
	return -ENOENT;
}

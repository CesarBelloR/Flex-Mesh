#include <string.h>
#include "etc_settings.h"

int etc_get_relay_iccid(char *buf, int buf_len)
{
	const char iccid[] = "1234";
	int copy_size;

	copy_size = sizeof(iccid) < buf_len ? sizeof(iccid) : buf_len;
	memcpy(buf, iccid, copy_size);
	return copy_size;
}

static enum etc_device_type mock_device_type = ETC_DEVICE_TYPE_LOGGER;

void mock_set_device_type(enum etc_device_type type)
{
	mock_device_type = type;
}

enum etc_device_type etc_get_device_type(void)
{
	return mock_device_type;
}

/* Tests for the reclaim-request APIs need the device to look like a relay
 * (etc_device_is_relay() consults this). */
enum etc_device_mode etc_get_device_mode(void)
{
	return ETC_DEVICE_MODE_RELAY;
}
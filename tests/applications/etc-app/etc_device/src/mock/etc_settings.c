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

enum etc_device_type etc_get_device_type(void)
{
	return ETC_DEVICE_TYPE_LOGGER;
}

/* Tests for the reclaim-request APIs need the device to look like a relay
 * (etc_device_is_relay() consults this). */
enum etc_device_mode etc_get_device_mode(void)
{
	return ETC_DEVICE_MODE_RELAY;
}
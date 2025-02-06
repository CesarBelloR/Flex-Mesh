#include <string.h>

int etc_get_relay_iccid(char *buf, int buf_len) 
{
	const char iccid[] = "1234";
	int copy_size;

	copy_size = sizeof(iccid) < buf_len ? sizeof(iccid) : buf_len;
	memcpy(buf, iccid, copy_size);
	return copy_size;
}
#include "etc_img.h"
#include <zephyr/toolchain.h>

#ifndef UNUSED_ARG
#define UNUSED_ARG(x) (void)(x)
#endif

int etc_img_get_pubkey_hash(uint8_t *hash)
{
	UNUSED_ARG(hash);
	return 0;
}
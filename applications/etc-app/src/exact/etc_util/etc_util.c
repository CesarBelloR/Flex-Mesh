/*
 * Copyright (c) 2023 EXACT Technology
*/
#include <zephyr/kernel.h>

#include "etc_util.h"


int util_validate_u32(uint32_t data, uint32_t lower_limit, uint32_t upper_limit) 
{
	if ((data < lower_limit) || (data > upper_limit)) {
		return -EINVAL;
	}
	return 0;
}
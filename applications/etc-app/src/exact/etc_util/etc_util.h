/*
 * Copyright (c) 2023 EXACT Technology
*/

#ifndef ETC_UTIL_H__
#define ETC_UTIL_H__

/**
 * @brief Validate given data against given lower and upper limits.
 * The limits are inclusive.
 * 
 * @param data Value to be validated
 * @param lower_limit Accepted lower limit
 * @param upper_limit Accepted upper limit
 * @return 0 if data within limits
 *         -EINVAL if data outside of limits
*/
int util_validate_u32(uint32_t data, uint32_t lower_limit, uint32_t upper_limit);

/**
 * @brief Validate given data against given lower and upper limits.
 * The limits are inclusive.
 * 
 * @param data Value to be validated
 * @param lower_limit Accepted lower limit
 * @param upper_limit Accepted upper limit
 * @return 0 if data within limits
 *         -EINVAL if data outside of limits
*/
inline static int util_validate_u16(uint16_t data, uint16_t lower_limit, uint16_t upper_limit) 
{
	return util_validate_u32((uint32_t)data, (uint32_t)lower_limit, (uint32_t)upper_limit);
}

/**
 * @brief Validate given data against given lower and upper limits.
 * The limits are inclusive.
 * 
 * @param data Value to be validated
 * @param lower_limit Accepted lower limit
 * @param upper_limit Accepted upper limit
 * @return 0 if data within limits
 *         -EINVAL if data outside of limits
*/
inline static int util_validate_u8(uint8_t data, uint8_t lower_limit, uint8_t upper_limit) 
{
	return util_validate_u32((uint32_t)data, (uint32_t)lower_limit, (uint32_t)upper_limit);
}

#endif
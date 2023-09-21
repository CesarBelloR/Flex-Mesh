/*
 * Copyright (c) 2023 EXACT Technology
*/

#ifndef ETC_UTIL_H__
#define ETC_UTIL_H__

#include <stdint.h>

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

/**
 * @brief Round up to the nearest integer that is divisible by the divisor
 * 
 * Round up to the nearest integer after the division.
 * @param value Value to be rounded.
 * @param divisor Divisor for rounding up.
 * @return Resulting integer after rounding.
*/
uint16_t ceil_int(uint16_t value, uint16_t divisor);

/**
 * @brief Round up/down to the nearest integer that is divisible by the divisor
 * 
 * Round up/down to the nearest integer according to regular rounding rules
 * (round up when fractional part >= 0.5, otherwise round down).
 * 
 * Example:
 * Round 16 to the nearest integer that's divisibe by 3.
 * 
 * uint16_t result;
 * result = round_int(16U, 3U);
 * 
 * Value 15 will be stored in result.
 * 
 * @param value Value to be rounded.
 * @param divisor Divisor for rounding.
 * @return Result of integer division.
*/
uint16_t round_int(uint16_t value, uint16_t divisor);

#endif
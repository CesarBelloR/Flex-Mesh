/*
 * Copyright (c) 2023 EXACT Technology
*/

#ifndef ETC_UTIL_H__
#define ETC_UTIL_H__

#include <stdint.h>

/* Define a bit lenght for encrypt/decrypt method */
#define AES_KEY_BITLEN 128
#define AES_KEY_BLOCK_SIZE (AES_KEY_BITLEN / 8)

#define NANODEGREE_TO_DEGREE(x) (x / 1000000000.0f)

/**
 * Convert `tm_year` in a `struct tm` to a human-readable year format.
*/
#define TM_YEAR_TO_YEAR(x) (x + 1900)
/**
 * Convert `tm_mon` in a `struct tm` to a human-readable month format.
*/
#define TM_MON_TO_MONTH(x) (x + 1)

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

/**
 * Same as @ref round_int just with 32 bit integer values.
*/
uint32_t round_int32(uint32_t value, uint32_t divisor);

#ifdef CONFIG_ETC_BLE_ENCRYPTION
/**
 * @brief Encrypts the given data using the provided pre-shared key (PSK).
 *
 * This function takes a pre-shared key (PSK), data to be encrypted, and its length.
 * It performs encryption and stores the result in the provided buffer.
 *
 * @param psk               The pre-shared key used for encryption.
 * @param encrypting_data   The data to be encrypted.
 * @param encrypting_length The length of the data to be encrypted.
 * @param encrypted_data    The buffer to store the encrypted data.
 *
 * @return                  Returns 0 on success, or a non-zero value on failure.
 */
int encrypt_data(const unsigned char *psk, 
	const unsigned char *encrypting_data, size_t encrypting_length, 
	unsigned char *encrypted_data);

/**
 * @brief Decrypts the given encrypted data using the provided pre-shared key (PSK).
 *
 * This function takes a pre-shared key (PSK), encrypted data, and its length.
 * It performs decryption and stores the result in the provided buffer.
 *
 * @param psk               The pre-shared key used for decryption.
 * @param encrypted_data    The data to be decrypted.
 * @param encrypted_length  The length of the data to be decrypted.
 * @param decrypted_data    The buffer to store the decrypted data.
 *
 * @return                  Returns 0 on success, or a non-zero value on failure.
 */
int decrypt_data(const unsigned char *psk, 
	const unsigned char *encrypted_data, size_t encrypted_length, 
	unsigned char *decrypted_data);
#endif
/**
 * @brief Validate if the given value is a valid LoRa probe offset.
 *
 * This function checks if the provided `value` meets the LoRa probe offset
 * criteria: it should be a multiple of 900 and in the range [0, 2700].
 * 
 * @param value The value to be validated as a LoRa probe offset.
 * @return int Returns 0 if the value is valid, -EINVAL otherwise.
 */
inline static int util_validate_in_lora_probe_offset(uint16_t value) 
{
	if ((value % 900 == 0) && (value >= 0) && (value <= 2700)) {
		return 0;
	} 

	return -EINVAL;
}

/**
 * Parse null-terminated buffer for a float value. Non-digit (except for +/-)
 * characters at the beginning of the input string are skipped.
 * 
 * @param float_field Null-terminated string buffer that contains a float value.
 * @param float_value Pointer that the resulting float value will be saved to.
 * 
 * @retval 0 success
 * @retval -EINVAL Input arguments are invalid or value could not be parsed.
*/
int parse_for_float(const char *float_field, float *float_value);

/**
 * Parse null-terminated buffer for an integer value. Non-digit (except for +/-)
 * characters at the beginning of the input string are skipped.
 * 
 * @param int_field Null-terminated string buffer that contains an int value.
 * @param int_value Pointer that the resulting int value will be saved to.
 * 
 * @retval 0 success
 * @retval -EINVAL Input arguments are invalid or value could not be parsed.
*/
int parse_for_int(const char *int_field, int *int_value);

#endif
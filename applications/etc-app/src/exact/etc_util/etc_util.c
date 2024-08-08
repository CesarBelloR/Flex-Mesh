/*
 * Copyright (c) 2023 EXACT Technology
*/
#include <zephyr/kernel.h>

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#ifdef CONFIG_ETC_BLE_ENCRYPTION
#include <mbedtls/aes.h>
#include <mbedtls/cipher.h>
#endif

#include "etc_util.h"

#define ETC_CAPE_IV 21

int util_validate_u32(uint32_t data, uint32_t lower_limit, uint32_t upper_limit) 
{
	if ((data < lower_limit) || (data > upper_limit)) {
		return -EINVAL;
	}
	return 0;
}

uint32_t round_int32(uint32_t value, uint32_t divisor)
{
	uint32_t remainder = value % divisor;
	uint32_t quotient = value / divisor;

	if (remainder >= divisor / 2U) {
		quotient++;
	}
	return quotient * divisor;
}

uint16_t round_int(uint16_t value, uint16_t divisor) 
{
	return (uint16_t)round_int32(value, divisor);
}

uint16_t ceil_int(uint16_t value, uint16_t divisor) 
{
	uint16_t remainder = value % divisor;
	uint16_t quotient = value / divisor;

	if (remainder != 0) {
		quotient++;
	}
	
	return quotient * divisor;
}

bool is_field_iv(const char *field)
{
	if (strlen(field) == 1 && *field == ETC_CAPE_IV) {
		return true;
	}
	return false;
}

int parse_for_float(const char *float_field, float *float_value)
{
	const char *buf = float_field;
	char *end;

	if ((float_field == NULL) || (float_value == NULL)) {
		return -EINVAL;
	}

	/* Allow a field that only contains the encryption IV */
	if (is_field_iv(float_field)) {
		return 1;
	}

	*float_value = strtof(buf, &end);

	if ((buf == end) ||
	    (*end != '\0')) {
		return -ENOMSG;
	}
	return 0;
}

int parse_for_int(const char *int_field, int *int_value)
{
	const char *buf = int_field;
	char *end;

	if ((int_field == NULL) || (int_value == NULL)) {
		return -EINVAL;
	}	
	
	/* Allow a field that only contains the encryption IV */
	if (is_field_iv(int_field)) {
		return 1;
	}

	*int_value = strtol(buf, &end, 0);

	if ((buf == end) ||
	    (*end != '\0')) {
		return -ENOMSG;
	}
	return 0;
}

int parse_for_uint(const char *int_field, uint32_t *int_value)
{
	const char *buf = int_field;
	char *end;

	if ((int_field == NULL) || (int_value == NULL)) {
		return -EINVAL;
	}

	/* Allow a field that only contains the encryption IV */
	if (is_field_iv(int_field)) {
		return 1;
	}

	*int_value = strtoul(buf, &end, 0);

	if ((buf == end) ||
	    (*end != '\0')) {
		return -ENOMSG;
	}
	return 0;
}

#ifdef CONFIG_ETC_BLE_ENCRYPTION
int encrypt_data(const unsigned char *psk, const unsigned char *encrypting_data, size_t encrypting_length, unsigned char *encrypted_data) {
	size_t output_length = 0;
	size_t partial_length = 0;
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
	const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_CBC);
    mbedtls_cipher_setup(&ctx, info);
    mbedtls_cipher_setkey(&ctx, psk, AES_KEY_BITLEN, MBEDTLS_ENCRYPT);
    mbedtls_cipher_update(&ctx, encrypting_data, encrypting_length, encrypted_data, &partial_length);
	output_length += partial_length;
    mbedtls_cipher_finish(&ctx, encrypted_data + output_length, &partial_length);
	output_length += partial_length;
    mbedtls_cipher_free(&ctx);
    return output_length;
}

int decrypt_data(const unsigned char *psk, const unsigned char *encrypted_data, size_t encrypted_length, unsigned char *decrypted_data) {
	size_t output_length = 0;
	size_t partial_length = 0;
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
	const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_CBC);
    mbedtls_cipher_setup(&ctx, info);
    mbedtls_cipher_setkey(&ctx, psk, AES_KEY_BITLEN, MBEDTLS_DECRYPT);
    mbedtls_cipher_update(&ctx, encrypted_data, encrypted_length, decrypted_data, &partial_length);
	output_length += partial_length;
    mbedtls_cipher_finish(&ctx, decrypted_data + output_length, &partial_length);
	output_length += partial_length;
    mbedtls_cipher_free(&ctx);

    return output_length;
}
#endif

/**
 * @brief Extracts the command from the buffer, 
 * assumes the command is between quotes and ends with a colon.
 */
int extract_command(const char *buf, char *command, size_t max_len)
{
	const char *start = strchr(buf, '\'');
	if (!start) {
		return -EINVAL;
	}
	start++;
	const char *end = strchr(start, ':');
	if (!end) {
		return -EINVAL;
	}
	size_t length = end - start;
	if (length >= max_len) {
		return -EINVAL;
	}
	strncpy(command, start, length);
	command[length] = '\0';
	return 0;
}

/**
 * @brief Extracts a field from the buffer until the delimiter, 
 * updating the buffer pointer
 */
static int extract_field(const char **buf, char delimiter, char *output, 
			 size_t max_len) 
{
	const char *start = *buf;
	const char *end = strchr(start, delimiter);

	if (!end) {
		if (delimiter != '\0') {
			return -EINVAL;
		}
		end = start + strlen(start);
	}

	size_t length = end - start;
	if (length >= max_len) {
		return -EINVAL;
	}
	strncpy(output, start, length);
	output[length] = '\0';

	if (delimiter != '\0') {
		*buf = end + 1;
	}

	return 0;
}

int etc_common_parser_reclaim_replay_command(const char* buf, const size_t len, 
	struct relay_reclaim_request* request) 
{
	if (request == NULL) {
		return -EINVAL;
	}

	char command[10];

	// Extract command
	if (extract_command(buf, command, sizeof(command)) != 0) {
		return -EINVAL;
	}

	if ((strcmp(command, "RECLAIM") != 0) || 
	    (strlen(command) != strlen("RECLAIM"))) {
		return -EINVAL;
	}
	
	// Move the pointer to after the command and delimiter
	const char *current_position = strchr(buf, ':') + 1;

	// Extract logger id
	if (extract_field(&current_position, ',', request->logger_id, 
	    UTIL_LOGGER_ID_SIZE) != 0) {
		return -EINVAL;
	}

	// Extract start time
	char time_str[16];
	if (extract_field(&current_position, ',', time_str, 
	    sizeof(time_str)) != 0) {
		return -EINVAL;
	}
	
	if (parse_for_int(time_str, &request->start_time) != 0) {
		return -EINVAL;
	}

	// Extract stop time
	if (extract_field(&current_position, '\'', time_str, 
	    sizeof(time_str)) != 0) {
		return -EINVAL;
	}

	if (parse_for_int(time_str, &request->stop_time) != 0) {
		return -EINVAL;
	}
	
	return 0;
}

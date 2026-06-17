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

int etc_common_parser_reclaim_replay_command(const char *buf, const size_t len,
					     struct relay_reclaim_request *request)
{
	if (request == NULL) {
		return -EINVAL;
	}

	memset(request, 0, sizeof(*request));

	char command[10];

	// Extract command
	if (extract_command(buf, command, sizeof(command)) != 0) {
		return -EINVAL;
	}

	if ((strcmp(command, "RECLAIM") != 0) || (strlen(command) != strlen("RECLAIM"))) {
		return -EINVAL;
	}

	// Move the pointer to after the command and delimiter
	const char *current_position = strchr(buf, ':') + 1;

	/* Peek the first sub-field (up to ',' or terminating quote) without
	 * consuming it, so we can branch on the keyword. */
	const char *next_comma = strchr(current_position, ',');
	const char *next_quote = strchr(current_position, '\'');
	const char *next_end = next_quote;
	if (next_end == NULL || (next_comma != NULL && next_comma < next_end)) {
		next_end = next_comma;
	}
	if (next_end == NULL) {
		return -EINVAL;
	}
	size_t first_len = next_end - current_position;

	/* RECLAIM:count */
	if (first_len == strlen("count") && strncmp(current_position, "count", first_len) == 0) {
		if (next_end != next_quote) {
			return -EINVAL;
		}
		request->subcmd = RELAY_RECLAIM_SUBCMD_COUNT;
		return 0;
	}

	/* RECLAIM:clear */
	if (first_len == strlen("clear") && strncmp(current_position, "clear", first_len) == 0) {
		if (next_end != next_quote) {
			return -EINVAL;
		}
		request->subcmd = RELAY_RECLAIM_SUBCMD_CLEAR;
		return 0;
	}

	/* RECLAIM:i,<idx> — only matches when followed by a comma; a bare "i"
	 * (no comma) falls through and is treated as a (short) logger id below. */
	if (first_len == 1 && current_position[0] == 'i' && next_end == next_comma) {
		current_position = next_comma + 1;
		char idx_str[12];
		if (extract_field(&current_position, '\'', idx_str, sizeof(idx_str)) != 0) {
			return -EINVAL;
		}
		if (parse_for_int(idx_str, &request->index) != 0) {
			return -EINVAL;
		}
		request->subcmd = RELAY_RECLAIM_SUBCMD_GET_IDX;
		return 0;
	}

	/* From here on the first field is treated as a logger id. */
	if (first_len == 0 || first_len >= UTIL_LOGGER_ID_SIZE) {
		return -EINVAL;
	}

	/* RECLAIM:<logger> — list reclaims for one logger id */
	if (next_end == next_quote) {
		if (extract_field(&current_position, '\'', request->logger_id,
				  UTIL_LOGGER_ID_SIZE) != 0) {
			return -EINVAL;
		}
		request->subcmd = RELAY_RECLAIM_SUBCMD_LIST_LOGGER;
		return 0;
	}

	/* RECLAIM:<logger>,<start>,<stop> — add a reclaim request. */
	if (extract_field(&current_position, ',', request->logger_id, UTIL_LOGGER_ID_SIZE) != 0) {
		return -EINVAL;
	}

	char time_str[16];
	if (extract_field(&current_position, ',', time_str, sizeof(time_str)) != 0) {
		return -EINVAL;
	}
	if (parse_for_int(time_str, &request->start_time) != 0) {
		return -EINVAL;
	}

	if (extract_field(&current_position, '\'', time_str, sizeof(time_str)) != 0) {
		return -EINVAL;
	}
	if (parse_for_int(time_str, &request->stop_time) != 0) {
		return -EINVAL;
	}

	request->subcmd = RELAY_RECLAIM_SUBCMD_ADD;
	return 0;
}

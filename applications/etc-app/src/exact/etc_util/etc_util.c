/*
 * Copyright (c) 2023 EXACT Technology
*/
#include <zephyr/kernel.h>

#include <stdlib.h>
#include <ctype.h>

#ifdef CONFIG_ETC_BLE_ENCRYPTION
#include <mbedtls/aes.h>
#include <mbedtls/cipher.h>
#endif

#include "etc_util.h"

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

int parse_for_float(const char *float_field, float *float_value)
{
	const char *buf = float_field;
	char *end;

	if ((float_field == NULL) || (float_value == NULL)) {
		return -EINVAL;
	}

	while (!isdigit(*buf) && *buf != '\0') {
		buf++;
	}
	*float_value = strtof(buf, &end);

	if (buf == end) {
		return -EINVAL;
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
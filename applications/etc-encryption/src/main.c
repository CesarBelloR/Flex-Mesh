#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/base64.h>
#include <mbedtls/aes.h>
#include <mbedtls/cipher.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define AES_KEY_BITLEN 128

int encrypt_data(const unsigned char *psk, const unsigned char *input_data, size_t input_length, unsigned char *output_data) {
	size_t output_length = 0;
	size_t partial_length = 0;
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
	const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_CBC);
    mbedtls_cipher_setup(&ctx, info);
    mbedtls_cipher_setkey(&ctx, psk, AES_KEY_BITLEN, MBEDTLS_ENCRYPT);

    mbedtls_cipher_update(&ctx, input_data, input_length, output_data, &partial_length);
	output_length += partial_length;
	LOG_INF("%d", output_length);
    mbedtls_cipher_finish(&ctx, output_data + output_length, &partial_length);
	output_length += partial_length;
	LOG_INF("%d", output_length);
    mbedtls_cipher_free(&ctx);
    return output_length;
}

int decrypt_data(const unsigned char *psk, const unsigned char *input_data, size_t input_length, unsigned char *output_data) {
	size_t output_length = 0;
	size_t partial_length = 0;
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
	const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_CBC);
    mbedtls_cipher_setup(&ctx, info);
    mbedtls_cipher_setkey(&ctx, psk, AES_KEY_BITLEN, MBEDTLS_DECRYPT);

    mbedtls_cipher_update(&ctx, input_data, input_length, output_data, &partial_length);
	output_length += partial_length;
	LOG_INF("%d", output_length);
    mbedtls_cipher_finish(&ctx, output_data + output_length, &partial_length);
	output_length += partial_length;
	LOG_INF("%d", output_length);
    mbedtls_cipher_free(&ctx);

    return output_length;
}

#define ETC_SETTING_PSK_LEN 33
#define CONFIG_MODEM_QUECTEL_BG95_M3_PSK_KEY "70475440693636213646256D7744"

char msg[516] = "1.0.0-rc1,11000011,4.09,49,1708264560,*,*,*,26.26,26.93,47.03,0,";
char out_msg[2048] = {0x00};
uint8_t tmp_psk[ETC_SETTING_PSK_LEN];
char decrypted_msg[2048] = {0x00};

int main(void)
{
	LOG_HEXDUMP_INF(msg, sizeof(msg), "DECRYTPED");
	int ret;
	int input_len = strlen(CONFIG_MODEM_QUECTEL_BG95_M3_PSK_KEY);
	ret = hex2bin(CONFIG_MODEM_QUECTEL_BG95_M3_PSK_KEY, input_len, tmp_psk, ETC_SETTING_PSK_LEN);
	if (ret < 0) {
		LOG_ERR("Failed to convert Pre-shared key");
	} else {
		LOG_HEXDUMP_INF(tmp_psk, ret, "DUMP");
		memset(out_msg, 0, sizeof(out_msg));
		size_t out_len = 0;
		ret = encrypt_data(tmp_psk, msg, strlen(msg), out_msg);
		if (ret > 0) {
			LOG_HEXDUMP_INF(out_msg, ret, "ENCRYPTED");
			ret = decrypt_data(tmp_psk, out_msg, ret, decrypted_msg);
			LOG_HEXDUMP_INF(decrypted_msg, ret, "DECRYTPED");
		} else {
			LOG_ERR("Failed to encrypt data %d", ret);
		}
	}

	return 0;
}

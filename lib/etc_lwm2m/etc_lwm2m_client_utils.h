#ifndef ETC_LWM2M_CLIENT_UTILS_H__
#define ETC_LWM2M_CLIENT_UTILS_H__

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/lwm2m.h>

struct dtls_psk {
	uint8_t psk[CONFIG_LWM2M_SECURITY_KEY_SIZE];
	uint8_t psk_len;
};

#if defined(CONFIG_EXACT_LWM2M_CLIENT_UTILS_SECURITY_OBJ_SUPPORT)

/**
 * @brief Load the LwM2M DTLS credentials into the modem driver.
 * Retrieve the credentials from the LwM2M objects and use the modem api
 * to set credentials on modem.
 * @param ctx Pointer to lwm2m client struct.
 * @return 0: success,
 *         <0: error.
 */
int lwm2m_init_security(struct lwm2m_ctx *client, const char *ep_name,
			struct dtls_psk *psk);

#endif /* defined(CONFIG_EXACT_LWM2M_CLIENT_UTILS_SECURITY_OBJ_SUPPORT) */

#endif /* ETC_LWM2M_CLIENT_UTILS_H__ */
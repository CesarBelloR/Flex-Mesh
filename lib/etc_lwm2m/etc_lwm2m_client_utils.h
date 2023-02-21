#ifndef LWM2M_CLIENT_UTILS_H__
#define LWM2M_CLIENT_UTILS_H__

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/lwm2m.h>

#if defined(CONFIG_EXACT_LWM2M_CLIENT_UTILS_SECURITY_OBJ_SUPPORT)

int lwm2m_load_credentials_to_modem(struct lwm2m_ctx *ctx);

#endif /* defined(CONFIG_EXACT_LWM2M_CLIENT_UTILS_SECURITY_OBJ_SUPPORT) */

#endif /* LWM2M_CLIENT_UTILS_H__ */
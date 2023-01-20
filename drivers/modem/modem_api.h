/*
 * Copyright (c) 2022 EXACT Technology
 *
 */
#ifndef MODEM_API_H
#define MODEM_API_H

#include <zephyr/syscall_handler.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <errno.h>


enum modem_api_evt_type {
	MODEM_API_CONNECTED_EVT,
        MODEM_API_DISCONNECTED_EVT
};


struct modem_api_evt {
        enum modem_api_evt_type type;
};

typedef void(*modem_api_evt_handler_t)(const struct modem_api_evt *const evt);

typedef int(*modem_api_evt_handler_init_t)(const struct device *dev,
                                           modem_api_evt_handler_t evt_handler);

struct modem_api {
	/**
	 * Mandatory to get in first position.
	 * A network device should indeed provide a pointer on such
	 * net_if_api structure. So we make current structure pointer
	 * that can be casted to a net_if_api structure pointer.
	 */
	struct net_if_api iface_api;

	/* API function to initialize the callback for the 
	 * modem event handler.
	 */
	modem_api_evt_handler_init_t evt_handler_init;
};

/**
 * @brief Register an event handler callback for the modem
 * 	  device specified.
 * 
 * @param dev Pointer to the device
 * @param evt_handler Event handler callback function
 * @return 0 on success, negative on error
*/
inline int modem_evt_handler_init(const struct device *dev,
				  modem_api_evt_handler_t evt_handler)
{
	const struct modem_api *api =
		(const struct modem_api *)dev->api;

	if (api->evt_handler_init == NULL) {
		return -ENOSYS;
	}

	return api->evt_handler_init(dev, evt_handler);
}

char* quectel_bg95_get_imei(void);
char* quectel_bg95_get_revision(void);
char* quectel_bg95_get_sim_number(void);
bool quectel_bg95_is_ready(void);
int quectel_bg95_get_time(char* time_buf);
int quectel_bg95_get_rssi(void);

#endif // MODEM_API_H
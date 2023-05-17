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

#define MDM_MANUFACTURER_LENGTH		  10
#define MDM_MODEL_LENGTH		  16
#define MDM_REVISION_LENGTH		  64
#define MDM_IMEI_LENGTH			  16
#define MDM_IMSI_LENGTH			  16
#define MDM_ICCID_LENGTH		  23

enum modem_api_evt_type {
	MODEM_API_CONNECTED_EVT,
	MODEM_API_DISCONNECTED_EVT,
	MODEM_API_PSM_ENTERED_EVT,
};


struct modem_api_evt {
        enum modem_api_evt_type type;
};

enum modem_api_cred_type {
	MODEM_API_CRED_TYPE_PSK_ID,
	MODEM_API_CRED_TYPE_PSK
};

enum modem_api_psm_cmd {
	MODEM_API_PSM_CMD_WAKEUP,
};

struct modem_static_info {
	char manufacturer[MDM_MANUFACTURER_LENGTH];
	char model[MDM_MODEL_LENGTH];
	char revision[MDM_REVISION_LENGTH];
	char imei[MDM_IMEI_LENGTH];
	char imsi[MDM_IMSI_LENGTH];
	char iccid[MDM_ICCID_LENGTH];
};

typedef void(*modem_api_evt_handler_t)(const struct modem_api_evt *const evt);

typedef int(*modem_api_evt_handler_init_t)(const struct device *dev,
                                           modem_api_evt_handler_t evt_handler);

typedef int(*modem_api_set_credentials_t)(const struct device *dev,
					  enum modem_api_cred_type type,
					  uint8_t *cred_buf, uint8_t cred_len);

typedef int(*modem_api_psm_t)(const struct device *dev,
			      enum modem_api_psm_cmd cmd,
			      void *psm_data);

typedef int(*modem_api_get_static_info_t)(const struct device *dev,
					  struct modem_static_info *info);

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
	/* Set the modem's DTLS credentials.
	*/
	modem_api_set_credentials_t set_credentials;	
	/* Send a PSM command, e.g. wakeup */
	modem_api_psm_t psm_cmd;
	/* Get static information about the modem */
	modem_api_get_static_info_t get_static_info;
};

struct modem_psk {
	uint8_t id[CONFIG_MODEM_QUECTEL_BG95_M3_PSK_ID_MAX_SIZE];
	uint8_t id_len;
	uint8_t psk[CONFIG_MODEM_QUECTEL_BG95_M3_PSK_MAX_SIZE];
	uint8_t psk_len;
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

/**
 * @brief Set the modem security credentials
 * 
 * @param dev Pointer to the modem device
 * @param type Type of the credential to set
 * @param cred_buf Credential buffer
 * @param cred_len Length of the credential buffer
 * @return 0 on success, negative on error
*/
inline static int modem_set_credentials(const struct device *dev,
				 enum modem_api_cred_type type,
				 uint8_t *cred_buf, uint8_t cred_len)
{
	const struct modem_api *api =
		(const struct modem_api *)dev->api;

	if (api->set_credentials == NULL) {
		return -ENOSYS;
	}

	return api->set_credentials(dev, type, cred_buf, cred_len);
}

inline static int modem_psm_cmd(const struct device *dev,
			        enum modem_api_psm_cmd cmd,
			        void *psm_data)
{
	const struct modem_api *api =
		(const struct modem_api *)dev->api;

	if (api->psm_cmd == NULL) {
		return -ENOSYS;
	}

	return api->psm_cmd(dev, cmd, psm_data);
}

inline static int modem_get_static_info(const struct device *dev,
					struct modem_static_info *info)
{
	const struct modem_api *api =
		(const struct modem_api *)dev->api;

	if (api->get_static_info == NULL) {
		return -ENOSYS;
	}

	return api->get_static_info(dev, info);
}

char* quectel_bg95_get_imei(void);
char* quectel_bg95_get_revision(void);
char* quectel_bg95_get_sim_number(void);
bool quectel_bg95_is_ready(void);
int quectel_bg95_get_time(char* time_buf);
int quectel_bg95_get_rssi(void);
int quectel_bg95_get_qual(void);

#endif // MODEM_API_H
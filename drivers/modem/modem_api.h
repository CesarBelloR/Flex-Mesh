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

enum cereg_stat {
	STAT_NOT_REGISTERED = 0,
	STAT_REGISTERED_HOME = 1,
	STAT_SEARCHING = 2,
	STAT_REGISTRATION_DENIED = 3,
	STAT_UNKNOWN = 4,
	STAT_REGISTERED_ROAMING = 5
};

enum cops_mode {
	MODE_AUTOMATIC,
	MODE_MANUAL,
	MODE_MANUAL_DEREGISTER,
	MODE_FORMAT_ONLY,
	MODE_MANUAL_AUTOMATIC
};

enum access_technology {
	ACT_GSM = 0,
	ACT_LTE_M = 8,
	ACT_NB_IOT = 9
};

struct modem_network_data {
	/* Data that can be retrieved from CEREG */
	enum cereg_stat stat;
	uint16_t tac; 
	uint32_t cell_id;
	enum access_technology act;
	uint8_t cause_type;
	uint8_t reject_cause;
	uint16_t active_time_s;
	uint32_t periodic_tau_s;
	/* Data that can be retrieved from COPS */
	enum cops_mode cops_mode;
	/* Mobile Country Code */
	uint16_t mcc;
	/* Mobile Network Code */
	uint16_t mnc;
};

enum modem_api_evt_type {
	/* Modem successfully connected to a network */
	MODEM_API_CONNECTED_EVT,
	/* Modem disconnected from network and trying to re-establish connection */
	MODEM_API_DISCONNECTED_EVT,
	/* PSM entered and modem put to sleep */
	MODEM_API_PSM_ENTERED_EVT,
	/* Modem woken up from PSM and powered on */
	MODEM_API_PSM_WAKEUP_EVT,
	/* Modem powered down manually, NOT in PSM*/
	MODEM_API_POWER_DOWN_EVT,
	/* Modem dynamic data changed (e.g. cellular network, PSM parameters) */
	MODEM_API_DYNAMIC_DATA_UPDATE_EVT
};

struct modem_api_evt {
        enum modem_api_evt_type type;
	union {
		const struct modem_network_data *dynamic_data;
	};
};

enum modem_api_cred_type {
	MODEM_API_CRED_TYPE_PSK
};

struct modem_api_psk {
	/* Null-terminated string with the PSK ID*/
	char *psk_id;
	/* Binary key */
	uint8_t *psk;
	/* Length of PSK */
	uint16_t psk_len;
};

enum modem_api_cmd {
	MODEM_API_CMD_PSM_WAKEUP,
	MODEM_API_CMD_POWER_ON
};

struct modem_static_info {
	char manufacturer[MDM_MANUFACTURER_LENGTH];
	char model[MDM_MODEL_LENGTH];
	char revision[MDM_REVISION_LENGTH];
	char imei[MDM_IMEI_LENGTH];
	char imsi[MDM_IMSI_LENGTH];
	char iccid[MDM_ICCID_LENGTH];
};

enum modem_api_data_request {
	MODEM_API_DATA_REQUEST_STATIC,
	MODEM_API_DATA_REQUEST_DYNAMIC,
	MODEM_API_DATA_REQUEST_POWER_STATE
};

enum modem_power_state {
	MODEM_POWER_OFF,
	MODEM_POWER_ON,
	MODEM_POWER_PSM_PENDING,
	MODEM_POWER_PSM
};

struct modem_api_data {
	union {
		struct modem_static_info modem_info;
		struct modem_network_data modem_network;
		enum modem_power_state power_state;
	};
};

typedef void(*modem_api_evt_handler_t)(const struct modem_api_evt *const evt);

typedef int(*modem_api_evt_handler_init_t)(const struct device *dev,
                                           modem_api_evt_handler_t evt_handler);

typedef int(*modem_api_set_credentials_t)(const struct device *dev,
					  uint8_t cid,
					  enum modem_api_cred_type type,
					  void *cred_data);

typedef int(*modem_api_cmd_t)(const struct device *dev,
			      enum modem_api_cmd cmd,
			      void *data);

typedef int(*modem_api_get_data_t)(const struct device *dev,
				   enum modem_api_data_request request,
				   struct modem_api_data *data);

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
	/* Send a command to the modem, e.g. wakeup from PSM */
	modem_api_cmd_t cmd;
	/* Get static information about the modem */
	modem_api_get_data_t get_data;
};

struct modem_psk {
	uint8_t id[CONFIG_MODEM_QUECTEL_BG95_M3_PSK_ID_MAX_SIZE];
	uint8_t id_len;
	uint8_t psk[CONFIG_MODEM_QUECTEL_BG95_M3_PSK_MAX_SIZE];
	uint8_t psk_len;
};

/**
 * @brief Return the given evt as a string.
*/
static inline char *modem_evt_to_str(enum modem_api_evt_type evt) 
{
	switch (evt) {
	case MODEM_API_CONNECTED_EVT:
		return "MODEM_API_CONNECTED_EVT";
	case MODEM_API_DISCONNECTED_EVT:
		return "MODEM_API_DISCONNECTED_EVT";
	case MODEM_API_PSM_ENTERED_EVT:
		return "MODEM_API_PSM_ENTERED_EVT";
	case MODEM_API_PSM_WAKEUP_EVT:
		return "MODEM_API_PSM_WAKEUP_EVT";
	case MODEM_API_POWER_DOWN_EVT:
		return "MODEM_API_POWER_DOWN_EVT";
	case MODEM_API_DYNAMIC_DATA_UPDATE_EVT:
		return "MODEM_API_DYNAMIC_DATA_UPDATE_EVT";
	default:
		return "Unknown event";
	}
}

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
 * @param cid SSL Context ID for the modem to use
 * @param type Type of the credential to set
 * @param cred_data Pointer to the credential data. Supported types:
 * 	- struct modem_api_psk if `type == MODEM_API_CRED_TYPE_PSK`
 * @return 0 on success, negative on error
*/
inline static int modem_set_credentials(const struct device *dev,
					uint8_t cid,
					enum modem_api_cred_type type,
					void *cred_data)
{
	const struct modem_api *api =
		(const struct modem_api *)dev->api;

	if (api->set_credentials == NULL) {
		return -ENOSYS;
	}

	return api->set_credentials(dev, cid, type, cred_data);
}

inline static int modem_cmd(const struct device *dev,
			        enum modem_api_cmd api_cmd,
			        void *data)
{
	const struct modem_api *api =
		(const struct modem_api *)dev->api;

	if (api->cmd == NULL) {
		return -ENOSYS;
	}

	return api->cmd(dev, api_cmd, data);
}

inline static int modem_get_data(const struct device *dev,
				enum modem_api_data_request request,
				struct modem_api_data *data)
{
	const struct modem_api *api =
		(const struct modem_api *)dev->api;

	if (api->get_data == NULL) {
		return -ENOSYS;
	}

	return api->get_data(dev, request, data);
}

char* quectel_bg95_get_imei(void);
char* quectel_bg95_get_revision(void);
char* quectel_bg95_get_sim_number(void);
bool quectel_bg95_is_ready(void);
int quectel_bg95_get_time(char* time_buf);
int quectel_bg95_get_rssi(void);

/**
 * Retrieve the Modem RSRP (signal strength)
 * 
 * @return RSRP in dBm
*/
int quectel_bg95_get_rsrp(void);

/**
 * Retrieve the Modem RSRQ (signal quality)
 * 
 * @return RSRQ in dBm
*/
int quectel_bg95_get_rsrq(void);

#endif // MODEM_API_H
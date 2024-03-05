/*
 * Copyright (c) 2020 Analog Life LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef QUECTEL_BG95_H
#define QUECTEL_BG95_H

#include <zephyr/kernel.h>
#include <ctype.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/init.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/offloaded_netdev.h>
#include <zephyr/net/net_offload.h>
#include <zephyr/net/socket_offload.h>

#include "modem_context.h"
#include "modem_socket.h"
#include "modem_cmd_handler.h"
#include "modem_iface_uart.h"
#include "modem_api.h"

#define MDM_UART_DEV			DEVICE_DT_GET(DT_INST_BUS(0))
#define MDM_UART_NODE			DT_INST_BUS(0)
#define MDM_CMD_TIMEOUT			K_SECONDS(10)
#define MDM_DNS_TIMEOUT			K_SECONDS(60)
#define MDM_RECV_TIMEOUT		K_SECONDS(10)
#define MDM_CMD_CONN_TIMEOUT		K_SECONDS(120)
#define MDM_REGISTRATION_TIMEOUT	K_SECONDS(180)
#define MDM_SHUTDOWN_TIMEOUT		K_SECONDS(60)
#define MDM_TX_LOCK_TIMEOUT		K_SECONDS(5)
#define MDM_SENDMSG_SLEEP		K_MSEC(1)
#define MDM_NTP_TIMEOUT			K_SECONDS(150)
#define MDM_MAX_DATA_LENGTH		1024
#define MDM_RECV_MAX_BUF		16
#define MDM_RECV_BUF_SIZE		256
#define MDM_MAX_SOCKETS			5
#define MDM_BASE_SOCKET_NUM		0
#define MDM_NETWORK_RETRY_COUNT		10
#define MDM_INIT_RETRY_COUNT		10
#define MDM_PDP_ACT_RETRY_COUNT		10
#define MDM_WAIT_FOR_RSSI_COUNT		10
#define MDM_POWER_DOWN_RETRY_COUNT	10
#define BUF_ALLOC_TIMEOUT		K_SECONDS(1)
#define MDM_MAX_BOOT_TIME		K_SECONDS(15)
#define MDM_RSSI_INVALID		-1000
#define MDM_RSRP_INVALID		-125
#define MDM_PDPDEACT_RECONNECT_DELAY	K_SECONDS(5)

#define MDM_FILE_NAME_MAX_LENGTH	(80)

#define MDM_TLS_CA_FILE_NAME "iot_cacert.pem"
#define MDM_TLS_PRIV_KEY_FILE_NAME "iot_privatekey.pem"
#define MDM_TLS_CLIENT_CERT_FILE_NAME "iot_clientcert.pem"

/* Default lengths of certain things. */
#define MDM_TIME_LENGTH			32
#define MDM_APN_LENGTH			32
#define RSSI_TIMEOUT_SECS		30
#define MDM_WAIT_FOR_RSSI_TIMEOUT	K_SECONDS(2)

#define MDM_APN				CONFIG_MODEM_QUECTEL_BG95_M3_APN
#define MDM_USERNAME			CONFIG_MODEM_QUECTEL_BG95_M3_USERNAME
#define MDM_PASSWORD			CONFIG_MODEM_QUECTEL_BG95_M3_PASSWORD

#define CONFIG_DNS_RESOLVER

/* Modem ATOI routine. */
#define ATOI(s_, value_, desc_)		modem_atoi(s_, value_, desc_, __func__, 10)
#define ATOI_HEX(s_, value_, desc_)	modem_atoi(s_, value_, desc_, __func__, 16)

struct modem_psm_timers {
	/* 8-bit notation active timer value */
	char active_timer[PSM_TIMER_VALUE_SIZE];
	/* 8-bit notation periodic TAU timer value */
	char tau[PSM_TIMER_VALUE_SIZE];
};

/* driver data */
struct modem_data {
	struct net_if *net_iface;
	uint8_t mac_addr[6];

	/* modem interface */
	struct modem_iface_uart_data iface_data;
	uint8_t iface_rb_buf[MDM_MAX_DATA_LENGTH];

	/* modem cmds */
	struct modem_cmd_handler_data cmd_handler_data;
	uint8_t cmd_match_buf[MDM_RECV_BUF_SIZE + 1];

	/* socket data */
	struct modem_socket_config socket_config;
	struct modem_socket sockets[MDM_MAX_SOCKETS];

	/* RSSI work */
	struct k_work_delayable rssi_query_work;

	/* Modem dynamic data update work */
	struct k_work dynamic_data_update_work;

	/* PSM wakeup work */
	struct k_work psm_wakeup_work;

	/* modem data */
	char mdm_manufacturer[MDM_MANUFACTURER_LENGTH];
	char mdm_model[MDM_MODEL_LENGTH];
	char mdm_revision[MDM_REVISION_LENGTH];
	char mdm_imei[MDM_IMEI_LENGTH];
#if defined(CONFIG_MODEM_QUECTEL_BG95_M3_SIM_NUMBERS)
	char mdm_imsi[MDM_IMSI_LENGTH];
	char mdm_iccid[MDM_ICCID_LENGTH];
#endif /* #if defined(CONFIG_MODEM_QUECTEL_BG95_M3_SIM_NUMBERS) */
	char mdm_time[MDM_TIME_LENGTH];
	int mdm_rssi;
	int mdm_rsrp;
	int mdm_rsrq;

	struct modem_network_data mdm_network;
	struct k_mutex mdm_data_mutex;

	/* bytes written to socket in last transaction */
	int sock_written;

	/* Socket from which we are currently reading data. */
	int sock_fd;
	
	/*  Flag to detect DNS is ready or not */
	struct zsock_addrinfo *dns_ai;
	bool dns_ready;
	bool dns_request;
	int  dns_result;
	int  dns_ip_count;

	/*  Flag to detect if recvfrom/read needs to retry */
	bool recvfrom_ready;

	/* File stattus */
	char file_name[MDM_FILE_NAME_MAX_LENGTH];
	int file_size;

	/* Unread data status */
	int unread_size;

	/* Modem status */
	bool is_connected;

	enum modem_power_state power;

	/* SIM initialization status reported by modem */
	int8_t sim_ini_stat;

	/* Modem API */
	modem_api_evt_handler_t evt_callback;

#if defined(CONFIG_MODEM_QUECTEL_BG95_M3_DYNAMIC_PSK)
	struct modem_psk psk;
#endif

#if IS_ENABLED(CONFIG_MODEM_QUECTEL_BG95_PSM) || IS_ENABLED(CONFIG_MODEM_QUECTEL_BG95_PSM_AUTO)
	struct modem_psm_timers mdm_psm_timers;
#endif
#if IS_ENABLED(CONFIG_MODEM_QUECTEL_BG95_SOFT_PSM) || IS_ENABLED(CONFIG_MODEM_QUECTEL_BG95_PSM_AUTO)
	uint16_t mdm_soft_psm_timeout_s;
#endif
	/* Semaphore(s) */
	struct k_sem sem_response;
	struct k_sem sem_ready;
	struct k_sem sem_tx_ready;
	struct k_sem sem_sock_conn;
	struct k_sem sem_dns_busy;
	struct k_sem sem_dns_ready;
	struct k_sem sem_data_ready;
	struct k_sem sem_shutdown;
	struct k_sem sem_busy;
	struct k_sem sem_ntp_ready;
};

/* Socket read callback data */
struct socket_read_data {
	char		 *recv_buf;
	size_t		 recv_buf_len;
	struct sockaddr	 *recv_addr;
	uint16_t	 recv_read_len;
};


#endif /* QUECTEL_BG95_H */

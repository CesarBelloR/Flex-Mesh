#define DT_DRV_COMPAT quectel_bg95

#include <fcntl.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(modem_quectel_bg95, CONFIG_MODEM_LOG_LEVEL);

#include "quectel-bg95.h"
#include "certificates.h"

#ifdef CONFIG_PM_DEVICE
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#endif

#define PSM_TIMER_VAL_LEN	sizeof("00000011")

#define MDM_TCP_ERROR_NO_MEMORY		553
#define MDM_TCP_ERROR_TIMEOUT		569
#define MDM_TCP_ERROR_SOCKET_IN_USE	563
#define MDM_TCP_ERROR_UNKNOWN		550

static struct k_thread	       modem_rx_thread;
static struct k_work_q	       modem_workq;
static struct modem_data       mdata;
static struct modem_context    mctx;
static const struct socket_op_vtable offload_socket_fd_op_vtable;

#if defined(CONFIG_DNS_RESOLVER)
#define AI_ARR_MAX 1
#endif

#define MODEM_SUBMIT_EVT(evt_type)	const struct modem_api_evt mdm_evt = { 	\
					.type = evt_type 			\
				};						\
				modem_event_callback(&mdm_evt);

#if defined(CONFIG_MODEM_QUECTEL_BG95_PSM)
struct psm_ind {
	struct k_work_delayable work;
	/* 1 rising, 0 falling */
	int edge;
	int64_t last_change_s;
} psm_ind;

#define PSM_IND_DEBOUNCE_INTERVAL_MS	250
#endif

static K_KERNEL_STACK_DEFINE(modem_rx_stack, CONFIG_MODEM_QUECTEL_BG95_M3_RX_STACK_SIZE);
static K_KERNEL_STACK_DEFINE(modem_workq_stack, CONFIG_MODEM_QUECTEL_BG95_M3_RX_WORKQ_STACK_SIZE);
NET_BUF_POOL_DEFINE(mdm_recv_pool, MDM_RECV_MAX_BUF, MDM_RECV_BUF_SIZE, 0, NULL);

static const struct gpio_dt_spec power_gpio = GPIO_DT_SPEC_INST_GET(0, mdm_power_gpios);
#if DT_INST_NODE_HAS_PROP(0, mdm_on_off_gpios)
static const struct gpio_dt_spec on_off_gpio = GPIO_DT_SPEC_INST_GET(0, mdm_on_off_gpios);
#endif
#if DT_INST_NODE_HAS_PROP(0, mdm_pon_trig_gpios)
static const struct gpio_dt_spec pon_trig_gpio = GPIO_DT_SPEC_INST_GET(0, mdm_pon_trig_gpios);
#endif
#if DT_INST_NODE_HAS_PROP(0, mdm_uart_oe_gpios)
static const struct gpio_dt_spec uart_oe_gpio = GPIO_DT_SPEC_INST_GET(0, mdm_uart_oe_gpios);
#endif
#if DT_INST_NODE_HAS_PROP(0, mdm_reset_gpios)
static const struct gpio_dt_spec reset_gpio = GPIO_DT_SPEC_INST_GET(0, mdm_reset_gpios);
#endif
#if DT_INST_NODE_HAS_PROP(0, mdm_dtr_gpios)
static const struct gpio_dt_spec dtr_gpio = GPIO_DT_SPEC_INST_GET(0, mdm_dtr_gpios);
#endif
#if DT_INST_NODE_HAS_PROP(0, mdm_wdisable_gpios)
static const struct gpio_dt_spec wdisable_gpio = GPIO_DT_SPEC_INST_GET(0, mdm_wdisable_gpios);
#endif

#if defined(CONFIG_MODEM_QUECTEL_BG95_PSM)
/* GPIO mdm_psm_ind_gpios indicates the modem's PSM status. 
 * Pin active: modem is in PSM (or off) 
 * Pin inactive: modem is on */
static const struct gpio_dt_spec psm_ind_gpio = GPIO_DT_SPEC_INST_GET(0, mdm_psm_ind_gpios);
#if 0 // Uncomment when use
static char psm_param_rat[PSM_TIMER_VAL_LEN] = CONFIG_MODEM_QUECTEL_BG95_M3_PSM_REQ_RAT;
static char psm_param_rptau[PSM_TIMER_VAL_LEN] = CONFIG_MODEM_QUECTEL_BG95_M3_PSM_REQ_RPTAU;
#endif 
#endif

static void quectel_bg95_set_connected(bool connected);
static int modem_event_callback(const struct modem_api_evt *evt);
int quectel_bg95_psm_wakeup(void);
/* Implementation in net/ip/utils.h */
extern char *net_byte_to_hex(char *ptr, uint8_t byte, char base, bool pad);

static inline int digits(int n)
{
	int count = 0;

	while (n != 0) {
		n /= 10;
		++count;
	}

	return count;
}

static inline uint32_t hash32(char *str, int len)
{
#define HASH_MULTIPLIER		37

	uint32_t h = 0;
	int i;

	for (i = 0; i < len; ++i) {
		h = (h * HASH_MULTIPLIER) + str[i];
	}

	return h;
}

static inline uint8_t *modem_get_mac(const struct device *dev)
{
	struct modem_data *data = dev->data;
	uint32_t hash_value;

	data->mac_addr[0] = 0x00;
	data->mac_addr[1] = 0x10;

	/* use IMEI for mac_addr */
	hash_value = hash32(mdata.mdm_imei, strlen(mdata.mdm_imei));

	UNALIGNED_PUT(hash_value, (uint32_t *)(data->mac_addr + 2));

	return data->mac_addr;
}

/* Func: modem_atoi
 * Desc: Convert string to long integer, but handle errors
 */
static int modem_atoi(const char *s, const int err_value,
		      const char *desc, const char *func, int base)
{
	int   ret;
	char  *endptr;

	ret = (int)strtol(s, &endptr, base);
	if (!endptr || (*endptr != '\0' && *endptr != '\"')) {
		LOG_ERR("bad %s '%s' in %s", s, desc,
			func);
		return err_value;
	}

	return ret;
}

static inline int find_len(char *data)
{
	char buf[10] = {0};
	int  i;

	for (i = 0; i < 10; i++) {
		if (data[i] == '\r')
			break;

		buf[i] = data[i];
	}

	return ATOI(buf, 0, "rx_buf");
}

enum t3412_inc {
	T3412_10_MIN = 0,
	T3412_1_HR,
	T3412_10_HR,
	T3412_2_SEC,
	T3412_30_SEC,
	T3412_1_MIN
};

enum t3324_inc {
	T3324_2_SEC = 0,
	T3324_1_MIN,
	T3324_DECI_HR,
	T3324_DISABLED
};

static int timer_val_parse(char *buf, uint8_t buf_len, uint16_t *at_val,
			    uint8_t *at_increment) {
	__ASSERT_NO_MSG(buf != NULL);
	__ASSERT_NO_MSG(at_val != NULL);	
	__ASSERT_NO_MSG(at_increment != NULL);

	/* 8 digits (bits) + 2x '"' */
	if (buf_len != 10) {
		// exit early
		return -1;
	}
	*at_val = 0;

	/* Determine increment binary value */
	*at_increment = (buf[1] - '0') << 2 | (buf[2] - '0') << 1 | (buf[3] - '0');
	/* Determine raw timer value */
	for (int i = 4; i < 9; i++) {
		*at_val |= (buf[i] - '0') << (8 - i);
	}

	return 0;
}

static inline uint16_t tau_to_seconds(char *buf, uint8_t buf_len)
{
	uint32_t at_val = 0;
	uint8_t at_increment;

	if (timer_val_parse(buf, buf_len, (uint16_t *)&at_val, &at_increment) != 0) {
		return 0;
	}	
	
	switch (at_increment) {
		case T3412_10_MIN:
			at_val *= (10 * 60);
			break;

		case T3412_1_HR:
			at_val *= (60 * 60);
			break;

		case T3412_10_HR:
			at_val *= (10 * 60 * 60);
			break;

		case T3412_2_SEC:
			at_val *= 2;
			break;

		case T3412_30_SEC:
			at_val *= 30;
			break;

		case T3412_1_MIN:
			at_val *= 60;
			break;

		default:
			at_val = 0;
	}

	return at_val;
}

/**
 * Convert a T3324 active timer value to seconds.
 * 
 * @return 0: fail (0 is not a valid timer value)
 *         >0: success
*/
static inline uint16_t active_time_to_seconds(char *buf, uint8_t buf_len)
{
	uint16_t at_val = 0;
	uint8_t at_increment;

	if (timer_val_parse(buf, buf_len, &at_val, &at_increment) != 0) {
		return 0;
	}

	switch (at_increment) {
		case T3324_2_SEC:
			at_val *= 2;
			break;
			
		case T3324_1_MIN:
			at_val *= 60;
			break;

		case T3324_DECI_HR:
			at_val *= (6 * 60);
			break;

		case T3324_DISABLED:
		default:
			at_val = 0;
	}

	return at_val;
}

static inline int parse_oper(char *buf, uint8_t buf_len, uint8_t format)
{
	if ((format != 2) || 
	    (buf_len < (sizeof("\"#####\"") - 1))) {
		LOG_WRN("Incorrect format: %u", format);
		return -1;
	}

	/* Limit MCC to first 3 digits: "### */
	char tmp = buf[4];
	buf[4] = '\0';
	mdata.mdm_network.mcc = ATOI(&buf[1], 0, "MCC");

	/* Parse MNC (can be two or 3 digits) */
	buf[4] = tmp;
	mdata.mdm_network.mnc = ATOI(&buf[4], 0, "MNC");

	return 0;
}


/* Func: on_cmd_sockread_common
 * Desc: Function to successfully read data from the modem on a given socket.
 */
static int on_cmd_sockread_common(int socket_fd,
				  struct modem_cmd_handler_data *data,
				  int socket_data_length,
				  uint16_t len)
{
	struct modem_socket	 *sock = NULL;
	struct socket_read_data	 *sock_data;
	int ret, i;
	int bytes_to_skip;
	char *skipto;

	if (!len) {
		LOG_ERR("Invalid length, Aborting!");
		return -EAGAIN;
	}

	/* Make sure we still have buf data */
	if (!data->rx_buf) {
		LOG_ERR("Incorrect format! Ignoring data!");
		return -EINVAL;
	}
	
	/* zero length */
	if (socket_data_length <= 0) {
		LOG_ERR("Length problem (%d).  Aborting!", socket_data_length);
		return -EAGAIN;
	}

	/* check to make sure we have all of the data. */
	if (net_buf_frags_len(data->rx_buf) < (socket_data_length + 2 + 4)) {
		return -EAGAIN;
	}

	/* See how many characters we need to skip.
	*  Modem sends: +####: <length>\r\n<data>
	*  We need to skip <length>\r\n
	*/
	skipto = memchr((void *)data->rx_buf->data, (int)'\n',
			data->rx_buf->len);
	bytes_to_skip = (skipto - (char *)data->rx_buf->data) + 1;
	for (i = 0; i < bytes_to_skip; i++) {
		net_buf_pull_u8(data->rx_buf);
	}

	if (!data->rx_buf->len) {
		data->rx_buf = net_buf_frag_del(NULL, data->rx_buf);
	}

	sock = modem_socket_from_fd(&mdata.socket_config, socket_fd);
	if (!sock) {
		LOG_ERR("Socket not found! (%d)", socket_fd);
		ret = -EINVAL;
		goto exit;
	}

	sock_data = (struct socket_read_data *)sock->data;
	if (!sock_data) {
		LOG_ERR("Socket data not found! Skip handling (%d)", socket_fd);
		ret = -EINVAL;
		goto exit;
	}

	ret = net_buf_linearize(sock_data->recv_buf, sock_data->recv_buf_len,
				data->rx_buf, 0, (uint16_t)socket_data_length);
	data->rx_buf = net_buf_skip(data->rx_buf, ret);
	sock_data->recv_read_len = ret;
	if (ret != socket_data_length) {
		LOG_ERR("Total copied data is different then received data!"
			" copied:%d vs. received:%d", ret, socket_data_length);
		ret = -EINVAL;
	}

exit:
	/* remove packet from list (ignore errors) */
	(void)modem_socket_packet_size_update(&mdata.socket_config, sock,
					      -socket_data_length);

	/* don't give back semaphore -- OK to follow */
	return ret;
}

/* Func: socket_close
 * Desc: Function to close the given socket descriptor.
 */
static void socket_close(struct modem_socket *sock, bool force_close)
{
	char buf[sizeof("AT+Q###CLOSE=##")] = {0};
	int  ret = 0;
	if ((sock->ip_proto == IPPROTO_TLS_1_2) || (sock->ip_proto == IPPROTO_DTLS_1_2)) {
		snprintk(buf, sizeof(buf), "AT+QSSLCLOSE=%d", sock->id);
	} else {
		snprintk(buf, sizeof(buf), "AT+QICLOSE=%d", sock->id);
	}
	
	k_sem_reset(&mdata.sem_response);
	/* Tell the modem to close the socket, if connected */
	if (sock->is_connected || force_close) {
		ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler,
				NULL, 0U, buf,
				&mdata.sem_response, MDM_CMD_TIMEOUT);
		if (ret < 0) {
			LOG_ERR("%s ret:%d", buf, ret);
		}
	}

	if (ret == 0) {
		modem_socket_put(&mdata.socket_config, sock->sock_fd);
	}
}

/* Handler: OK */
MODEM_CMD_DEFINE(on_cmd_ok)
{
	modem_cmd_handler_set_error(data, 0);
	k_sem_give(&mdata.sem_response);
	return 0;
}

/* Handler: ERROR */
MODEM_CMD_DEFINE(on_cmd_error)
{
	modem_cmd_handler_set_error(data, -EIO);
	k_sem_give(&mdata.sem_response);
	return 0;
}

/* Handler: +CME Error: <err>[0] */
MODEM_CMD_DEFINE(on_cmd_exterror)
{
	modem_cmd_handler_set_error(data, -EIO);
	k_sem_give(&mdata.sem_response);
	return 0;
}

/* Handler: +CSQ: <signal_power>[0], <qual>[1] */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_rssi_csq)
{
	int rssi = ATOI(argv[0], 0, "signal_power");
	int qual = ATOI(argv[1], 0, "qual");

	/* Check the RSSI value. */
	if (rssi == 31) {
		mdata.mdm_rssi = -51;
	} else if (rssi >= 0 && rssi <= 31) {
		mdata.mdm_rssi = -114 + ((rssi * 2) + 1);
	} else {
		mdata.mdm_rssi = MDM_RSSI_INVALID;
	}
	mdata.mdm_qual = qual;

	LOG_INF("RSSI: %d, qual: %d", mdata.mdm_rssi, mdata.mdm_qual);

	return 0;
}

/* Handler: +QIOPEN: <connect_id>[0], <err>[1] */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_sockopen)
{
	int err = ATOI(argv[1], 0, "sock_err");

	LOG_INF("Status of open socket: %d", err);
	modem_cmd_handler_set_error(data, err);
	k_sem_give(&mdata.sem_sock_conn);

	return 0;
}

/* Handler: +QSSLOPEN: <connect_id>[0], <err>[1] */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_sslopen)
{
	int err = ATOI(argv[1], 0, "sock_err");

	LOG_INF("Status of open TLS socket: %d", err);
	modem_cmd_handler_set_error(data, err);
	k_sem_give(&mdata.sem_sock_conn);

	return 0;
}

/* Handler: <manufacturer> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_manufacturer)
{
	size_t out_len = net_buf_linearize(mdata.mdm_manufacturer,
					   sizeof(mdata.mdm_manufacturer) - 1,
					   data->rx_buf, 0, len);
	mdata.mdm_manufacturer[out_len] = '\0';
	LOG_INF("Manufacturer: %s", mdata.mdm_manufacturer);
	return 0;
}

/* Handler: <model> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_model)
{
	size_t out_len = net_buf_linearize(mdata.mdm_model,
					   sizeof(mdata.mdm_model) - 1,
					   data->rx_buf, 0, len);
	mdata.mdm_model[out_len] = '\0';

	/* Log the received information. */
	LOG_INF("Model: %s", mdata.mdm_model);
	return 0;
}

/* Handler: <rev> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_revision)
{
	size_t out_len = net_buf_linearize(mdata.mdm_revision,
					   sizeof(mdata.mdm_revision) - 1,
					   data->rx_buf, 0, len);
	mdata.mdm_revision[out_len] = '\0';

	/* Log the received information. */
	LOG_INF("Revision: %s", mdata.mdm_revision);
	return 0;
}

/* Handler: <IMEI> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_imei)
{
	size_t out_len = net_buf_linearize(mdata.mdm_imei,
					   sizeof(mdata.mdm_imei) - 1,
					   data->rx_buf, 0, len);
	mdata.mdm_imei[out_len] = '\0';

	/* Log the received information. */
	LOG_INF("IMEI: %s", mdata.mdm_imei);
	return 0;
}

#if defined(CONFIG_MODEM_QUECTEL_BG95_M3_SIM_NUMBERS)
/* Handler: <IMSI> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_imsi)
{
	size_t	out_len = net_buf_linearize(mdata.mdm_imsi,
					    sizeof(mdata.mdm_imsi) - 1,
					    data->rx_buf, 0, len);
	mdata.mdm_imsi[out_len] = '\0';

	/* Log the received information. */
	LOG_INF("IMSI: %s", mdata.mdm_imsi);
	return 0;
}

/* Handler: <ICCID> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_iccid)
{
	size_t out_len;
	char iccid_buf[32];
	char   *p;

	out_len = net_buf_linearize(iccid_buf, sizeof(iccid_buf) - 1,
				    data->rx_buf, 0, len);
	iccid_buf[out_len] = '\0';

	/* Skip over the +CCID bit, which modems omit. */
	if (iccid_buf[0] == '+') {
		p = strchr(iccid_buf, ' ');
		if (p) {
			out_len = strlen(p + 1);
			if (out_len < sizeof(mdata.mdm_iccid)) {
				memcpy(mdata.mdm_iccid, p + 1, out_len);
				mdata.mdm_iccid[out_len] = '\0';
			}
		}
	} else {
		if (out_len < sizeof(mdata.mdm_iccid)) {
			memcpy(mdata.mdm_iccid, iccid_buf, out_len);
			mdata.mdm_iccid[out_len] = '\0';
		}
	}

	LOG_INF("ICCID: %s", mdata.mdm_iccid);
	return 0;
}
#endif /* #if defined(CONFIG_MODEM_QUECTEL_BG95_M3_SIM_NUMBERS) */

/* Handler: TX Ready */
MODEM_CMD_DIRECT_DEFINE(on_cmd_tx_ready)
{
	k_sem_give(&mdata.sem_tx_ready);
	return len;
}

/* Handler: SEND OK */
MODEM_CMD_DEFINE(on_cmd_send_ok)
{
	modem_cmd_handler_set_error(data, 0);
	k_sem_give(&mdata.sem_response);

	return 0;
}

/* Handler: SEND FAIL */
MODEM_CMD_DEFINE(on_cmd_send_fail)
{
	mdata.sock_written = 0;
	modem_cmd_handler_set_error(data, -EIO);
	k_sem_give(&mdata.sem_response);

	return 0;
}

/* Handler: Read data +QIRD: <length>[0] OR +QSSLRECV: <length>[0] */
MODEM_CMD_DEFINE(on_cmd_sock_readdata)
{
	return on_cmd_sockread_common(mdata.sock_fd, data, 
						ATOI(argv[0], 0, "length"), len);
}

/* Handler: Read data size +QIRD: <length>[0] OR +QSSLRECV: <length>[0] */
MODEM_CMD_DEFINE(on_cmd_sock_getdatasize)
{
	int received, read, unread;

	received = ATOI(argv[0], 0, "recvd");
	read = ATOI(argv[1], 0, "read");
	unread = ATOI(argv[2], 0, "unread");
	LOG_DBG("recvd %d, read %d, unread %d", received, read, unread);
	mdata.unread_size = unread;

	return 0;
}

/* Handler: Read data size +QIGETERROR: <result>[0], <description>[1] */
MODEM_CMD_DEFINE(on_cmd_tcp_geterror)
{
	int error;

	error = ATOI(argv[0], 0, "error_code");

	LOG_INF("TCP error %d: %s", error, argv[1]);
	modem_cmd_handler_set_error(data, error);

	return 0;
}

#if CONFIG_MODEM_QUECTEL_BG95_PSM
static int pm_suspend_uart(void) 
{
	int ret;

#if DT_INST_NODE_HAS_PROP(0, mdm_uart_oe_gpios)
	gpio_pin_set_dt(&uart_oe_gpio, GPIO_OUTPUT_INACTIVE);
#endif
	uart_irq_rx_disable(mctx.iface.dev);
	uart_irq_tx_disable(mctx.iface.dev);
	// uart doesn't have a shutdown mode only suspend
	ret = pm_device_action_run(mctx.iface.dev, PM_DEVICE_ACTION_SUSPEND);
	if (ret)
	{
		LOG_ERR("Can't suspend device: %d", ret);
		return ret;
	}

	LOG_DBG("UART suspended");
	
	return 0;
}

static int pm_resume_uart(void)
{
	int ret;

#if DT_INST_NODE_HAS_PROP(0, mdm_uart_oe_gpios)
	gpio_pin_set_dt(&uart_oe_gpio, GPIO_OUTPUT_ACTIVE);
#endif
	uart_irq_rx_enable(mctx.iface.dev);
	ret = pm_device_action_run(mctx.iface.dev, PM_DEVICE_ACTION_RESUME);
	__ASSERT_NO_MSG((ret == 0) || (ret == -EALREADY));
	if (ret)
	{
		LOG_ERR("Can't resume device: %d", ret);
		return ret;
	}

	LOG_DBG("UART resumed");

	return 0;
}

/**
 * @brief Handler for the PSM_IND modem signal. It signals a wakeup from or
 * entering of PSM.
 * The modem changes the PSM_IND's pin status before sending APP RDY.
 * When entering PSM, there are two high -> low transitions with pulses of
 * ~50 ms. This is handled through a "debounce" mechanism.
 * When this handler is called, save the current state of the pin and time
 * and resume UART if needed. If last change was within the debouncing interval,
 * (re-)schedule work to modem work queue.
*/
static void psm_ind_handler(struct psm_ind *psm_ind_data, int edge)
{
	int ret;
	int64_t uptime_now = k_uptime_get();

	psm_ind_data->edge = edge;
	if (psm_ind_data->last_change_s != 0 && 
	    (uptime_now - psm_ind_data->last_change_s) < PSM_IND_DEBOUNCE_INTERVAL_MS) {
		psm_ind_data->last_change_s = uptime_now;
		k_work_reschedule_for_queue(&modem_workq, &psm_ind_data->work, 
					    K_MSEC(PSM_IND_DEBOUNCE_INTERVAL_MS));
		return;
	}
	psm_ind_data->last_change_s = uptime_now;

	/* If psm.edge is a negative value something went wrong and we can't
	 * guarantee correct modem operation anymore. */
	__ASSERT_NO_MSG(psm_ind_data->edge >= 0);
	if (psm_ind_data->edge == 0) {
		pm_resume_uart();
	}
}

/**
 * @brief Wake up modem UART if pin edge/level indicates a PSM wake up.
*/
static void psm_ind_work_fn(struct k_work *work)
{	
	struct k_work_delayable *work_delayable = k_work_delayable_from_work(work);
	struct psm_ind *psm_ind_data = CONTAINER_OF(work_delayable, struct psm_ind, work);

	/* If psm.edge is a negative value something went wrong and we can't
	 * guarantee correct modem operation anymore. */
	__ASSERT_NO_MSG(psm_ind_data->edge >= 0);
	if (psm_ind_data->edge == 0) {
		pm_resume_uart();
	}
}

static void psm_ind_callback(const struct device *dev,
			     struct gpio_callback *cb, uint32_t pins)
{
	/* We need to wake up the UART when turning on as soon as possible to
	   not miss the modem's APP RDY output. Task in work queue could be waiting
	   for semaphore that might time out. */
	psm_ind_handler(&psm_ind, gpio_pin_get(dev, find_msb_set(pins) - 1));
}
static struct gpio_callback psm_ind_gpio_callback;

static inline int disable_psm_ind_interrupt()
{
	int ret;

	ret = gpio_pin_interrupt_configure_dt(&psm_ind_gpio, GPIO_INT_DISABLE);

	return ret;
}

static inline int enable_psm_ind_interrupt()
{
	int ret;

	ret = gpio_pin_interrupt_configure_dt(&psm_ind_gpio, GPIO_INT_EDGE_BOTH);
	__ASSERT_NO_MSG(ret == 0);

	return ret;
}
#endif

static int setup_psm_ind_interrupt()
{
#if CONFIG_MODEM_QUECTEL_BG95_PSM
	int ret;

	ret = gpio_pin_configure_dt(&psm_ind_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s pin", "psm_ind");
		return ret;
	}

	gpio_init_callback(&psm_ind_gpio_callback, psm_ind_callback,
			   BIT(psm_ind_gpio.pin));
	ret = gpio_add_callback(psm_ind_gpio.port, &psm_ind_gpio_callback);
	if (ret < 0) {
		LOG_ERR("Failed to set gpio callback!");
		return ret;
	}

	k_work_init_delayable(&psm_ind.work, psm_ind_work_fn);

	return ret;
#endif
	return -ENOTSUP;
}

MODEM_CMD_DEFINE(on_cmd_unsol_qpsmtimer)
{
	uint32_t tau;
	uint32_t active_timer;

	tau = ATOI(argv[0], 0, "tau");
	active_timer = ATOI(argv[1], 0, "active_timer");

	LOG_INF("Entering PSM. TAU: %u, AT: %u", tau, active_timer);

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_unsol_cereg)
{
	struct modem_data *mdm_data = CONTAINER_OF(data, struct modem_data, 
							 cmd_handler_data);
	struct modem_network_data *nw_data = &mdm_data->mdm_network;
	int ret;

	ret = k_mutex_lock(&mdm_data->mdm_network_mutex, MDM_CMD_TIMEOUT);
	__ASSERT_NO_MSG(ret == 0);
	if (ret != 0) {
		return -1;
	}
	nw_data->stat = ATOI(argv[0], STAT_NOT_REGISTERED, "stat");
	if (argc >= 4) {
		/* Hex values start with a '"'. Skip this. "*/
		if (strlen(argv[1]) > 0) {
			nw_data->tac = ATOI_HEX(argv[1] + 1, 0, "tac");
		}
		if (strlen(argv[2]) > 0) {
			nw_data->cell_id = ATOI_HEX(argv[2] + 1, 0, "ci");
		}
		nw_data->act = ATOI(argv[3], 0, "AcT");
	}

	/* Copy PSM active timer and periodic TAU values */
	if (argc >= 8) {
		nw_data->active_time_s = active_time_to_seconds(argv[6], strlen(argv[6]));

		nw_data->periodic_tau_s = tau_to_seconds(argv[7], strlen(argv[7]));
	}
	k_mutex_unlock(&mdm_data->mdm_network_mutex);
	LOG_INF("Status: %u, tac: %u, ci: %u, AcT: %u, AT: %u, TAU: %u", 
		nw_data->stat, nw_data->tac, nw_data->cell_id, nw_data->act,
		nw_data->active_time_s, nw_data->periodic_tau_s);
	
	if ((nw_data->stat == STAT_REGISTERED_HOME) || 
	    (nw_data->stat == STAT_REGISTERED_ROAMING)) {
		LOG_INF("Network connected");
		k_work_submit_to_queue(&modem_workq, &mdata.dynamic_data_update_work);
	} else {
		LOG_INF("Network disconnected.");
	}

	return 0;
}

enum cops_error {
	COPS_OKAY = 0,
	COPS_WRONG_FORMAT,
	COPS_ERROR
};

/**
 * Process a COPS command response.
 * 
 * @return 0: success
 * 	   -
*/
MODEM_CMD_DEFINE(on_cmd_cops)
{
	struct modem_data *mdm_data = CONTAINER_OF(data, struct modem_data, 
							 cmd_handler_data);
	struct modem_network_data *nw_data = &mdm_data->mdm_network;
	uint8_t format = 0;
	int ret;

	if (argc > 1) {
		format = ATOI(argv[1], 0, "fmt");
	}

	if (format != 2) {
		modem_cmd_handler_set_error(&mdata.cmd_handler_data, COPS_WRONG_FORMAT);
		return COPS_WRONG_FORMAT;
	}

	ret = k_mutex_lock(&mdm_data->mdm_network_mutex, MDM_CMD_TIMEOUT);
	__ASSERT_NO_MSG(ret == 0);
	if (ret != 0) {
		return -1;
	}
	
	if (argc >= 3) {
		parse_oper(argv[2], strlen(argv[2]), format);
	} else {
		LOG_WRN("COPS: not enough args");
	}
	k_mutex_unlock(&mdm_data->mdm_network_mutex);

	LOG_INF("MCC: %u, MNC: %u", nw_data->mcc, nw_data->mnc);
	
	ret = modem_cmd_handler_set_error(&mdata.cmd_handler_data, COPS_OKAY);
	return COPS_OKAY;
}

/* Func: get_data_size
 * Desc: This function will retrieve the size of the
 * data available on the socket object.
 */
static ssize_t get_data_size(struct modem_socket *sock)
{
	char   sendbuf[sizeof("AT+Q###RECV=##,####")] = {0};
	int    ret;
	struct socket_read_data sock_data;
	/* Modem command to read the data. */
	struct modem_cmd cmd[] = {
		MODEM_CMD("+QIRD: ", on_cmd_sock_getdatasize, 3U, ","),
		MODEM_CMD("+QSSLRECV: ", on_cmd_sock_getdatasize, 3U, ",") };

	if ((sock->ip_proto == IPPROTO_TLS_1_2) || 
		(sock->ip_proto == IPPROTO_DTLS_1_2)) {
		snprintk(sendbuf, sizeof(sendbuf), "AT+QSSLRECV=%d,%zd", 
				sock->id, 0);
	} else {
		snprintk(sendbuf, sizeof(sendbuf), "AT+QIRD=%d,%zd", 
				sock->id, 0);
	}

	/* Socket read settings */
	(void) memset(&sock_data, 0, sizeof(sock_data));
	// sock->data	       = &sock_data;
	// mdata.sock_fd	   = sock->id;
	/* Tell the modem to give us the available data's length */
	/* (AT+QIRD=sock_fd,0). */
	k_sem_reset(&mdata.sem_response);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler,
			     cmd, ARRAY_SIZE(cmd), sendbuf, &mdata.sem_response,
			     MDM_RECV_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Could not retrieve recv buffer size");
		errno = -ret;
		ret = -1;
	} else {
		ret = mdata.unread_size;
	}

	return ret;
}

/* Handler: Data receive indication. */
MODEM_CMD_DEFINE(on_cmd_unsol_recv)
{
	struct modem_socket *sock;
	int		     sock_fd;

	sock_fd = ATOI(argv[0], 0, "sock_fd");

	/* Socket pointer from FD. */
	sock = modem_socket_from_fd(&mdata.socket_config, sock_fd);
	if (!sock) {
		return 0;
	}

	int ret = modem_socket_packet_size_update(&mdata.socket_config, sock, 1);
	if (ret < 0) {
		LOG_ERR("socket_id:%d err: %d", sock_fd, ret);
	}

	/* Data ready indication. */
	LOG_DBG("Data Receive Indication for socket: %d", sock_fd);
	modem_socket_data_ready(&mdata.socket_config, sock);

	return 0;
}

/* Handler: Socket Close Indication. */
MODEM_CMD_DEFINE(on_cmd_unsol_close)
{
	struct modem_socket *sock;
	int		     sock_fd;

	sock_fd = ATOI(argv[0], 0, "sock_fd");
	sock	= modem_socket_from_fd(&mdata.socket_config, sock_fd);
	if (!sock) {
		return 0;
	}

	LOG_INF("Socket Close Indication for socket: %d", sock_fd);

	/* Tell the modem to close the socket. */
	socket_close(sock, false);
	LOG_INF("Socket Closed: %d", sock_fd);
	return 0;
}

MODEM_CMD_DEFINE(on_cmd_unsol_pdpdeact)
{
	quectel_bg95_set_connected(false);

	if (mdata.power == MODEM_POWER_ON) {
		k_work_reschedule_for_queue(&modem_workq,
					&mdata.rssi_query_work,
					MDM_PDPDEACT_RECONNECT_DELAY);
	}

	return 0;
}

/* Handler: Modem initialization ready. */
MODEM_CMD_DEFINE(on_cmd_unsol_rdy)
{
	if (mdata.power != MODEM_POWER_PSM) {
		k_sem_give(&mdata.sem_ready);
		return 0;
	}

	k_work_submit_to_queue(&modem_workq, &mdata.psm_wakeup_work);
	
	return 0;
}

static int on_dns_parser_ip_count(uint8_t* dns_buffer, int length) {
	(void)length;
	char* token = strtok(dns_buffer, ",");
	if (NULL == token) {
		LOG_ERR("Can't get error code");
		return -1;
	}

	int error_code = atoi(token);
	LOG_DBG("Error DNS code %d", error_code);

	token = strtok(NULL, ",");
	if (NULL == token) {
		LOG_ERR("Can't get number of IP return");
		return -1;
	}

	return (int)atoi(token);
}

#if defined(CONFIG_DNS_RESOLVER)
/* Handler: +QIURC: "dnsgip","<resolved_ip_address>"[0] */
MODEM_CMD_DEFINE(on_cmd_dns)
{
	struct zsock_addrinfo *result;
	if (!mdata.dns_request) {
		return 0;
	};

	uint8_t dns_buffer[128];
	size_t out_len = net_buf_linearize(dns_buffer, sizeof(dns_buffer) - 1, data->rx_buf, 0, len);

	if (!mdata.dns_ready) {
		mdata.dns_ip_count = 0;
		mdata.dns_result = on_dns_parser_ip_count(dns_buffer, out_len);
		LOG_DBG("Number of dns result: %d", mdata.dns_result);
		mdata.dns_ready = true;
		return 0;
	}

	mdata.dns_ip_count += 1;

	if (mdata.dns_ip_count == 1) {
		result = mdata.dns_ai;
		/* chop off end quote */
		dns_buffer[out_len - 1] = '\0';

		result->_ai_addr.sa_family = AF_INET;
		/* skip beginning quote when parsing */
		(void)net_addr_pton(result->ai_family, &dns_buffer[1],
					&((struct sockaddr_in *)&result->_ai_addr)->sin_addr);
	}


	if (mdata.dns_ip_count == mdata.dns_result) {
		k_sem_give(&mdata.sem_dns_ready);
	}
	
	return 0;
}
#endif


static int get_tcp_error(struct modem_socket *sock)
{
	char   sendbuf[] = "AT+QIGETERROR";
	int    ret;
	/* Modem command to read the data. */
	struct modem_cmd cmd[] = {
		MODEM_CMD("+QIGETERROR: ", on_cmd_tcp_geterror, 2U, ","),
	};

	k_sem_reset(&mdata.sem_response);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler,
			     cmd, ARRAY_SIZE(cmd), sendbuf, &mdata.sem_response,
			     MDM_RECV_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Could not retrieve error details");
		errno = -ret;
		ret = -1;
	} else {
		ret = modem_cmd_handler_get_error(mctx.cmd_handler.cmd_handler_data);
	}

	return ret;
}

/* Func: send_socket_data
 * Desc: This function will send "binary" data over the socket object.
 */
static ssize_t send_socket_data(struct modem_socket *sock,
				const struct sockaddr *dst_addr,
				struct modem_cmd *handler_cmds,
				size_t handler_cmds_len,
				const char *buf, size_t buf_len,
				k_timeout_t timeout)
{
	int  ret;
	char send_buf[sizeof("AT+Q###SEND=##,####,")] = {0};
	int bytes_written;

	if (buf_len > MDM_MAX_DATA_LENGTH) {
		buf_len = MDM_MAX_DATA_LENGTH;
	}

	/* Setup the locks correctly. */
	k_sem_take(&mdata.cmd_handler_data.sem_tx_lock, K_FOREVER);
	k_sem_reset(&mdata.sem_tx_ready);

	/* Create a buffer with the correct params. */
	mdata.sock_written = buf_len;
	if ((sock->ip_proto == IPPROTO_TLS_1_2) || (sock->ip_proto == IPPROTO_DTLS_1_2)) {
		snprintk(send_buf, sizeof(send_buf), "AT+QSSLSEND=%d,%ld", sock->id, (long)buf_len);
	} else {
		snprintk(send_buf, sizeof(send_buf), "AT+QISEND=%d,%ld", sock->id, (long)buf_len);
	}

	/* Send the Modem command. */
	ret = modem_cmd_send_nolock(&mctx.iface, &mctx.cmd_handler,
				    NULL, 0U, send_buf, NULL, K_NO_WAIT);
	if (ret < 0) {
		goto exit;
	}

	/* set command handlers */
	ret = modem_cmd_handler_update_cmds(&mdata.cmd_handler_data,
					    handler_cmds, handler_cmds_len,
					    true);
	if (ret < 0) {
		goto exit;
	}

	/* Wait for '>' */
	ret = k_sem_take(&mdata.sem_tx_ready, K_MSEC(5000));
	if (ret < 0) {
		/* Didn't get the data prompt - Exit. */
		LOG_DBG("Timeout waiting for tx");
		goto exit;
	}

	/* Write all data on the console. Do not send CTRL+Z, as we are */
	/* in fixed length mode. */
	mctx.iface.write(&mctx.iface, buf, buf_len);
	LOG_HEXDUMP_DBG(buf, buf_len, "SEND");
	/* Wait for 'SEND OK' or 'SEND FAIL' */
	k_sem_reset(&mdata.sem_response);
	ret = k_sem_take(&mdata.sem_response, timeout);
	if (ret < 0) {
		LOG_DBG("No send response");
		goto exit;
	}

	ret = modem_cmd_handler_get_error(&mdata.cmd_handler_data);
	if (ret != 0) {
		LOG_DBG("Failed to send data");
	}

exit:
	/* unset handler commands and ignore any errors */
	(void)modem_cmd_handler_update_cmds(&mdata.cmd_handler_data,
					    NULL, 0U, false);
	bytes_written = mdata.sock_written;
	k_sem_give(&mdata.cmd_handler_data.sem_tx_lock);

	if (ret < 0) {
		return ret;
	}

	/* Return the amount of data written on the socket. */
	return bytes_written;
}

/* Func: offload_sendto
 * Desc: This function will send data on the socket object.
 */
static ssize_t offload_sendto(void *obj, const void *buf, size_t len,
			      int flags, const struct sockaddr *to,
			      socklen_t tolen)
{
	int ret;
	struct modem_socket *sock = (struct modem_socket *) obj;

	/* Here's how sending data works,
	 * -> We firstly send the "AT+QISEND" command on the given socket and
	 *    specify the length of data to be transferred.
	 * -> In response to "AT+QISEND" command, the modem may respond with a
	 *    data prompt (>) or not respond at all. If it doesn't respond, we
	 *    exit. If it does respond with a data prompt (>), we move forward.
	 * -> We plainly write all data on the UART and terminate by sending a
	 *    CTRL+Z. Once the modem receives CTRL+Z, it starts processing the
	 *    data and will respond with either "SEND OK", "SEND FAIL" or "ERROR".
	 *    Here we are registering handlers for the first two responses. We
	 *    already have a handler for the "generic" error response.
	 */
	struct modem_cmd cmd[] = {
		MODEM_CMD_DIRECT(">", on_cmd_tx_ready),
		MODEM_CMD("SEND OK", on_cmd_send_ok,   0, ","),
		MODEM_CMD("SEND FAIL", on_cmd_send_fail, 0, ","),
	};

	/* Ensure that valid parameters are passed. */
	if (!buf || len == 0) {
		errno = EINVAL;
		return -1;
	}

	if (!sock->is_connected) {
		errno = ENOTCONN;
		return -1;
	}

	LOG_INF("len: %u", len);

	ret = send_socket_data(sock, to, cmd, ARRAY_SIZE(cmd), buf, len,
			       MDM_CMD_TIMEOUT);
	if (ret < 0) {
		ret = get_tcp_error(sock); 
		/* Error EAGAIN memory allocation failed, ETIMEDOUT if timed out */
		if (ret == MDM_TCP_ERROR_NO_MEMORY) {
			ret = EAGAIN;
		} else if (ret == MDM_TCP_ERROR_TIMEOUT) {
			ret = ETIMEDOUT;
		}
		errno = ret;
		return -ret;
	}

	/* Data was written successfully. */
	errno = 0;
	return ret;
}

/* Func: offload_recvfrom
 * Desc: This function will receive data on the socket object.
 */
static ssize_t offload_recvfrom(void *obj, void *buf, size_t len,
				int flags, struct sockaddr *from,
				socklen_t *fromlen)
{
	struct modem_socket *sock = (struct modem_socket *)obj;
	char   sendbuf[sizeof("AT+Q###RECV=##,####")] = {0};
	int    ret;
	int	   next_packet_size;
	struct socket_read_data sock_data;
	/* Modem command to read the data. */
	struct modem_cmd data_cmd[] = {
		MODEM_CMD("+QIRD: ", on_cmd_sock_readdata, 1U, ""),
		MODEM_CMD("+QSSLRECV: ", on_cmd_sock_readdata, 1U, "") };

	if (!buf || len == 0) {
		errno = EINVAL;
		return -1;
	}

	if (flags & ZSOCK_MSG_PEEK) {
		errno = ENOTSUP;
		return -1;
	}

	/* Wait for packet, if there are none available. */
	next_packet_size = modem_socket_next_packet_size(&mdata.socket_config,
							 sock);
	if (!next_packet_size) {
		if ((flags & ZSOCK_MSG_DONTWAIT) || (sock->flags & O_NONBLOCK)) {
			errno = EAGAIN;
			return -1;
		}

		if (!sock->is_connected) {
			errno = 0;
			return 0;
		}

		modem_socket_wait_data(&mdata.socket_config, sock);
		next_packet_size = modem_socket_next_packet_size(
			&mdata.socket_config, sock);
	}

	if ((sock->ip_proto == IPPROTO_TLS_1_2) || 
		(sock->ip_proto == IPPROTO_DTLS_1_2)) {
		snprintk(sendbuf, sizeof(sendbuf), "AT+QSSLRECV=%d,%zd", 
				sock->id, len);
	} else {
		snprintk(sendbuf, sizeof(sendbuf), "AT+QIRD=%d,%zd", 
				sock->id, len);
	}

	/* Take tx semaphore to ensure only one socket at a time can receive
	   and that semaphore is acquired before mdata.sock_fd is modified. */
	if (k_sem_take(&mdata.cmd_handler_data.sem_tx_lock, MDM_TX_LOCK_TIMEOUT) != 0) {
		LOG_ERR("Error taking semaphore");
		errno = EAGAIN;
		return -1;
	}
	/* Socket read settings */
	(void) memset(&sock_data, 0, sizeof(sock_data));
	sock_data.recv_buf     = buf;
	sock_data.recv_buf_len = len;
	sock_data.recv_addr    = from;
	sock->data	       = &sock_data;
	mdata.sock_fd	       = sock->sock_fd;
	/* Tell the modem to give us data (AT+QIRD=sock_fd,data_len). */
	ret = modem_cmd_send_nolock(&mctx.iface, &mctx.cmd_handler,
			     data_cmd, ARRAY_SIZE(data_cmd), sendbuf, &mdata.sem_response,
			     MDM_RECV_TIMEOUT);
	k_sem_give(&mdata.cmd_handler_data.sem_tx_lock);
	if (ret < 0) {
		errno = -ret;
		ret = -1;
		goto exit;
	}

	/* HACK: use dst address as from */
	if (from && fromlen) {
		*fromlen = sizeof(sock->dst);
		memcpy(from, &sock->dst, *fromlen);
	}

#ifdef CONFIG_MODEM_CONTEXT_VERBOSE_DEBUG
	LOG_HEXDUMP_DBG(sock_data.recv_buf, sock_data.recv_read_len, "RECV");
#endif

	/* Update data on socket with current size. */
	int new_size = get_data_size(sock);
	ret = modem_socket_packet_size_update(&mdata.socket_config, sock, new_size);
	if (ret < 0) {
		LOG_ERR("socket_id:%d err: %d", sock->id, ret);
	}
	if (new_size > 0) {
		/* Data ready indication. */
		LOG_DBG("Data Receive Indication for socket: %d", sock->id);
		modem_socket_data_ready(&mdata.socket_config, sock);
	}

	/* return length of received data */
	errno = 0;
	ret = sock_data.recv_read_len;

exit:
	/* clear socket data */
	sock->data = NULL;
	return ret;
}

/* Func: offload_read
 * Desc: This function reads data from the given socket object.
 */
static ssize_t offload_read(void *obj, void *buffer, size_t count)
{
	return offload_recvfrom(obj, buffer, count, 0, NULL, 0);
}

/* Func: offload_write
 * Desc: This function writes data to the given socket object.
 */
static ssize_t offload_write(void *obj, const void *buffer, size_t count)
{
	return offload_sendto(obj, buffer, count, 0, NULL, 0);
}

/* Func: offload_poll
 * Desc: This function polls on a given socket object.
 */
static int offload_poll(struct zsock_pollfd *fds, int nfds, int msecs)
{
	int i;
	void *obj;

	/* Only accept modem sockets. */
	for (i = 0; i < nfds; i++) {
		if (fds[i].fd < 0) {
			continue;
		}

		/* If vtable matches, then it's modem socket. */
		obj = z_get_fd_obj(fds[i].fd,
				   (const struct fd_op_vtable *) &offload_socket_fd_op_vtable,
				   EINVAL);
		if (obj == NULL) {
			return -1;
		}
	}

	return modem_socket_poll(&mdata.socket_config, fds, nfds, msecs);
}

static int offload_fcntl(void *obj, unsigned int request, va_list args) {
	struct modem_socket *sock = (struct modem_socket *)obj;
	int retval = 0;

	switch (request) {
	case F_GETFL:	
		retval = sock->flags;
		break;

	case F_SETFL:
		sock->flags = va_arg(args, int);
		break;

	default:
		LOG_ERR("Invalid request : %d", request);
		retval = -EINVAL;
	}

	return retval;
}

/* Func: offload_ioctl
 * Desc: Function call to handle various misc requests.
 */
static int offload_ioctl(void *obj, unsigned int request, va_list args)
{
	switch (request) {
	case ZFD_IOCTL_POLL_PREPARE:
		return -EXDEV;

	case ZFD_IOCTL_POLL_UPDATE:
		return -EOPNOTSUPP;

	case ZFD_IOCTL_POLL_OFFLOAD: {
		/* Poll on the given socket. */
		struct zsock_pollfd *fds;
		int nfds, timeout;

		fds = va_arg(args, struct zsock_pollfd *);
		nfds = va_arg(args, int);
		timeout = va_arg(args, int);

		return offload_poll(fds, nfds, timeout);
	}

	default:
		/* Forward to offloaded fcntl()
	 	*  In Zephyr, fcntl() is just an alias of ioctl().
	 	*/
		return offload_fcntl(obj, request, args);
	}
}

/* Handler: +QFLSt: <filename>,<file_size> */
MODEM_CMD_DEFINE(on_cmd_file_list)
{
	int file_size = ATOI(argv[1], 0, "file_size");
	mdata.file_size = file_size;
	memcpy(mdata.file_name, argv[0], strlen(argv[0]));
	return 0;
}

/* Handler: Transparent Mod Ready */
MODEM_CMD_DIRECT_DEFINE(on_cmd_data_ready)
{
	k_sem_give(&mdata.sem_data_ready);
	return len;
}

/* Handler: +QFDWL: <upload_size>,<checksum> */
MODEM_CMD_DEFINE(on_cmd_download_done)
{
	int upload_size = ATOI(argv[0], 0, "upload_size");
	LOG_DBG("Uploaded size %d", upload_size);
	k_sem_give(&mdata.sem_response);
	return 0;
}

/* Handler: +QFUPL: <upload_size>,<checksum> */
MODEM_CMD_DEFINE(on_cmd_data_done)
{
	int upload_size = ATOI(argv[0], 0, "upload_size");
	LOG_DBG("Uploaded size %d", upload_size);
	k_sem_give(&mdata.sem_response);
	return 0;
}

MODEM_CMD_DEFINE(on_cmd_psm_power_down)
{	
	/* stop RSSI delay work */
	k_work_cancel_delayable(&mdata.rssi_query_work);

	mdata.power = MODEM_POWER_PSM;
	quectel_bg95_set_connected(false);
	for(int i = 0; i < MDM_MAX_SOCKETS; i++) {
		if (mdata.sockets[i].id >= mdata.socket_config.base_socket_id) {
			LOG_DBG("invalidating socket: %u", mdata.sockets[i].id);
			modem_socket_put(&mdata.socket_config, mdata.sockets[i].sock_fd);
		}
	}

	pm_suspend_uart();
	enable_psm_ind_interrupt();

	MODEM_SUBMIT_EVT(MODEM_API_PSM_ENTERED_EVT);

	return 0;
}

MODEM_CMD_DEFINE(on_cmd_power_down)
{
	k_sem_give(&mdata.sem_shutdown);
	mdata.power = MODEM_POWER_OFF;
	return 0;
}

MODEM_CMD_DEFINE(on_cmd_sim_ini_stat)
{
	mdata.sim_ini_stat = ATOI(argv[0], -1, "sim_ini_stat");
	LOG_DBG("SIM ini stat %d", mdata.sim_ini_stat);
	k_sem_give(&mdata.sem_response);
	return 0;
}

/** @brief Turn the modem on/off using PWRKEY.
 * 
*/
static void modem_pin_on_off(void)
{
	gpio_pin_set_dt(&power_gpio, 1);
	k_sleep(K_MSEC(1000));
	gpio_pin_set_dt(&power_gpio, 0);
}

static int quectel_bg95_power_down() {
	const char *pw_dwn = "AT+QPOWD";
	int ret;
	int retries = 0;

	struct modem_cmd cmd[] = {
		MODEM_CMD("POWERED DOWN", on_cmd_power_down, 0U, ""),
	};

	if (k_sem_take(&mdata.cmd_handler_data.sem_tx_lock, MDM_TX_LOCK_TIMEOUT) != 0) {
		LOG_ERR("Error taking semaphore");
		return -EAGAIN;
	}

	k_sem_reset(&mdata.sem_shutdown);
#if 1
	do {
		ret = modem_cmd_send_nolock(&mctx.iface, &mctx.cmd_handler, 
				NULL, 0U, pw_dwn, &mdata.sem_response,
				MDM_CMD_TIMEOUT);
		retries++;
	} while((ret != 0) && (retries < MDM_POWER_DOWN_RETRY_COUNT));
	if (ret != 0) {
		goto error;
	}
	
	/* set modem handler commands */
	modem_cmd_handler_update_cmds(mctx.cmd_handler.cmd_handler_data,
				      cmd, ARRAY_SIZE(cmd), false);
#else
	modem_pin_on_off();
#endif

	ret = k_sem_take(&mdata.sem_shutdown, MDM_SHUTDOWN_TIMEOUT);
	if (ret != 0) {
		goto error;
	}
	// Set modem as disconnected after power down.
	mdata.is_connected = false;

	/* unset handler commands and ignore any errors */
	modem_cmd_handler_update_cmds(mctx.cmd_handler.cmd_handler_data,
				      NULL, 0U, false);
	k_sem_give(&mdata.cmd_handler_data.sem_tx_lock);
	LOG_INF("Modem powered down");

	return 0;
error:
	LOG_ERR("Failed to shut down modem, %d", ret);
	/* unset handler commands */
	modem_cmd_handler_update_cmds(mctx.cmd_handler.cmd_handler_data,
				      NULL, 0U, false);
	k_sem_give(&mdata.cmd_handler_data.sem_tx_lock);
	return ret;
}

#if 0 // Uncomment when use
static int set_cops_format(uint8_t format)
{
	char cmd[sizeof("AT+COPS=3,#")];
	int ret;

	__ASSERT_NO_MSG(format >= 0 && format <= 2);

	snprintk(cmd, sizeof(cmd), "AT+COPS=3,%u", format);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, 
		NULL, 0, cmd, &mdata.sem_response,
		MDM_CMD_TIMEOUT);
	return ret;
}
#endif 

static int get_operator_info(void)
{
	const char *cmd_cops = "AT+COPS?";
	int ret;

	struct modem_cmd cmd[] = {
		MODEM_CMD_ARGS_MAX("+COPS: ", on_cmd_cops, 1U, 4U, ","),
	};

	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, 
			cmd, ARRAY_SIZE(cmd), cmd_cops, &mdata.sem_response,
			MDM_CMD_TIMEOUT);

	if (ret != 0) {
		LOG_ERR("Error retrieving operator details");
		return -1;
	}
	
	return 0;
}

#if 0 // Uncomment when we need to use
static int quectel_bg95_set_cereg(uint8_t n)
{
	char buf[sizeof("AT+CEREG=#")];
	int ret;

	if (!(n >= 0 && n <= 3) || (n != 4)) {
		return -EINVAL;
	}
	snprintk(buf, sizeof(buf), "AT+CEREG=%d", n);

	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0, buf,
			     &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret != 0) {
		LOG_ERR("Failed to set CEREG");
	} else {
		LOG_DBG("Set CEREG: %u", n);
	}

	return ret;
}
#endif

static int modem_set_psm_indication(bool enable)
{
	char sendbuf[sizeof("AT+QCFG=#psm/urc#,##")];
	uint8_t en_val;
	int ret;

	if (enable) {
		en_val = 1;
	} else {
		en_val = 0;
	}

	snprintk(sendbuf, sizeof(sendbuf), "AT+QCFG=\"psm/urc\",%u", en_val);

	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0u, sendbuf,
			     &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0) {
		LOG_WRN("Error setting PSM indication");
	}

	return ret;
}

/**
 * @brief Set the PSM requested active and periodic TAU timer values.
 * 
 * @param enable true to enable PSM request, false to disable
 * @param req_rat requested active timer value in E-UTRAN format
 * @param req_rptau requested periodic TAU timer value in E-UTRAN format
 * @return 0 on success, negative on error
*/
static int quectel_bg95_set_psm(bool enable, char *req_rat, char *req_rptau)
{
	char buf[sizeof("AT+QPSMS=#,,,##########,##########")];
	int ret;

	if (enable) {
		if (req_rat == NULL || req_rptau == NULL ||
		    strlen(req_rat) != PSM_TIMER_VAL_LEN - 1 ||
		    strlen(req_rptau) != PSM_TIMER_VAL_LEN - 1) {
			return -EINVAL;
		}
		snprintk(buf, sizeof(buf), "AT+QPSMS=1,,,\"%s\",\"%s\"",
			 req_rptau, req_rat);
	}
	else {
		snprintk(buf, sizeof(buf), "AT+QPSMS=0");
	}

	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0, buf,
			     &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret != 0) {
		LOG_ERR("Failed to set PSM requested values");
	} else {
		LOG_DBG("Set PSM requested values");
		if (enable) {
			modem_set_psm_indication(true);
		}
	}

	return ret;
}


int quectel_bg95_file_find(const char* file_name) {
	char buf[sizeof("AT+QFLST=") + MDM_FILE_NAME_MAX_LENGTH] = {0};
	snprintk(buf, sizeof(buf), "AT+QFLST=\"%s\"", file_name);

	struct modem_cmd cmd[] = {
		MODEM_CMD("+QFLST: ", on_cmd_file_list, 2U, ","),
	};

	memset(mdata.file_name, 0, sizeof(mdata.file_name));
	mdata.file_size = 0;

	int ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler,
			     cmd, 1U, buf, &mdata.sem_response,
			     MDM_CMD_TIMEOUT);
	if (ret != 0) {
		LOG_ERR("Failed to send command to list file %d", ret);
	} else {
		if (strncmp(file_name, mdata.file_name, strlen(file_name)) == 0) {
			LOG_DBG("Found the list %s - size %d", mdata.file_name, mdata.file_size);
		}
	}

	return ret;
}

int quectel_bg95_file_delete(const char* file_name) {
	char buf[sizeof("AT+QFDEL=") + MDM_FILE_NAME_MAX_LENGTH] = {0};
	snprintk(buf, sizeof(buf), "AT+QFDEL=\"%s\"", file_name);

	int ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler,
			     NULL, 0, buf, &mdata.sem_response,
			     MDM_CMD_TIMEOUT);
	if (ret != 0) {
		LOG_ERR("Failed to send command to delete file %d", ret);
	} else {
		LOG_DBG("Deleted file %s successfully", file_name);
	}

	return ret;
}

int quectel_bg95_file_upload(const char* file_name) {
	char buf[sizeof("AT+QFDWL=") + MDM_FILE_NAME_MAX_LENGTH] = {0};
	snprintk(buf, sizeof(buf), "AT+QFDWL=\"%s\"", file_name);

	struct modem_cmd cmd[] = {
		MODEM_CMD_DIRECT("CONNECT", on_cmd_data_ready),
		MODEM_CMD("+QFDWL: ", on_cmd_download_done, 2U, ","),
	};

	k_sem_reset(&mdata.sem_data_ready);
	if (k_sem_take(&mdata.cmd_handler_data.sem_tx_lock, MDM_TX_LOCK_TIMEOUT) != 0) {
		LOG_ERR("Error taking semaphore");
		return -EAGAIN;
	}

	/* Send the Modem command. */
	int ret = modem_cmd_send_nolock(&mctx.iface, &mctx.cmd_handler,
				    NULL, 0U, buf, NULL, K_NO_WAIT);
	if (ret < 0) {
		goto exit;
	}

	/* set command handlers */
	ret = modem_cmd_handler_update_cmds(&mdata.cmd_handler_data,
					    cmd, ARRAY_SIZE(cmd),
					    true);
	if (ret < 0) {
		goto exit;
	}

	/* Wait for 'CONNECT' */
	ret = k_sem_take(&mdata.sem_data_ready, K_MSEC(1000));
	if (ret < 0) {
		/* Didn't get the data prompt - Exit. */
		LOG_DBG("Timeout waiting for tx");
		goto exit;
	}

	/* Write all data on the console */
	k_sem_reset(&mdata.sem_response);
	ret = k_sem_take(&mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0) {
		LOG_DBG("Failed to wait the response from modem");
		goto exit;
	}
exit:
	/* unset handler commands */
	modem_cmd_handler_update_cmds(mctx.cmd_handler.cmd_handler_data,
				      NULL, 0U, false);
	k_sem_give(&mdata.cmd_handler_data.sem_tx_lock);
	return ret;
}

int quectel_bg95_file_download(const char* file_name, const uint8_t* data, const size_t data_length) {
	char buf[sizeof("AT+QFUPL=") + MDM_FILE_NAME_MAX_LENGTH] = {0};
	snprintk(buf, sizeof(buf), "AT+QFUPL=\"%s\",%d,%d", file_name, data_length, 60);

	struct modem_cmd cmd[] = {
		MODEM_CMD_DIRECT("CONNECT", on_cmd_data_ready),
		MODEM_CMD("+QFUPL: ", on_cmd_data_done, 2U, ","),
	};

	k_sem_reset(&mdata.sem_data_ready);
	if (k_sem_take(&mdata.cmd_handler_data.sem_tx_lock, MDM_TX_LOCK_TIMEOUT) != 0) {
		LOG_ERR("Error taking semaphore");
		return -EAGAIN;
	}

	/* Send the Modem command. */
	int ret = modem_cmd_send_nolock(&mctx.iface, &mctx.cmd_handler,
				    NULL, 0U, buf, NULL, K_NO_WAIT);
	if (ret < 0) {
		goto exit;
	}

	/* set command handlers */
	ret = modem_cmd_handler_update_cmds(&mdata.cmd_handler_data,
					    cmd, ARRAY_SIZE(cmd),
					    true);
	if (ret < 0) {
		goto exit;
	}

	/* Wait for 'CONNECT' */
	ret = k_sem_take(&mdata.sem_data_ready, K_MSEC(1000));
	if (ret < 0) {
		/* Didn't get the data prompt - Exit. */
		LOG_DBG("Timeout waiting for tx");
		goto exit;
	}

	LOG_HEXDUMP_DBG(data, data_length, "QFUPL");

	/* Write all data on the console */
	mctx.iface.write(&mctx.iface, data, data_length);

	k_sem_reset(&mdata.sem_response);
	ret = k_sem_take(&mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0) {
		LOG_DBG("Failed to wait the response from modem");
		goto exit;
	}
exit:
	/* unset handler commands */
	modem_cmd_handler_update_cmds(mctx.cmd_handler.cmd_handler_data,
				      NULL, 0U, false);
	k_sem_give(&mdata.cmd_handler_data.sem_tx_lock);
	return ret;
}

static int on_connect_dtls_init(struct modem_socket *sock)
{
	int ret = 0;
	char psk_fn[sizeof("!##_server.psk!")];
	char buf[256];

	// File name is <SSL context ID>_server.psk.
	snprintk(psk_fn, sizeof(psk_fn), "%d_server.psk", sock->id);
	if (quectel_bg95_file_find(psk_fn) == 0) {
		if (quectel_bg95_file_delete(psk_fn) != 0) {
			return -1;
		}
	}

#if defined(CONFIG_MODEM_QUECTEL_BG95_M3_DYNAMIC_PSK)
	// Use dynamic psk data if not empty.
	if (mdata.psk.id_len > 0 && mdata.psk.psk_len > 0) {
		ret = snprintk(buf, sizeof(buf), "%s&", mdata.psk.id);
		ret += bin2hex(mdata.psk.psk, mdata.psk.psk_len,
			       buf + ret, sizeof(buf) - ret);
	} else
#endif  
	{
		// Modem expects file content in format <PSK_ID>&<PSK_KEY>
		ret = snprintk(buf, sizeof(buf), "%s&%s", 
				CONFIG_MODEM_QUECTEL_BG95_M3_PSK_ID, 
				CONFIG_MODEM_QUECTEL_BG95_M3_PSK_KEY);
		if (ret >= sizeof(buf)) {
			LOG_WRN("PSK file truncated");
			ret = sizeof(buf) - 1;
		}
	}
	ret = quectel_bg95_file_download(psk_fn, buf, ret);
	if (ret != 0) {
		LOG_DBG("Failed to download PSK file %d", ret);
	}

	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,0X00AE", "ciphersuite", sock->id);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG for CipherSuite Type");
		return -1;
	}

	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,%d", "dtlsversion", sock->id, 1);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG for DTLS Version");
		return -1;
	}
		
	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,%d", "dtls", sock->id, 1);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG for DTLS enable");
		return -1;
	}

	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,%d", "negotiatetime", 
		 sock->id, CONFIG_MODEM_QUECTEL_BG95_M3_SSL_NEGOTIATION_TIMEOUT);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG for DTLS enable");
		return -1;
	}

	return 0;
}

static int on_connect_tls_init(struct modem_socket *sock)
{
	int ret = 0;

	if (quectel_bg95_file_find(MDM_TLS_CA_FILE_NAME) == 0) {
		if (quectel_bg95_file_delete(MDM_TLS_CA_FILE_NAME) != 0) {
			return -1;
		}
	}

	if (quectel_bg95_file_find(MDM_TLS_CLIENT_CERT_FILE_NAME) == 0) {
		if (quectel_bg95_file_delete(MDM_TLS_CLIENT_CERT_FILE_NAME) != 0) {
			return -1;
		}
	}

	if (quectel_bg95_file_find(MDM_TLS_PRIV_KEY_FILE_NAME) == 0) {
		if (quectel_bg95_file_delete(MDM_TLS_PRIV_KEY_FILE_NAME) != 0) {
			return -1;
		}
	}

	ret = quectel_bg95_file_download(MDM_TLS_CA_FILE_NAME, MEMFAULT_ROOT_CERTS_PEM, sizeof(MEMFAULT_ROOT_CERTS_PEM) - 1);
	if (ret != 0) {
		LOG_DBG("Failed to download CA Certificate %d", ret);
		return ret;
	}

	ret = quectel_bg95_file_download(MDM_TLS_CLIENT_CERT_FILE_NAME, "empty", sizeof("empty") - 1);
	if (ret != 0) {
		LOG_DBG("Failed to download Client Certificate %d", ret);
		return ret;
	}

	ret = quectel_bg95_file_download(MDM_TLS_PRIV_KEY_FILE_NAME, "empty", sizeof("empty") - 1);
	if (ret != 0) {
		LOG_DBG("Failed to download Private Key %d", ret);
		return ret;
	}

	char buf[256];

	snprintk(buf, sizeof(buf), "AT+QFLST");
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG for CipherSuite Type");
		return -1;
	}

	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,0XFFFF", "ciphersuite", sock->id);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG for CipherSuite Type");
		return -1;
	}

	/* Set CA path */
	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,\"%s\"", "cacert", sock->id, MDM_TLS_CA_FILE_NAME);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG for CA Path");
		return -1;
	}

	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,\"%s\"", "clientcert", sock->id, MDM_TLS_CLIENT_CERT_FILE_NAME);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG for Client Certificate Path");
		return -1;
	}

	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,\"%s\"", "clientkey", sock->id, MDM_TLS_PRIV_KEY_FILE_NAME);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG for Client Private Key Path");
		return -1;
	}

	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,%d", "sslversion", sock->id, 3);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG for SSL Version");
		return -1;
	}

	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,%d", "seclevel", sock->id, 0);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG->seclevel");
		return -1;
	}

	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,%d", "negotiatetime", sock->id, 300);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG->negotiatetime");
		return -1;
	}

	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,%d", "ignorelocaltime", sock->id, 0);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG->ignorelocaltime");
		return -1;
	}

	/* Disable DTLS when using TLS socket */
	snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"%s\",%d,%d", "dtls", sock->id, 0);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf,
						 &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0)
	{
		LOG_DBG("Error to set QSSLCFG for DTLS enable");
		return -1;
	}
	
	return 0;
}

/* Func: offload_connect
 * Desc: This function will connect with a provided TCP.
 */
static int offload_connect(void *obj, const struct sockaddr *addr,
						   socklen_t addrlen)
{
	struct modem_socket *sock     = (struct modem_socket *) obj;
	uint16_t	    dst_port  = 0;
	struct modem_cmd    cmd[]     = {
		MODEM_CMD("+QIOPEN: ", on_cmd_atcmdinfo_sockopen, 2U, ","),
		MODEM_CMD("+QSSLOPEN: ", on_cmd_atcmdinfo_sslopen, 2U, ",") };
	char	buf[sizeof("AT+Q###OPEN=#,##,!###!,!####:####:####:####:####:####:####:####!,######") + 256] = {0};
	int	ret;
	char	ip_str[NET_IPV6_ADDR_LEN];

	if (modem_socket_is_allocated(&mdata.socket_config, sock) == false) {
		LOG_ERR("Invalid socket_id(%d) from fd:%d",
			sock->id, sock->sock_fd);
		errno = EINVAL;
		return -1;
	}

	if (sock->is_connected == true) {
		LOG_ERR("Socket is already connected!! socket_id(%d), socket_fd:%d",
			sock->id, sock->sock_fd);
		errno = EISCONN;
		return -1;
	}

	/* Find the correct destination port. */
	if (addr->sa_family == AF_INET6) {
		dst_port = ntohs(net_sin6(addr)->sin6_port);
	} else if (addr->sa_family == AF_INET) {
		dst_port = ntohs(net_sin(addr)->sin_port);
	}

	if (sock->ip_proto == IPPROTO_TLS_1_2) {
		if (on_connect_tls_init(sock) != 0) {
			errno = EAGAIN;
			return -errno;
		}
	} else if (sock->ip_proto == IPPROTO_DTLS_1_2) {
		if (on_connect_dtls_init(sock) != 0) {
			errno = EAGAIN;
			return -errno;
		}
	}
	

	k_sem_reset(&mdata.sem_sock_conn);

	ret = modem_context_sprint_ip_addr(addr, ip_str, sizeof(ip_str));
	if (ret != 0) {
		LOG_ERR("Error formatting IP string %d", ret);
		LOG_ERR("Closing the socket!!!");
		socket_close(sock, false);
		errno = -ret;
		return -1;
	}
	
	/* Formulate the complete string. */
	/* Open the socket with buffer access mode */
	if ((sock->ip_proto == IPPROTO_TLS_1_2) || (sock->ip_proto == IPPROTO_DTLS_1_2)) {
		snprintk(buf, sizeof(buf), "AT+QSSLOPEN=%d,%d,%d,\"%s\",%d,0", 1, sock->id, sock->id,
			ip_str, dst_port);
	} else if (sock->ip_proto == IPPROTO_UDP) {
		snprintk(buf, sizeof(buf), "AT+QIOPEN=%d,%d,\"%s\",\"%s\",%d,0,0", 1, sock->id, "UDP",
			ip_str, dst_port);
	} else {
		snprintk(buf, sizeof(buf), "AT+QIOPEN=%d,%d,\"%s\",\"%s\",%d,0,0", 1, sock->id, "TCP",
			ip_str, dst_port);
	}

	if (k_sem_take(&mdata.cmd_handler_data.sem_tx_lock, MDM_TX_LOCK_TIMEOUT) != 0) {
		LOG_ERR("Error taking semaphore");
		errno = EAGAIN;
		return -1;
	}

	/* Send out the command. */
	ret = modem_cmd_send_nolock(&mctx.iface, &mctx.cmd_handler,
			     NULL, 0U, buf,
			     &mdata.sem_response, K_SECONDS(1));
	if (ret < 0) {
		LOG_ERR("%s ret:%d", buf, ret);
		LOG_ERR("Closing the socket!!!");
		k_sem_give(&mdata.cmd_handler_data.sem_tx_lock);
		socket_close(sock, false);
		goto exit;
	}

	/* set command handlers */
	ret = modem_cmd_handler_update_cmds(&mdata.cmd_handler_data, cmd, ARRAY_SIZE(cmd), true);
	if (ret < 0) {
		k_sem_give(&mdata.cmd_handler_data.sem_tx_lock);
		socket_close(sock, false);
		goto exit;
	}

	/* Wait for QI+OPEN */
	ret = k_sem_take(&mdata.sem_sock_conn, MDM_CMD_CONN_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Timeout waiting for socket open");
		LOG_ERR("Closing the socket!!!");
		k_sem_give(&mdata.cmd_handler_data.sem_tx_lock);
		socket_close(sock, false);
		goto exit;
	}

	ret = modem_cmd_handler_get_error(&mdata.cmd_handler_data);
	if (ret != 0) {
		bool force_close = false;
		LOG_ERR("Closing the socket!!! error %d", ret);
		__ASSERT_NO_MSG(ret != 550);
		if (ret == MDM_TCP_ERROR_TIMEOUT) {
			ret = -ETIMEDOUT;
		} else if (ret == MDM_TCP_ERROR_SOCKET_IN_USE) {
			force_close = true;
		} else {
			ret = -ret;
		}
		k_sem_give(&mdata.cmd_handler_data.sem_tx_lock);
		socket_close(sock, force_close);
		goto exit;
	}

	k_sem_give(&mdata.cmd_handler_data.sem_tx_lock);

	/* Connected successfully. */
	sock->is_connected = true;
	errno = 0;
	return 0;

exit:
	(void) modem_cmd_handler_update_cmds(&mdata.cmd_handler_data,
					     NULL, 0U, false);
	errno = -ret;
	return -1;
}

/* Func: offload_close
 * Desc: This function closes the connection with the remote client and
 * frees the socket.
 */
static int offload_close(void *obj)
{
	struct modem_socket *sock = (struct modem_socket *) obj;

	/* Make sure we assigned an id */
	if (modem_socket_is_allocated(&mdata.socket_config, sock) == false) {
		return 0;
	}

	/* Close the socket */
	socket_close(sock, false);

	return 0;
}

/* Func: offload_sendmsg
 * Desc: This function sends messages to the modem.
 */
static ssize_t offload_sendmsg(void *obj, const struct msghdr *msg, int flags)
{
	ssize_t sent = 0;
	int rc;

	LOG_DBG("msg_iovlen:%zd flags:%d", msg->msg_iovlen, flags);

	for (int i = 0; i < msg->msg_iovlen; i++) {
		const char *buf = msg->msg_iov[i].iov_base;
		size_t len	= msg->msg_iov[i].iov_len;

		while (len > 0) {
			rc = offload_sendto(obj, buf, len, flags,
					    msg->msg_name, msg->msg_namelen);
			if (rc < 0) {
				if (rc == -EAGAIN) {
					k_sleep(MDM_SENDMSG_SLEEP);
				} else {
					sent = rc;
					break;
				}
			} else {
				sent += rc;
				buf += rc;
				len -= rc;
			}
		}
	}

	return (ssize_t) sent;
}

/* Func: modem_rx
 * Desc: Thread to process all messages received from the Modem.
 */
static void modem_rx(void)
{
	while (true) {

		/* Wait for incoming data */
		modem_iface_uart_rx_wait(&mctx.iface, K_FOREVER);

		modem_cmd_handler_process(&mctx.cmd_handler, &mctx.iface);
	}
}

/* Func: modem_pdp_context_active
 * Desc: This helper function is called from modem_setup, and is
 * used to open the PDP context. If there is trouble activating the
 * PDP context, we try to deactive and reactive MDM_PDP_ACT_RETRY_COUNT times.
 * If it fails, we return an error.
 */
static int modem_pdp_context_activate(void)
{
	int ret;
	int retry_count = 0;

	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler,
			     NULL, 0U, "AT+QIACT=1", &mdata.sem_response,
			     MDM_REGISTRATION_TIMEOUT);

	/* If there is trouble activating the PDP context, we try to deactivate/reactive it. */
	while (ret == -EIO && retry_count < MDM_PDP_ACT_RETRY_COUNT) {
		ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler,
			     NULL, 0U, "AT+QIDEACT=1", &mdata.sem_response,
			     MDM_CMD_TIMEOUT);

		/* If there's any error for AT+QIDEACT, restart the module. */
		if (ret != 0) {
			return ret;
		}

		ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler,
			     NULL, 0U, "AT+QIACT=1", &mdata.sem_response,
			     MDM_REGISTRATION_TIMEOUT);

		retry_count++;
	}

	if (ret == -EIO && retry_count >= MDM_PDP_ACT_RETRY_COUNT) {
		LOG_ERR("Retried activating/deactivating too many times.");
	}

	return ret;
}

/**
 * @brief Call the set event callback with the specified event.
 * 
 * @param evt_type Event type to be forwarded to callback.
 * @return 0 on success, negative on error.
*/
static int modem_event_callback(const struct modem_api_evt *evt)
{
	__ASSERT_NO_MSG(evt != NULL);

	if (mdata.evt_callback == NULL) {
		return -ENOSYS;
	}

	mdata.evt_callback(evt);

	return 0;
}

/**
 * @brief Set modem status to connected/disconnected and call event callback.
 * When modem state changes to connected, activate pdp context.
 * 
 * @param connected true: change to connected, false: change to disconnected.
*/
static void quectel_bg95_set_connected(bool connected)
{
	int ret;

	if (!connected) {
		if (mdata.is_connected && 
		    !(mdata.power == MODEM_POWER_PSM)) {
			MODEM_SUBMIT_EVT(MODEM_API_DISCONNECTED_EVT);
		}
		mdata.is_connected = false;
	} else {
		if (mdata.is_connected) {
			return;
		}

		ret = modem_pdp_context_activate();
		if (ret < 0) {
			LOG_ERR("Error activating modem with pdp context");
		} else if (ret == 0) {
			bool enable = IS_ENABLED(CONFIG_MODEM_QUECTEL_BG95_PSM);
			quectel_bg95_set_psm(enable,
					CONFIG_MODEM_QUECTEL_BG95_M3_PSM_REQ_RAT,
					CONFIG_MODEM_QUECTEL_BG95_M3_PSM_REQ_RPTAU);
			MODEM_SUBMIT_EVT(MODEM_API_CONNECTED_EVT);
			LOG_INF("Network connected.");
			mdata.is_connected = true;
		}
	}
}

static void modem_dynamic_update_work(struct k_work *work)
{
	struct modem_api_evt evt = {
		.type = MODEM_API_DYNAMIC_DATA_UPDATE_EVT
	};
	struct modem_network_data nw_data;
	int ret;

	get_operator_info();

	ret = k_mutex_lock(&mdata.mdm_network_mutex, MDM_CMD_TIMEOUT);
	__ASSERT_NO_MSG(ret == 0);
	if (ret != 0) {
		return;
	}
	memcpy(&nw_data, &mdata.mdm_network, sizeof(nw_data));
	k_mutex_unlock(&mdata.mdm_network_mutex);

	evt.dynamic_data = &nw_data;

	modem_event_callback(&evt);
}

/**
 * @brief Activate pdp context and call event handler when device disconnects/
 * 	  connects.
*/
static void modem_connect_work(void) 
{
	if (mdata.mdm_rssi == MDM_RSSI_INVALID) {
		quectel_bg95_set_connected(false);
		return;
	}

	/* If the RSSI is valid, which means that the network is ready, 
	 * and the modem is not connected, we try to activate the PDP context. */
	quectel_bg95_set_connected(true);
}

/* Func: modem_rssi_query_work
 * Desc: Routine to get Modem RSSI.
 */
static void modem_rssi_query_work(struct k_work *work)
{
	struct modem_cmd cmd  = MODEM_CMD("+CSQ: ", on_cmd_atcmdinfo_rssi_csq, 2U, ",");
	static char *send_cmd = "AT+CSQ";
	int ret;
	k_timeout_t timeout = K_SECONDS(RSSI_TIMEOUT_SECS);

	/* query modem RSSI */
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler,
			     &cmd, 1U, send_cmd, &mdata.sem_response,
			     MDM_CMD_TIMEOUT);
	if (ret < 0) {
		if (!mdata.is_connected) {
			/* Set RSSI to invalid if AT+CSQ returns with an error
			   and modem is currently not connected. 
			   AT+CSQ can timeout when modem is busy downloading
			   large amounts of data, e.g. firmware update. */
			mdata.mdm_rssi = MDM_RSSI_INVALID;
		}
		LOG_ERR("AT+CSQ ret:%d", ret);
	}

	modem_connect_work();

	if (!mdata.is_connected) {
		timeout = MDM_WAIT_FOR_RSSI_TIMEOUT;
	}

	/* Re-start RSSI query work */
	if (work && (mdata.power == MODEM_POWER_ON)) {
		k_work_reschedule_for_queue(&modem_workq,
					    &mdata.rssi_query_work,
					    timeout);
	}
}

/* Func: pin_init
 * Desc: Boot up the Modem.
 */
static void pin_init(void)
{
	LOG_INF("Setting Modem Pins");

#if DT_INST_NODE_HAS_PROP(0, mdm_on_off_gpios)
	gpio_pin_set_dt(&on_off_gpio, 1);
	k_sleep(K_MSEC(500));
#endif

	modem_pin_on_off();

	LOG_INF("... Done!");
}

static const struct modem_cmd response_cmds[] = {
	MODEM_CMD("OK", on_cmd_ok, 0U, ""),
	MODEM_CMD("ERROR", on_cmd_error, 0U, ""),
	MODEM_CMD("+CME ERROR: ", on_cmd_exterror, 1U, ""),
};

static const struct modem_cmd unsol_cmds[] = {
	MODEM_CMD("+QIURC: \"recv\",",	   on_cmd_unsol_recv,  1U, ""),
	MODEM_CMD("+QIURC: \"closed\",",   on_cmd_unsol_close, 1U, ""),
	MODEM_CMD("+QSSLURC: \"recv\",",   on_cmd_unsol_recv,  1U, ""),
	MODEM_CMD("+QSSLURC: \"closed\",", on_cmd_unsol_close, 1U, ""),
	MODEM_CMD("+QIURC: \"dnsgip\",", on_cmd_dns, 0U, ""),
	MODEM_CMD("+QIURC: \"pdpdeact\",", on_cmd_unsol_pdpdeact, 1U, ","),
	MODEM_CMD_ARGS_MAX("+CEREG: ", on_cmd_unsol_cereg, 1U, 9U, ","),
	MODEM_CMD("+QPSMTIMER: ", on_cmd_unsol_qpsmtimer, 2U, ","),
	MODEM_CMD("APP RDY", on_cmd_unsol_rdy, 0U, ""),
	MODEM_CMD("NORMAL POWER DOWN", on_cmd_power_down, 0U, ""),
	MODEM_CMD("PSM POWER DOWN", on_cmd_psm_power_down, 0U, ""),
};

#if CONFIG_MODEM_QUECTEL_BG95_PSM
static const struct setup_cmd psm_wakeup_cmds[] = {
	SETUP_CMD_NOHANDLE("ATE0"),
	SETUP_CMD_NOHANDLE("AT+CMEE=1"),
	SETUP_CMD_NOHANDLE("AT+CEREG=4"),
	SETUP_CMD_NOHANDLE("AT+COPS=3,2"),
};
#endif

/* Commands sent to the modem to set it up at boot time. */
static const struct setup_cmd setup_cmds[] = {
	SETUP_CMD_NOHANDLE("ATE0"),
	SETUP_CMD_NOHANDLE("ATH"),
	SETUP_CMD_NOHANDLE("AT+CFUN=0"),
	SETUP_CMD_NOHANDLE("AT+QCFG=\"nwscanmode\",3,1"),
	SETUP_CMD_NOHANDLE("AT+CEREG=4"),
	SETUP_CMD_NOHANDLE("AT+COPS=3,2"),
	SETUP_CMD_NOHANDLE("AT+CFUN=1"),
	SETUP_CMD_NOHANDLE("AT+CMEE=1"),
	SETUP_CMD_NOHANDLE("AT+QURCCFG=\"urcport\",\"uart1\""),
#ifdef CONFIG_MODEM_QUECTEL_BG95_PSM
	SETUP_CMD_NOHANDLE("AT+QCFG=\"psm/urc\",1"),
#endif

	/* Commands to read info from the modem (things like IMEI, Model etc). */
	SETUP_CMD("AT+CGMI", "", on_cmd_atcmdinfo_manufacturer, 0U, ""),
	SETUP_CMD("AT+CGMM", "", on_cmd_atcmdinfo_model, 0U, ""),
	SETUP_CMD("AT+QGMR", "", on_cmd_atcmdinfo_revision, 0U, ""),
	SETUP_CMD("AT+CGSN", "", on_cmd_atcmdinfo_imei, 0U, ""),
	SETUP_CMD_NOHANDLE("AT+QICSGP=1,3,\"" MDM_APN "\",\"" MDM_USERNAME "\",\"" MDM_PASSWORD "\",1"),
	/* Save current profile to ensure settings are loaded on next power up. */
	SETUP_CMD_NOHANDLE("AT&W")
};

#ifdef CONFIG_MODEM_QUECTEL_BG95_PSM
/* Func: modem_rssi_query_work
 * Desc: Routine to get Modem RSSI.
 */
static void modem_psm_wakeup_work(struct k_work *work)
{
	int ret;

	disable_psm_ind_interrupt();

	/* Run setup commands on the modem. */
	ret = modem_cmd_handler_setup_cmds(&mctx.iface, &mctx.cmd_handler,
					   psm_wakeup_cmds, ARRAY_SIZE(psm_wakeup_cmds),
					   &mdata.sem_response, MDM_REGISTRATION_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("wakeup commands fail: %u", ret);
	}

	mdata.power = MODEM_POWER_ON;

	k_work_reschedule_for_queue(&modem_workq, &mdata.rssi_query_work,
				    K_NO_WAIT);
}
#endif

/**
 * Retrieve SIM initialization status from modem.
 * 
 * @return true if ready, false if not ready or error.
*/
static bool modem_get_sim_init_status(void)
{
	char buf[] = "AT+QINISTAT";
	int ret;
	struct modem_cmd cmd[] = {
		MODEM_CMD("+QINISTAT: ", on_cmd_sim_ini_stat, 1U, ""),
	};


	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, cmd, ARRAY_SIZE(cmd), buf,
			     &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Failed to set retrieve SIM init status");
		return false;
	}

	if (mdata.sim_ini_stat == 3) {
		return true;
	}

	return false;
}

static void modem_retrieve_sim_numbers(void)
{
#if defined(CONFIG_MODEM_QUECTEL_BG95_M3_SIM_NUMBERS)
	static const struct setup_cmd sim_number_cmds[] = {
		SETUP_CMD("AT+CIMI", "", on_cmd_atcmdinfo_imsi, 0U, ""),
		SETUP_CMD("AT+QCCID", "", on_cmd_atcmdinfo_iccid, 0U, ""),
	};

	int cnt = 0;
	bool ret_bool;
	int ret;
	
	while (!(ret_bool = modem_get_sim_init_status()) &&
		(cnt < 3)) {
		cnt++;
		k_sleep(K_MSEC(100));
	}

	if (!ret_bool) {
		return;
	}

	/* Run SIM number setup commands on the modem. */
	ret = modem_cmd_handler_setup_cmds(&mctx.iface, &mctx.cmd_handler,
					   sim_number_cmds, ARRAY_SIZE(sim_number_cmds),
					   &mdata.sem_response, MDM_REGISTRATION_TIMEOUT);
	
	if (ret < 0) {
		LOG_WRN("Unable to read sim numbers");
	}
#endif /* #if defined(CONFIG_MODEM_QUECTEL_BG95_M3_SIM_NUMBERS) */		   
}

/* Func: modem_setup
 * Desc: This function is used to setup the modem from zero. The idea
 * is that this function will be called right after the modem is
 * powered on to do the stuff necessary to talk to the modem.
 */
static int modem_setup(void)
{
	int ret = 0;
	int counter = 0;

retry:
	/* Setup the pins to ensure that Modem is enabled. */
	pin_init();

	/* stop RSSI delay work */
	k_work_cancel_delayable(&mdata.rssi_query_work);

	/* Let the modem respond. */
	LOG_INF("Waiting for modem to respond");
	ret = k_sem_take(&mdata.sem_ready, MDM_MAX_BOOT_TIME);
	if (ret < 0) {
		LOG_ERR("Timeout waiting for RDY");
		if (counter < 4) {
			counter++;
			LOG_INF("Retrying...");
			goto retry;
		}
		goto error;
	}

	/* Run setup commands on the modem. */
	ret = modem_cmd_handler_setup_cmds(&mctx.iface, &mctx.cmd_handler,
					   setup_cmds, ARRAY_SIZE(setup_cmds),
					   &mdata.sem_response, MDM_REGISTRATION_TIMEOUT);
	if (ret < 0) {
		goto error;
	}

	modem_retrieve_sim_numbers();

	/* Modem is ready - Start RSSI work in the background. */
	LOG_INF("Modem is initialized.");
	mdata.power = MODEM_POWER_ON;
	k_work_reschedule_for_queue(&modem_workq, &mdata.rssi_query_work,
				    MDM_WAIT_FOR_RSSI_TIMEOUT);

error:
	return ret;
}

static int map_credentials(struct modem_socket *sock, const void *optval, socklen_t optlen)
{
	return 0;
}

static int modem_set_socket_timeout(struct modem_socket *sock, int timeout) {
	char buf[sizeof("AT+QSSLCFG=#negotiatetime#,##,####")] = {0};
	if (sock->ip_proto == IPPROTO_TLS_1_2) {
		snprintk(buf, sizeof(buf), "AT+QSSLCFG=\"negotiatetime\",%d,%d", sock->id, timeout);
	} else {
		return -EINVAL;
	}

	/* Send out the command. */
	int ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0U, buf, &mdata.sem_response, MDM_CMD_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s ret:%d", buf, ret);
	}
	return ret;
}

static int offload_setsockopt(void *obj, int level, int optname,
			      const void *optval, socklen_t optlen)
{
	struct modem_socket *sock = (struct modem_socket *)obj;

	int ret;

	if (level == SOL_TLS) {
		switch (optname) {
		case TLS_SEC_TAG_LIST:
			ret = map_credentials(sock, optval, optlen);
			break;
		case TLS_HOSTNAME:
			return 0;
		case TLS_PEER_VERIFY:
			return 0;
		default:
			return 0;
		}
	} else if (level == SOL_SOCKET) {
		ret = 0;
		switch (optname) {
			case SO_RCVTIMEO: {
				uint32_t timeout = *(uint32_t*)optval;
				modem_set_socket_timeout(sock, timeout);
				break;
			}
			default:
			break;
	}
	} else {
		return -EINVAL;
	}

	return ret;
}

static const struct socket_op_vtable offload_socket_fd_op_vtable = {
	.fd_vtable = {
		.read	= offload_read,
		.write	= offload_write,
		.close	= offload_close,
		.ioctl	= offload_ioctl,
	},
	.bind		= NULL,
	.connect	= offload_connect,
	.sendto		= offload_sendto,
	.recvfrom	= offload_recvfrom,
	.listen		= NULL,
	.accept		= NULL,
	.sendmsg	= offload_sendmsg,
	.getsockopt	= NULL,
	.setsockopt	= offload_setsockopt,
};

#if defined(CONFIG_DNS_RESOLVER)
/* TODO: This is a bare-bones implementation of DNS handling
 * We ignore most of the hints like ai_family, ai_protocol and ai_socktype.
 * Later, we can add additional handling if it makes sense.
 */
static int offload_getaddrinfo(const char *node, const char *service,
			       const struct zsock_addrinfo *hints,
			       struct zsock_addrinfo **res)
{
	uint32_t port = 0U;
	int ret;
	struct zsock_addrinfo *result;
	/* DNS command + 128 bytes for domain name parameter */
	char sendbuf[sizeof("AT+QIDNSGIP=#,'[]'\r") + 128];

	*res = calloc(AI_ARR_MAX, sizeof(struct zsock_addrinfo));
	if (!(*res)) {
		LOG_ERR("Allocation of struct zsock_addrinfo failed");
		return DNS_EAI_MEMORY;
	}
	result = &(*res[0]);

	/* FIXME: Hard-code DNS to return only IPv4 */
	result->ai_family = AF_INET;
	result->_ai_addr.sa_family = AF_INET;
	result->ai_addr = &result->_ai_addr;
	result->ai_addrlen = sizeof(result->_ai_addr);
	result->ai_canonname = result->_ai_canonname;
	result->_ai_canonname[0] = '\0';

	if (service) {
		port = ATOI(service, 0U, "port");
		if (port < 1 || port > USHRT_MAX) {
			free(*res);
			return DNS_EAI_SERVICE;
		}
	}

	if (port > 0U) {
		/* FIXME: DNS is hard-coded to return only IPv4 */
		if (result->_ai_addr.sa_family == AF_INET) {
			net_sin(&result->_ai_addr)->sin_port = htons(port);
		}
	}

	/* check to see if node is an IP address */
	if (net_addr_pton(result->ai_family, node,
			  &((struct sockaddr_in *)&result->_ai_addr)->sin_addr)
	    == 0) {
		return 0;
	}

	/* user flagged node as numeric host, but we failed net_addr_pton */
	if (hints && hints->ai_flags & AI_NUMERICHOST) {
		free(*res);
		return DNS_EAI_NONAME;
	}
	/* Ensure only one DNS request is processed at a time.*/
	ret = k_sem_take(&mdata.sem_dns_busy, MDM_TX_LOCK_TIMEOUT);
	if (ret != 0) {
		free(*res);
		return DNS_EAI_AGAIN;
	}
	mdata.dns_ai = *res;
	mdata.dns_ready = false;
	mdata.dns_request = true;
	mdata.dns_result = 0;
	snprintk(sendbuf, sizeof(sendbuf), "AT+QIDNSGIP=1,\"%s\"", node);
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler,
				NULL, 0, sendbuf, &mdata.sem_dns_ready,
				MDM_DNS_TIMEOUT);
	k_sem_give(&mdata.sem_dns_busy);
	if (ret < 0) {
		return ret;
	}

	LOG_DBG("DNS RESULT: %s",
		net_addr_ntop(result->ai_family,
			      &net_sin(&result->_ai_addr)->sin_addr,
			      sendbuf, NET_IPV4_ADDR_LEN));

	return 0;
}

static void offload_freeaddrinfo(struct zsock_addrinfo *res)
{
	__ASSERT_NO_MSG(res);

	free(res);
}

const struct socket_dns_offload offload_dns_ops = {
	.getaddrinfo = offload_getaddrinfo,
	.freeaddrinfo = offload_freeaddrinfo,
};
#endif

static int offload_socket(int family, int type, int proto);

/* Setup the Modem NET Interface. */
static void modem_net_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct modem_data *data	 = dev->data;

	/* Direct socket offload used instead of net offload: */
	net_if_set_link_addr(iface, modem_get_mac(dev),
			     sizeof(data->mac_addr),
			     NET_LINK_ETHERNET);
	data->net_iface = iface;

	net_if_socket_offload_set(iface, offload_socket);

#ifdef CONFIG_DNS_RESOLVER
	socket_offload_dns_register(&offload_dns_ops);
#endif
}

/**
 * @brief Initialize the event handler callback.
 * 
 * @param dev Pointer to the device
 * @param evt_handler Event handler callback function
 * @return 0 on success, negative on error
*/
static int quectel_bg95_evt_handler_init(const struct device *dev, 
					 modem_api_evt_handler_t evt_handler)
{
	struct modem_data *data = dev->data;

	data->evt_callback = evt_handler;

	return 0;
}

static int quectel_bg95_set_credentials(const struct device *dev,
					enum modem_api_cred_type type,
					uint8_t *cred_buf, uint8_t cred_len)
{
	struct modem_data *data = dev->data;
#if defined(CONFIG_MODEM_QUECTEL_BG95_M3_DYNAMIC_PSK)
	if (dev == NULL || cred_buf == NULL) {
		return -EINVAL;
	}

	if (type == MODEM_API_CRED_TYPE_PSK_ID) {
		if (cred_len > sizeof(data->psk.id)) {
			return -ENOMEM;
		}
		memcpy(data->psk.id, cred_buf, cred_len);
		data->psk.id_len = cred_len;
	} else if (type == MODEM_API_CRED_TYPE_PSK) {
		if (cred_len > sizeof(data->psk.psk)) {
			return -ENOMEM;
		}
		memcpy(data->psk.psk, cred_buf, cred_len);
		data->psk.psk_len = cred_len;
	} else {
		return -EINVAL;
	}

	return 0;
#else
	return -ENOTSUP;
#endif
}

static int quectel_bg95_psm_cmd(const struct device *dev,
				enum modem_api_psm_cmd cmd, void *psm_data)
{
	if (cmd == MODEM_API_PSM_CMD_WAKEUP) {
		return quectel_bg95_psm_wakeup();
	}

	return -EINVAL;
}


static int quectel_bg95_get_static_info(const struct device *dev,
				 struct modem_static_info *info)
{
	struct modem_data *data = dev->data;

	__ASSERT_NO_MSG(info != NULL);
	if (info == NULL) {
		return -EINVAL;
	}

	memcpy(info->manufacturer, data->mdm_manufacturer,
	       sizeof(info->manufacturer));
	memcpy(info->model, data->mdm_model, sizeof(info->model));
	memcpy(info->revision, data->mdm_revision, sizeof(info->revision));
	memcpy(info->imei, data->mdm_imei, sizeof(info->imei));
#if defined(CONFIG_MODEM_QUECTEL_BG95_M3_SIM_NUMBERS)
	memcpy(info->imsi, data->mdm_imsi, sizeof(info->imsi));
	memcpy(info->iccid, data->mdm_iccid, sizeof(info->iccid));
#endif /* #if defined(CONFIG_MODEM_QUECTEL_BG95_M3_SIM_NUMBERS) */

	return 0;
}

static int quectel_bg95_modem_get_dynamic_info(const struct device *dev,
					       struct modem_network_data *data)
{
	struct modem_data *mdm_data = dev->data;
	int ret;

	__ASSERT_NO_MSG(data != NULL);
	if (data == NULL) {
		return -EINVAL;
	}

	ret = k_mutex_lock(&mdm_data->mdm_network_mutex, MDM_CMD_TIMEOUT);
	__ASSERT_NO_MSG(ret == 0);
	if (ret != 0) {
		return -ETIMEDOUT;
	}
	memcpy(data, &mdm_data->mdm_network, sizeof(*data));
	k_mutex_unlock(&mdm_data->mdm_network_mutex);

	return 0;
}

static int quectel_bg95_get_data(const struct device *dev,
				 enum modem_api_data_request request,
				 struct modem_api_data *data)
{
	__ASSERT_NO_MSG(data != NULL);
	if (data == NULL) {
		return -EINVAL;
	}

	if (request == MODEM_API_DATA_REQUEST_STATIC) {
		return quectel_bg95_get_static_info(dev, &data->modem_info);
	} else if (MODEM_API_DATA_REQUEST_DYNAMIC) {
		return quectel_bg95_modem_get_dynamic_info(dev, &data->modem_network);
	} else {
		return -ENOTSUP;
	}
}

static struct modem_api api_funcs = {
	.iface_api.init = modem_net_iface_init,

	.evt_handler_init = quectel_bg95_evt_handler_init,
	.set_credentials = quectel_bg95_set_credentials,
	.psm_cmd = quectel_bg95_psm_cmd,
	.get_data = quectel_bg95_get_data,
};

static bool offload_is_supported(int family, int type, int proto)
{
	return true;
}

static int offload_socket(int family, int type, int proto)
{
	int ret;

	/* defer modem's socket create call to bind() */
	ret = modem_socket_get(&mdata.socket_config, family, type, proto);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	errno = 0;
	return ret;
}

const struct device* modem_uart;

static int modem_init(const struct device *dev)
{
	int ret; ARG_UNUSED(dev);
	k_sem_init(&mdata.sem_response,	 0, 1);
	k_sem_init(&mdata.sem_ready,	 0, 1);
	k_sem_init(&mdata.sem_tx_ready,	 0, 1);
	k_sem_init(&mdata.sem_sock_conn, 0, 1);
	k_sem_init(&mdata.sem_dns_busy, 1, 1);
	k_sem_init(&mdata.sem_dns_ready, 0, 1);
	k_sem_init(&mdata.sem_data_ready, 0, 1);
	k_sem_init(&mdata.sem_shutdown, 0, 1);
	k_sem_init(&mdata.sem_ntp_ready, 0, 1);
	
	k_mutex_init(&mdata.mdm_network_mutex);

	k_work_queue_start(&modem_workq, modem_workq_stack,
			   K_KERNEL_STACK_SIZEOF(modem_workq_stack),
			   K_PRIO_COOP(7), NULL);

	/* socket config */
	ret = modem_socket_init(&mdata.socket_config, &mdata.sockets[0], ARRAY_SIZE(mdata.sockets),
				MDM_BASE_SOCKET_NUM, true, &offload_socket_fd_op_vtable);
	if (ret < 0) {
		goto error;
	}

	/* cmd handler setup */
	const struct modem_cmd_handler_config cmd_handler_config = {
		.match_buf = &mdata.cmd_match_buf[0],
		.match_buf_len = sizeof(mdata.cmd_match_buf),
		.buf_pool = &mdm_recv_pool,
		.alloc_timeout = BUF_ALLOC_TIMEOUT,
		.eol = "\r",
		.user_data = NULL,
		.response_cmds = response_cmds,
		.response_cmds_len = ARRAY_SIZE(response_cmds),
		.unsol_cmds = unsol_cmds,
		.unsol_cmds_len = ARRAY_SIZE(unsol_cmds),
	};

	ret = modem_cmd_handler_init(&mctx.cmd_handler, &mdata.cmd_handler_data,
				     &cmd_handler_config);
	if (ret < 0) {
		goto error;
	}

	/* modem interface */
	const struct modem_iface_uart_config uart_config = {
		.rx_rb_buf = &mdata.iface_rb_buf[0],
		.rx_rb_buf_len = sizeof(mdata.iface_rb_buf),
		.dev = MDM_UART_DEV,
		.hw_flow_control = DT_PROP(MDM_UART_NODE, hw_flow_control),
	};

	ret = modem_iface_uart_init(&mctx.iface, &mdata.iface_data, &uart_config);
	if (ret < 0) {
		goto error;
	}

	/* modem data storage */
	mctx.data_manufacturer = mdata.mdm_manufacturer;
	mctx.data_model	       = mdata.mdm_model;
	mctx.data_revision     = mdata.mdm_revision;
	mctx.data_imei	       = mdata.mdm_imei;
	mctx.data_rssi	       = &mdata.mdm_rssi;

	/* Set qual and RSSI to 99 (means not known/not connected) */
	mdata.mdm_qual = 99;
	mdata.mdm_rssi = MDM_RSSI_INVALID;

#if DT_INST_NODE_HAS_PROP(0, mdm_on_off_gpios)
	ret = gpio_pin_configure_dt(&on_off_gpio, GPIO_OUTPUT_LOW);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s pin", "on_off");
		goto error;
	}
#endif

	ret = gpio_pin_configure_dt(&power_gpio, GPIO_OUTPUT_LOW);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s pin", "power");
		goto error;
	}

#if DT_INST_NODE_HAS_PROP(0, mdm_pon_trig_gpios)
	ret = gpio_pin_configure_dt(&pon_trig_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s pin", "pon_trig");
		goto error;
	}
#endif

#if DT_INST_NODE_HAS_PROP(0, mdm_uart_oe_gpios)
	ret = gpio_pin_configure_dt(&uart_oe_gpio, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s pin", "uart_oe");
		goto error;
	}
#endif

#if DT_INST_NODE_HAS_PROP(0, mdm_wdisable_gpios)
	ret = gpio_pin_configure_dt(&wdisable_gpio, GPIO_OUTPUT_LOW);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s pin", "wdisable");
		goto error;
	}
#endif

#if DT_INST_NODE_HAS_PROP(0, mdm_reset_gpios)
	ret = gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_LOW);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s pin", "reset");
		goto error;
	}
#endif

#if DT_INST_NODE_HAS_PROP(0, mdm_dtr_gpios)
	ret = gpio_pin_configure_dt(&dtr_gpio, GPIO_OUTPUT_LOW);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s pin", "dtr");
		goto error;
	}
#endif

#if DT_INST_NODE_HAS_PROP(0, mdm_wdisable_gpios)
	ret = gpio_pin_configure_dt(&wdisable_gpio, GPIO_OUTPUT_LOW);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s pin", "wdisable");
		goto error;
	}
#endif

	mctx.driver_data       = &mdata;

	ret = modem_context_register(&mctx);
	if (ret < 0) {
		LOG_ERR("Error registering modem context: %d", ret);
		goto error;
	}

	/* start RX thread */
	k_thread_create(&modem_rx_thread, modem_rx_stack,
			K_KERNEL_STACK_SIZEOF(modem_rx_stack),
			(k_thread_entry_t) modem_rx,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);

	/* Init RSSI query */
	k_work_init_delayable(&mdata.rssi_query_work, modem_rssi_query_work);
	k_work_init(&mdata.dynamic_data_update_work, modem_dynamic_update_work);
#ifdef CONFIG_MODEM_QUECTEL_BG95_PSM
	/* Init PSM work */
	k_work_init(&mdata.psm_wakeup_work, modem_psm_wakeup_work);
	setup_psm_ind_interrupt();
#endif
	return modem_setup();

error:
	return ret;
}

int quectel_bg95_get_psm_timers(void)
{
	char *sendcmd = "AT+QPSMS?";
	int ret;

	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, NULL, 0u, sendcmd,
			     &mdata.sem_response, MDM_CMD_TIMEOUT);	
	if (ret < 0) {
		LOG_WRN("Error getting PSM parameters");
	}
	
	return ret;
}

int quectel_bg95_psm_wakeup(void)
{
	int ret = -1;

#if DT_INST_NODE_HAS_PROP(0, mdm_pon_trig_gpios)
	LOG_DBG("Sending wakeup signal to modem");
	ret = gpio_pin_set_dt(&pon_trig_gpio, 1);
	k_sleep(K_MSEC(40));
	ret = gpio_pin_set_dt(&pon_trig_gpio, 0);

	return ret;
#else
	ret = gpio_pin_set_dt(&power_gpio, 1);
	k_sleep(K_MSEC(1000));
	ret = gpio_pin_set_dt(&power_gpio, 0);
	
	return ret;
#endif
}

#ifdef CONFIG_PM_DEVICE
static int quectel_bg95_pm_suspend(void)
{
	int ret;
	
	LOG_INF("PM_DEVICE_ACTION_SUSPEND");

	/* stop RSSI delay work */
	k_work_cancel_delayable(&mdata.rssi_query_work);

	ret = quectel_bg95_power_down();
	if (ret != 0) {
		return -EAGAIN;
	}

#if DT_INST_NODE_HAS_PROP(0, mdm_uart_oe_gpios)
	gpio_pin_set_dt(&uart_oe_gpio, GPIO_OUTPUT_INACTIVE);
#endif

	ret = pm_suspend_uart();
	if (ret) {
		return ret;
	}

	return 0;
}

static int quectel_bg95_pm_resume(void)
{
	int ret = 0;
	LOG_INF("PM_DEVICE_ACTION_RESUME");

#if DT_INST_NODE_HAS_PROP(0, mdm_uart_oe_gpios)
	gpio_pin_set_dt(&uart_oe_gpio, GPIO_OUTPUT_ACTIVE);
#endif

	ret = pm_resume_uart();
	if (ret) {
		return ret;
	}
	ret = modem_setup();

	return ret;
}

static int quectel_bg95_pm_action(const struct device *dev,
			       enum pm_device_action action)
{
	ARG_UNUSED(dev);
	int ret;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		/* device must be uninitialized */
		ret = quectel_bg95_pm_suspend();
		break;
	case PM_DEVICE_ACTION_RESUME:
		/* device must be reinitialized */
		ret = quectel_bg95_pm_resume();
		break;
	default:
		return -ENOTSUP;
	}

	return ret;
}

PM_DEVICE_DT_INST_DEFINE(0, quectel_bg95_pm_action);

/* Register the device with the Networking stack. */
NET_DEVICE_DT_INST_OFFLOAD_DEFINE(0, modem_init, PM_DEVICE_DT_INST_GET(0),
				  &mdata, NULL,
				  CONFIG_MODEM_QUECTEL_BG95_M3_INIT_PRIORITY,
				  &api_funcs, MDM_MAX_DATA_LENGTH);
#else
/* Register the device with the Networking stack. */
NET_DEVICE_DT_INST_OFFLOAD_DEFINE(0, modem_init, NULL,
				  &mdata, NULL,
				  CONFIG_MODEM_QUECTEL_BG95_M3_INIT_PRIORITY,
				  &api_funcs, MDM_MAX_DATA_LENGTH);
#endif

/* Register NET sockets. */
NET_SOCKET_OFFLOAD_REGISTER(quectel_bg95, CONFIG_NET_SOCKETS_OFFLOAD_PRIORITY,
			    AF_UNSPEC, offload_is_supported, offload_socket);


char* quectel_bg95_get_imei(void) {
	return mdata.mdm_imei;
}
char* quectel_bg95_get_revision(void) {
	return mdata.mdm_revision;
}

char* quectel_bg95_get_sim_number(void) {
#if defined(CONFIG_MODEM_QUECTEL_BG95_M3_SIM_NUMBERS)
	return mdata.mdm_iccid;
#endif
	return "N.A";
}

MODEM_CMD_DEFINE(on_cmd_atcmdinfo_clock)
{
#define QNTP_FORMAT_OFFSET ("#,")
	int out_len = net_buf_linearize(mdata.mdm_time, sizeof(mdata.mdm_time) - 1, data->rx_buf, 
		sizeof(QNTP_FORMAT_OFFSET) - 1, len - sizeof(QNTP_FORMAT_OFFSET));
	if (out_len <= 0) {
		errno = -out_len;
		mdata.mdm_time[0] = '\0';
		modem_cmd_handler_set_error(data, -1);
		return -1;
	}
	mdata.mdm_time[out_len] = '\0';
	LOG_DBG("Clock: %s", mdata.mdm_time);
	k_sem_give(&mdata.sem_ntp_ready);
	return 0;
}

int quectel_bg95_get_time(char* time_buf) {
	if (!mdata.is_connected) {
		return -1;
	}

	char   sendbuf[sizeof("AT+QNTP=1,") + 64] = {0};
	int    ret;
	static const struct modem_cmd cmd = MODEM_CMD("+QNTP: ", on_cmd_atcmdinfo_clock, 0, ",");
	snprintk(sendbuf, sizeof(sendbuf), "AT+QNTP=1,\"%s\"", CONFIG_MODEM_NTP_SERVER);

	
	/* query modem clock */
	ret = modem_cmd_send(&mctx.iface, &mctx.cmd_handler, &cmd, 1U, sendbuf,
			     &mdata.sem_ntp_ready, MDM_NTP_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("AT+QNTP ret:%d", ret);
		return ret;
	}

	memcpy(time_buf, mdata.mdm_time, sizeof(mdata.mdm_time));
	return 0;
}

bool quectel_bg95_is_ready(void) {
	return mdata.is_connected;
}

int quectel_bg95_get_rssi(void)
{
	return mdata.mdm_rssi;
}

int quectel_bg95_get_qual(void)
{
	return mdata.mdm_qual;
}
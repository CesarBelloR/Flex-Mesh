#include <zephyr/kernel.h>

#include "quectel-bg95_gnss.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(modem_quectel_bg95_gnss, CONFIG_MODEM_LOG_LEVEL);

/* Commands sent to the modem to set it up at boot time. */
static const struct setup_cmd gnss_setup_cmds[] = {
	/* Send NMEA messages to GNSS UART */
	SETUP_CMD_NOHANDLE("AT+QGPSCFG=\"outport\",\"uartnmea\""),
	/* Set NMEA format to NMEA 0183 version 4.10 */
	SETUP_CMD_NOHANDLE("AT+QGPSCFG=\"nmeafmt\",1"),
	SETUP_CMD_NOHANDLE("AT+QGPSXTRA=1"),
	SETUP_CMD_NOHANDLE("AT+QGPSCFG=\"gpsnmeatype\",3")
};

struct gnss_data {
	enum gnss_status status;
} gnss_data;

int quectel_bg95_gnss_setup(struct modem_context *mctx, struct modem_data *mdata)
{
	int ret;

	/* Run GNSS setup commands on the modem. */
	ret = modem_cmd_handler_setup_cmds(&mctx->iface, &mctx->cmd_handler,
					   gnss_setup_cmds, ARRAY_SIZE(gnss_setup_cmds),
					   &mdata->sem_response, MDM_REGISTRATION_TIMEOUT);
	return ret;
}

int quectel_bg95_turn_on_gnss(struct modem_context *mctx, struct modem_data *mdata,
			      struct quectel_bg95_gnss_cfg *cfg)
{
	int ret;
	static char send_cmd[sizeof("AT+QGPS=1,3,####,#####")];

	if (cfg->fix_count > 1000 || cfg->fix_rate < 1) {
		return -EINVAL;
	}

	snprintk(send_cmd, sizeof(send_cmd), "AT+QGPS=1,1,%u,%u",
		 cfg->fix_count, cfg->fix_rate);

	ret = modem_cmd_send(&mctx->iface, &mctx->cmd_handler,
			     NULL, 0U, send_cmd, &mdata->sem_response,
			     MDM_CMD_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s err %d", send_cmd, ret);
	} else {
		gnss_data.status = GNSS_ON;
	}
	
	return ret;
}

int quectel_bg95_turn_off_gnss(struct modem_context *mctx, struct modem_data *mdata)
{
	int ret;
	static char *send_cmd = "AT+QGPSEND";

	ret = modem_cmd_send(&mctx->iface, &mctx->cmd_handler,
			     NULL, 0U, send_cmd, &mdata->sem_response,
			     MDM_CMD_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("%s err %d", send_cmd, ret);
	} else {
		gnss_data.status = GNSS_OFF;
	}
	
	return ret;
}
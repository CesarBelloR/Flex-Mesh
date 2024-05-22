#ifndef QUECTEL_BG95_GNSS_H_
#define QUECTEL_BG95_GNSS_H_

#include <stdint.h>

#include "quectel-bg95.h"


enum gnss_status {
	GNSS_OFF = 0,
	GNSS_ON
};

struct quectel_bg95_gnss_cfg {
	uint16_t fix_count;
	uint16_t fix_rate;
};

/**
 * Run GNSS setup commands on the modem. This should be done on modem power on.
 *
 * @param ctx Current modem context
 * @param mdata Current modem data
 * @retval 0 success
 * @retval <0 error
*/
int quectel_bg95_gnss_setup(struct modem_context *mctx, struct modem_data *mdata);

/**
 * Send a command to modem to turn on GNSS
 *
 * @param ctx Current modem context
 * @param mdata Current modem data
 * @param cfg GNSS configuration
*/
int quectel_bg95_turn_on_gnss(struct modem_context *mctx, struct modem_data *mdata,
			      struct quectel_bg95_gnss_cfg *cfg);

/**
 * Send a command to modem to turn off GNSS
 *
 * @param ctx Current modem context
 * @param mdata Current modem data
 * @param cfg GNSS configuration
*/
int quectel_bg95_turn_off_gnss(struct modem_context *mctx, struct modem_data *mdata);

/**
 * Suspend GNSS. This runs a suspend on the gnss_generic_nmea device that uses
 * the modem's GNSS UART.
 * 
 * @retval 0 success
 * @retval -ENOTSUP PM not enabled
 * @retval <0 error
*/
int pm_suspend_gnss(void);

/**
 * Resume GNSS. This runs a resume on the gnss_generic_nmea device that uses
 * the modem's GNSS UART.
 * 
 * @retval 0 success
 * @retval -ENOTSUP PM not enabled
 * @retval <0 error
*/
int pm_resume_gnss(void);

#endif

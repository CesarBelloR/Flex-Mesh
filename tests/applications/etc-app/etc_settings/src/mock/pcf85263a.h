/***************************************************************************/
/*!
\file       PCF85263A.h
\brief      Real-time clock/calendar with alarm function, battery switch-over
	    and timestamp input application prototype interface (API)

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef PCF85263A_H_
#define PCF85263A_H_

#include <stdint.h>
#include <zephyr/sys/timeutil.h>
#include <time.h>

/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes the PCF85263A RTC
 *
 * @param device the I2C channel such as I2C_0, I2C_1
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int pcf85263a_init(const char *device);


/** Set the RTC offset to improve accuracy.
 *
 * @param offset_ppm Pointer to RTC clock offset in ppm derived through calibration process.
 *
 * @retval 0: successful
 * @retval <0: error
 */
int pcf85263a_set_offset(float *offset_ppm);
#endif /* PCF85263A_H_ */
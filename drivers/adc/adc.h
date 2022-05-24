/***************************************************************************/
/*!
\file       adc.h
\brief      ADC Driver to read analog data

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef ADC_H_
#define ADC_H_

#include <stdint.h>
#include <sys/timeutil.h>

/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
#define ADC_MODULE_MAX_CHANNEL (5)
/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes the ADC driver
 *
 * @retval return 0 on success
 */
int adc_init(void);

/** @brief Get raw ADC value
 *
 * @param channel input from 1 to 6
 * @retval return raw ADC value with 12 bit resolution
 */
int adc_get_channel(int channel);
#endif /* ADC_H_ */
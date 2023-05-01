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
#include <zephyr/sys/timeutil.h>

/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
#define ADC_NUM_CHANNELS    	DT_PROP_LEN(DT_PATH(zephyr_user), io_channels)
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
 * @param channel input from 0 to ( @a ADC_NUM_CHANNELS - 1)
 * @retval return raw ADC value with 12 bit resolution
 */
int adc_get_channel(int channel);

/**
 * @brief Convert the given raw ADC count to a voltage in millivolts.
 * 
 * @param channel ADC channel that the count was measured on
 * @param raw Pointer to raw ADC. Is converted to millivolts on success.
 * @retval 0 on success, negative on error.
*/
int adc_get_raw_to_millivolts(int channel, int* raw);

/**
 * @ref Get the full scale (maximum measureable) voltage on the specified channel.
 * 
 * @param channel input from 0 to @a ADC_NUM_CHANNELS
 * @retval Full scale voltage in millivolts, 0 if error.
*/
int adc_get_full_scale_voltage_mv(int channel);
/**
 * @ref Get the full scale (maximum) raw count on the specified channel.
 * 
 * @param channel input from 0 to @a ADC_NUM_CHANNELS
 * @retval Full scale count, 0 if error.
*/
int adc_get_full_scale_count(int channel);

#endif /* ADC_H_ */
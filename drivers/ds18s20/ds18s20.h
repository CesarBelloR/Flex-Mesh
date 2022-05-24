/***************************************************************************/
/*!
\file       ds18s20.h
\brief      DS18S20 - Temperature and Humidity sensor

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef DS18S20_H_
#define DS18S20_H_

#include <stdint.h>
#include <stdbool.h>
/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
#define DS18S20_ROM_ID_TYPE_1 (0x10)
#define DS18S20_ROM_ID_TYPE_2 (0x28)

/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes the DS18S20 driver
 *
 * @retval return 0 on success
 */
int ds18s20_init(void);

/** @brief Get the DS18S20 temperature sensor
 *
 * @retval return the current temperature
 */
int ds18s20_get_temperature(void);

/** @brief Get the DS18S20 humidity sensor
 *
 * @retval return the current humidity
 */
int ds18s20_get_humidity(void);
#endif /* DS18S20_H_ */
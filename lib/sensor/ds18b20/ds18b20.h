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
#ifndef	_DS18B20_H
#define	_DS18B20_H

#include <stdbool.h>
#include <zephyr/kernel.h>
#include "onewire.h"

/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
#define	DS18B20_DELAY(x)			k_msleep(x)
#define DS18B20_MAX_SENSORS			1
#define	DS18B20_CONVERT_TIMEOUT_MS	5000

typedef struct
{
	uint8_t 	address[8];
	float 		temperature;
	bool		data_is_valid;
} ds18b20_sensor_t;

extern ds18b20_sensor_t	ds18b20[DS18B20_MAX_SENSORS];

/* Every onewire chip has different ROM code, but all the same chips has same family code */
/* in case of DS18B20 this is 0x28 and this is first byte of ROM address */
#define DS18B20_FAMILY_CODE						0x28
#define DS18B20_CMD_ALARMSEARCH				0xEC

/* DS18B20 read temperature command */
#define DS18B20_CMD_CONVERTTEMP			0x44 	
#define DS18B20_DECIMAL_STEPS_12BIT		0.0625
#define DS18B20_DECIMAL_STEPS_11BIT		0.125
#define DS18B20_DECIMAL_STEPS_10BIT		0.25
#define DS18B20_DECIMAL_STEPS_9BIT		0.5

/* Bits locations for resolution */
#define DS18B20_RESOLUTION_R1					6
#define DS18B20_RESOLUTION_R0					5

/* CRC enabled */
#ifdef DS18B20_USE_CRC
#define DS18B20_DATA_LEN							9
#else
#define DS18B20_DATA_LEN							2
#endif

typedef enum {
	DS18B20_RESOLUTION_9BITS = 9,   /*!< DS18B20 9 bits resolution */
	DS18B20_RESOLUTION_10BITS = 10, /*!< DS18B20 10 bits resolution */
	DS18B20_RESOLUTION_11BITS = 11, /*!< DS18B20 11 bits resolution */
	DS18B20_RESOLUTION_12BITS = 12  /*!< DS18B20 12 bits resolution */
} ds18b20_resolution_e;

/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes the DS18S20 driver
 *
 * @retval return true on success
 */
bool        ds18b20_init(void);

/** @brief Set manual converter
 *
 * @retval return true on success
 */
bool		ds18b20_manualconvert(void);

/** @brief Start with specific ROM
 *
 * @param rom specific ROM to start
 * @retval return 0 on success
 */
int 	    ds18b20_start(onewire_t* one_wire_struct, uint8_t* rom);
void 		ds18b20_startall(onewire_t* one_wire_struct);
bool		ds18b20_read(onewire_t* one_wire_struct, uint8_t* rom, float* destination);
uint8_t 	ds18b20_getresolution(onewire_t* one_wire_struct, uint8_t* rom);
uint8_t 	ds18b20_setresolution(onewire_t* one_wire_struct, uint8_t* rom, ds18b20_resolution_e resolution);
uint8_t 	ds18b20_is(uint8_t* rom);
uint8_t 	ds18b20_setalarmhightemperature(onewire_t* one_wire_struct, uint8_t* rom, int8_t temp);
uint8_t 	ds18b20_setalarmlowtemperature(onewire_t* one_wire_struct, uint8_t* rom, int8_t temp);
uint8_t 	ds18b20_disablealarmtemperature(onewire_t* one_wire_struct, uint8_t* rom);
uint8_t 	ds18b20_alarmsearch(onewire_t* one_wire_struct);
uint8_t 	ds18b20_alldone(onewire_t* one_wire_struct);

#ifdef __cplusplus
}
#endif

#endif

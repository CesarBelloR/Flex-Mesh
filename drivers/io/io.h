/***************************************************************************/
/*!
\file       io.h
\brief      GPIO interface header to process interrupt input, button input 
    or normal output.

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef IO_H_
#define IO_H_

#include <stdint.h>
#include <stdbool.h>
/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
typedef enum {
    IO_SENSE_ENABLE, 
    IO_VSENSE_ENABLE, 
    IO_SENS_SEL0,
    IO_SENS_SEL1,
    IO_RTC_INT,
    IO_BATT_INT,
    IO_NUM,
} io_id_t;
/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes the IO driver
 *
 * @retval 0 if success
 */
int io_init(void);

/** @brief Get the IO status for input or current output level
 *
 * @param id @ref io_id_t
 * @retval HIGH or LOW
 */
int  io_get(io_id_t id);

/** @brief Set the IO output
 *
 * @param id ref @io_id_t
 * @param value HIGH or LOW level
 * @retval None
 */
void io_set(io_id_t id, int value);
#endif
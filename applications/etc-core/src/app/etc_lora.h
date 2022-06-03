/***************************************************************************/
/*!
\file       etc_lora.h
\brief      Lora management application

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef ETC_LORA_H_
#define ETC_LORA_H_

#include <stdint.h>
#include <stdbool.h>
/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes Lora
 *
 * @param None
 * @retval Zero if success
 */
int etc_lora_init(void);

/** @brief Send data over Lora
 *
 * @param data point to where data to be sent
 * @param length length of sending data.
 * @retval Zero if success
 */
int etc_lora_send(void* data, int length);
#endif /* ETC_LORA_H_ */
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
int etc_lora_init(void);

int etc_lora_send(void* data, int length);
#endif /* ETC_LORA_H_ */
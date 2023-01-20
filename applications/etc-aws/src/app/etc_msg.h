/***************************************************************************/
/*!
\file       etc_msg.h
\brief      Message for MQTT service

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef ETC_MSG_H_
#define ETC_MSG_H_

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
#define ETC_MSG_VERSION_LEN (8)

typedef struct {
    uint32_t sensor_id;
    uint32_t time;
    float bat;
    uint8_t rssi;
    char version[ETC_MSG_VERSION_LEN];
    uint8_t packet;
    float v1, v2, v3, v4, v5, v6;
} __packed etc_msg_struct_t;

/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Create the message heart-beat/reading
 *   based on EXACT Relay/Logger 2.0 Docs
 * 
 * @param None
 * @retval A message json. Notice: Need to free message after sending to MQTT.
 */
char* etc_msg_generator(void);
#endif /* ETC_MSG_H_ */
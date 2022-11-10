/***************************************************************************/
/*!
\file       etc_app.h
\brief      ETC application (relay/logger)

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef ETC_APP_H_
#define ETC_APP_H_

#include <stdint.h>
#include <stdbool.h>
/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes application mode
 *
 * @param None
 * @retval Zero if success
 */
int etc_app_init(void);

/** @brief Run the application (logger/relay) mode
 *
 * @retval Zero if success
 */
int etc_app_run(void);
#endif /* ETC_APP_H_ */
/***************************************************************************/
/*!
\file       etc_aws.h
\brief      AWS service application

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef ETC_AWS_H_
#define ETC_AWS_H_

#include <stdint.h>
#include <stdbool.h>
/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes the AWS Service
 *
 * @param None
 * @retval Zero if success
 */
int etc_aws_init(void);
#endif /* ETC_AWS_H_ */
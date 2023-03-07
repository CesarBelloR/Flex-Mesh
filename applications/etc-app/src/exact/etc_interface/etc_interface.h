#ifndef ETC_INTERFACE_H_
#define ETC_INTERFACE_H_

#include <zephyr/types.h>
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*etc_interface_event_handler)(void);

void etc_interface_enable_rtc_event(void);
void etc_interface_disable_rtc_event(void);
void etc_interface_register_event_handler(etc_interface_event_handler handler);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* ETC_INTERFACE_H_ */

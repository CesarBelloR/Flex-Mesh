#ifndef ETC_INTERFACE_H_
#define ETC_INTERFACE_H_

#include <zephyr/types.h>
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Define event type based on the input type */
enum etc_interface_event_type {
        ETC_INTERFACE_EVENT_BUTTON,
        ETC_INTERFACE_EVENT_HALL,
        ETC_INTERFACE_EVENT_RTC,
        ETC_INTERFACE_EVENT_UNKNOWN,
};

typedef void (*etc_interface_event_handler)(enum etc_interface_event_type type);

void etc_interface_enable_rtc_event(void);
void etc_interface_disable_rtc_event(void);
void etc_interface_register_event_handler(etc_interface_event_handler handler);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* ETC_INTERFACE_H_ */

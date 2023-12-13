#ifndef WATCHDOG_H_
#define WATCHDOG_H_

#include <stdint.h>

void etc_watchdog_feed(void);
void etc_watchdog_set_timeout(uint16_t timeout);
void etc_watchdog_start_work(void);
bool etc_watchdog_stop_work(void);

#endif /* WATCHDOG_H_ */
#include "date_time.h"
#include <zephyr.h>
#include <zephyr/types.h>
#include <device.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/timeutil.h>
#include <posix/time.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(date_time, CONFIG_RDB_LOG_LEVEL);

extern int quectel_bg95_get_time(char* time_buf);

#define AT_CMD_MODEM_DATE_TIME_RESPONSE_LEN	32

/* The magic number 115 corresponds to the year 2015 which is the default
 * year given by the modem when the cellular network has not pushed time
 * the modem.
 */
#define MODEM_TIME_DEFAULT 115
#define DATE_TIME_INVALID_PULL_TIMEOUT_SECOND (30)

K_SEM_DEFINE(time_fetch_sem, 0, 1);

static struct k_work_delayable time_work;

static struct time_aux {
	int64_t date_time_utc;
	int64_t last_date_time_update;
	int     time_zone;
} time_aux;

static bool initial_valid_time;
static date_time_evt_handler_t app_evt_handler;

static struct date_time_evt evt;

static void date_time_notify_event(const struct date_time_evt *evt)
{
	__ASSERT(evt != NULL, "Library event not found");

	if (app_evt_handler != NULL) {
		app_evt_handler(evt);
	}
}

static void date_time_print_datetime(struct tm *tm_time)
{
 LOG_DBG("Datetime: %4d-%02d-%02d (wday=%d)  TIME: %2d:%02d:%02d", tm_time->tm_year - 100,
									tm_time->tm_mon, tm_time->tm_mday, tm_time->tm_wday, tm_time->tm_hour, tm_time->tm_min,
									tm_time->tm_sec);
}

static int time_modem_get(void)
{
	char buf[AT_CMD_MODEM_DATE_TIME_RESPONSE_LEN + 1];
	struct tm date_time;

	int ret = quectel_bg95_get_time(buf);
 	if (ret != 0) {
 		return -ENODATA;
 	}

	/* This line zero indexes the buffer at the desired length
	 * This ensures clean printing of the modem response.
	 */
	buf[AT_CMD_MODEM_DATE_TIME_RESPONSE_LEN - 4] = '\0';

	/* Example of modem time response:
	 * "20/02/25,17:15:02+04"
	 */
	LOG_DBG("Response from modem: %s", (buf));

	/* Replace '/' ',' and ':' with whitespace for easier parsing by strtol.
	 * strtol skips over whitespace.
	 */
	for (int i = 0; i < AT_CMD_MODEM_DATE_TIME_RESPONSE_LEN; i++) {
		if (buf[i] == '/' || buf[i] == ',' || buf[i] == ':') {
			buf[i] = ' ';
		}
	}
	
	char *ptr_index = &buf[1];
	char *ptr_end = NULL;
	int base = 10;

	date_time.tm_year = strtol(ptr_index, &ptr_end, base) + 2000 - 1900;
	ptr_end += 1;
	ptr_index = ptr_end;
	date_time.tm_mon = strtol(ptr_index, &ptr_end, base) - 1;
	ptr_end += 1;
	ptr_index = ptr_end;
	date_time.tm_mday = strtol(ptr_index, &ptr_end, base);
	ptr_end += 1;
	ptr_index = ptr_end;
	date_time.tm_hour = strtol(ptr_index, &ptr_end, base);
	ptr_end += 1;
	ptr_index = ptr_end;
	date_time.tm_min = strtol(ptr_index, &ptr_end, base);
	ptr_end += 1;
	ptr_index = ptr_end;
	date_time.tm_sec = strtol(ptr_index, &ptr_end, base);
	if (ptr_end[0] == '+') {
		ptr_end += 1;
		ptr_index = ptr_end;
		time_aux.time_zone = strtol(ptr_index, &ptr_end, base) * 60 * 60 / 4;
	} else {
		ptr_end += 1;
		ptr_index = ptr_end;
		time_aux.time_zone = -(strtol(ptr_index, &ptr_end, base) * 60 * 60 / 4);
	}
	if (date_time.tm_year == MODEM_TIME_DEFAULT) {
		LOG_DBG("Modem time never set");
		return -ENODATA;
	}
	date_time_print_datetime(&date_time);
	time_aux.date_time_utc = (int64_t)timeutil_timegm64(&date_time) * 1000;
	LOG_DBG("Time UTC %d - Time Zone %d - Local Time %d", (int)(time_aux.date_time_utc / 1000), time_aux.time_zone,
		(int)(time_aux.date_time_utc / 1000 + time_aux.time_zone));
	
	time_aux.last_date_time_update = k_uptime_get();
	return 0;
}

static int current_time_check(void)
{
	if (time_aux.last_date_time_update == 0 ||
	    time_aux.date_time_utc == 0) {
		LOG_DBG("Date time never set");
		return -ENODATA;
	}

	if ((k_uptime_get() - time_aux.last_date_time_update) >
	    CONFIG_DATE_TIME_UPDATE_INTERVAL_SECONDS * 1000) {
		LOG_DBG("Current date time too old");
		return -ENODATA;
	}

	return 0;
}

static void date_time_store(int64_t curr_time_ms)
{
	struct timespec tp = { 0 };
	struct tm ltm = { 0 };
	int ret;

	tp.tv_sec = curr_time_ms / 1000;
	tp.tv_nsec = (curr_time_ms % 1000) * 1000000;

	ret = clock_settime(CLOCK_REALTIME, &tp);
	if (ret != 0) {
		LOG_ERR("Could not set system time, %d", ret);
		return;
	}
	gmtime_r(&tp.tv_sec, &ltm);
	LOG_DBG("System time updated: %04u-%02u-%02u %02u:%02u:%02u",
		ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
		ltm.tm_hour, ltm.tm_min, ltm.tm_sec);
}

static void new_date_time_get(void)
{
	int err;

	while (true) {
		k_sem_take(&time_fetch_sem, K_FOREVER);

		LOG_DBG("Updating date time UTC...");

		err = current_time_check();
		if (err == 0) {
			LOG_DBG("Time successfully obtained");
			initial_valid_time = true;
			date_time_notify_event(&evt);
			continue;
		}

		LOG_DBG("Current time not valid");
		err = time_modem_get();
		if (err == 0) {
			LOG_DBG("Time from cellular network obtained");
			initial_valid_time = true;
			date_time_store(time_aux.date_time_utc);
			evt.type = DATE_TIME_OBTAINED_MODEM;
			date_time_notify_event(&evt);
			continue;
		}

		LOG_DBG("Not getting cellular network time");

		evt.type = DATE_TIME_NOT_OBTAINED;
		date_time_notify_event(&evt);
	}
}

K_THREAD_DEFINE(time_thread, CONFIG_DATE_TIME_THREAD_SIZE,
		new_date_time_get, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

static void date_time_handler(struct k_work *work)
{
	if (CONFIG_DATE_TIME_UPDATE_INTERVAL_SECONDS > 0) {
		k_sem_give(&time_fetch_sem);

		LOG_DBG("New date time update in: %d seconds",
			CONFIG_DATE_TIME_UPDATE_INTERVAL_SECONDS);

		if (date_time_is_valid()) {
 			k_work_reschedule(&time_work, K_SECONDS(CONFIG_DATE_TIME_UPDATE_INTERVAL_SECONDS));
			LOG_DBG("New date time update in: %d seconds",
 						CONFIG_DATE_TIME_UPDATE_INTERVAL_SECONDS);
 		} else {
 			k_work_reschedule(&time_work, K_SECONDS(DATE_TIME_INVALID_PULL_TIMEOUT_SECOND));
			LOG_DBG("New date time update in: %d seconds",
 						DATE_TIME_INVALID_PULL_TIMEOUT_SECOND);
 		}
	}
}

static int date_time_init(const struct device *unused)
{
	k_work_init_delayable(&time_work, date_time_handler);
	return 0;
}

int date_time_set(const struct tm *new_date_time)
{
	int err = 0;

	if (new_date_time == NULL) {
		LOG_ERR("The passed in pointer cannot be NULL");
		return -EINVAL;
	}

	/** Seconds after the minute. tm_sec is generally 0-59.
	 *  The extra range is to accommodate for leap seconds
	 *  in certain systems.
	 */
	if (new_date_time->tm_sec < 0 || new_date_time->tm_sec > 61) {
		LOG_ERR("Seconds in time structure not in correct format");
		err = -EINVAL;
	}

	/** Minutes after the hour. */
	if (new_date_time->tm_min < 0 || new_date_time->tm_min > 59) {
		LOG_ERR("Minutes in time structure not in correct format");
		err = -EINVAL;
	}

	/** Hours since midnight. */
	if (new_date_time->tm_hour < 0 || new_date_time->tm_hour > 23) {
		LOG_ERR("Hours in time structure not in correct format");
		err = -EINVAL;
	}

	/** Day of the month. */
	if (new_date_time->tm_mday < 1 || new_date_time->tm_mday > 31) {
		LOG_ERR("Day in time structure not in correct format");
		err = -EINVAL;
	}

	/** Months since January. */
	if (new_date_time->tm_mon < 0 || new_date_time->tm_mon > 11) {
		LOG_ERR("Month in time structure not in correct format");
		err = -EINVAL;
	}

	/** Years since 1900. 115 corresponds to the year 2015. */
	if (new_date_time->tm_year < 115 || new_date_time->tm_year > 1900) {
		LOG_ERR("Year in time structure not in correct format");
		err = -EINVAL;
	}

	/** Days since Sunday. */
	if (new_date_time->tm_wday < 0 || new_date_time->tm_wday > 6) {
		LOG_ERR("Week day in time structure not in correct format");
		err = -EINVAL;
	}

	/** Days since January 1. */
	if (new_date_time->tm_yday < 0 || new_date_time->tm_yday > 365) {
		LOG_ERR("Year day in time structure not in correct format");
		err = -EINVAL;
	}

	if (err) {
		return err;
	}

	initial_valid_time = true;
	time_aux.last_date_time_update = k_uptime_get();
	time_aux.date_time_utc = (int64_t)timeutil_timegm64(new_date_time) * 1000;

	evt.type = DATE_TIME_OBTAINED_EXT;
	date_time_notify_event(&evt);

	return 0;
}

int date_time_set_second(uint32_t new_date_time_sec) {
	return pcf85263a_rtc_set_time((time_t)new_date_time_sec);
}

int date_time_uptime_to_unix_time_ms(int64_t *uptime)
{
	int64_t uptime_prev;

	if (uptime == NULL) {
		LOG_ERR("The passed in pointer cannot be NULL");
		return -EINVAL;
	}

	uptime_prev = *uptime;

	if (!initial_valid_time) {
		LOG_WRN("Valid time not currently available");
		return -ENODATA;
	}

	*uptime += time_aux.date_time_utc - time_aux.last_date_time_update;
	
	/** Check if the passed in uptime was allready converted,
	 * meaning that after a second conversion it is greater than the
	 * current date time UTC.
	 */
	if (*uptime > time_aux.date_time_utc +
	    (k_uptime_get() - time_aux.last_date_time_update)) {
		LOG_WRN("Uptime to large or previously converted");
		LOG_WRN("Clear variable or set a new uptime");
		*uptime = uptime_prev;
		return -EINVAL;
	}

	return 0;
}

int date_time_now(int64_t *unix_time_ms)
{
	int err;
	int64_t unix_time_ms_prev;

	if (unix_time_ms == NULL) {
		LOG_ERR("The passed in pointer cannot be NULL");
		return -EINVAL;
	}

	unix_time_ms_prev = *unix_time_ms;

	*unix_time_ms = k_uptime_get();

	err = date_time_uptime_to_unix_time_ms(unix_time_ms);
	if (err) {
		LOG_WRN("date_time_uptime_to_unix_time_ms, error: %d", err);
		*unix_time_ms = unix_time_ms_prev;
	}

	return err;
}

int date_time_local_second(uint32_t *local_time_s)
{
	int64_t unix_time_ms = 0;
	*local_time_s = 0;
	int ret = date_time_now(&unix_time_ms);
	if (ret == 0) {
		*local_time_s = (unix_time_ms/ 1000) + date_time_get_timezone();
	}
	return ret;
}

int date_time_now_second(uint32_t *unix_time_s)
{
	int64_t unix_time_ms = 0;
	*unix_time_s = 0;
	int ret = date_time_now(&unix_time_ms);
	if (ret == 0) {
		*unix_time_s = unix_time_ms/ 1000;
	}
	return ret;
}

bool date_time_is_valid(void)
{
	return initial_valid_time;
}

int date_time_get_timezone(void)
{
	if(date_time_is_valid()){
		return time_aux.time_zone;
	}
	return -1;
}

void date_time_register_handler(date_time_evt_handler_t evt_handler)
{
	if (evt_handler == NULL) {
		app_evt_handler = NULL;

		LOG_DBG("Previously registered handler %p de-registered",
			app_evt_handler);

		return;
	}

	LOG_DBG("Registering handler %p", evt_handler);

	app_evt_handler = evt_handler;
}

int date_time_update_async(date_time_evt_handler_t evt_handler)
{
	if (evt_handler) {
		app_evt_handler = evt_handler;
	} else if (app_evt_handler == NULL) {
		LOG_DBG("No handler registered");
	}

	k_sem_give(&time_fetch_sem);

	return 0;
}

int date_time_clear(void)
{
	time_aux.date_time_utc = 0;
	time_aux.last_date_time_update = 0;
	initial_valid_time = false;

	return 0;
}

int date_time_timestamp_clear(int64_t *unix_timestamp)
{
	if (unix_timestamp == NULL) {
		LOG_ERR("The passed in pointer cannot be NULL");
		return -EINVAL;
	}

	*unix_timestamp = 0;

	return 0;
}

void date_time_start_work(void)
{
	k_work_reschedule(&time_work, K_NO_WAIT);
}

SYS_INIT(date_time_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

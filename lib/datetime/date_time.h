#ifndef DATE_TIME_H_
#define DATE_TIME_H_

#include <zephyr/types.h>
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Date time notification event types used to signal the application. */
enum date_time_evt_type {
	/** Date time library has obtained valid time from the modem. */
	DATE_TIME_OBTAINED_MODEM,
	/** Date time library has obtained valid time from external source. */
	DATE_TIME_OBTAINED_EXT,
	/** Date time library does not have valid time. */
	DATE_TIME_NOT_OBTAINED
};

/** @brief Struct with data received from the Date time library. */
struct date_time_evt {
	/** Type of event. */
	enum date_time_evt_type type;
};

/** @brief Date time library asynchronous event handler.
 *
 *  @param[in] evt The event and any associated parameters.
 */
typedef void (*date_time_evt_handler_t)(const struct date_time_evt *evt);

/** @brief Set the current date time.
 *
 *  @note See http://www.cplusplus.com/reference/ctime/tm/ for accepted input
 *        format.
 *
 *  @param[in] new_date_time Pointer to a tm structure.
 *
 *  @return 0        If the operation was successful.
 *  @return -EINVAL  If a member of the passing variable new_date_time does not
 *                   adhere to the tm structure format, or pointer is passed in as NULL.
 */
int date_time_set(const struct tm *new_date_time);

/**
 * @brief Set the current date/time based UTC time
 * 
 * @param new_date_time_sec New date/time in UTC seconds
 *  @return 0        If the operation was successful.
 *  @return -EINVAL  If a member of the passing variable new_date_time does not
 *                   adhere to the tm structure format, or pointer is passed in as NULL.
 */
int date_time_set_second(uint32_t new_date_time_sec);

/** @brief Get the date time UTC when the passing variable uptime was set.
 *         This function requires that k_uptime_get() has been called on the
 *         passing variable uptime prior to the function call.
 *
 *  @warning If the function fails, the passed in variable retains its
 *           old value.
 *
 *  @param[in, out] uptime Pointer to a previously set uptime.
 *
 *  @return 0        If the operation was successful.
 *  @return -ENODATA If the library does not have a valid date time UTC.
 *  @return -EINVAL  If the passed in pointer is NULL, dereferenced value is too large,
 *		     or already converted.
 */
int date_time_uptime_to_unix_time_ms(int64_t *uptime);

/** @brief Get the current local date time (UTC + Timezone Offset).
 *
 *  @warning If the function fails, the passed in variable is assigned a
 *           value of 0.
 *
 *  @param[out] local_time_s Pointer to a variable to store the current local date
 *                           time.
 *
 *  @return 0        If the operation was successful.
 *  @return -ENODATA If the library does not have a valid date time UTC.
 *  @return -EINVAL  If the passed in pointer is NULL.
 */
int date_time_local_second(uint32_t *local_time_s);

/** @brief Get the current date time UTC in miliseconds
 *
 *  @warning If the function fails, the passed in variable retains its
 *           old value.
 *
 *  @param[out] unix_time_ms Pointer to a variable to store the current date
 *                           time UTC.
 *
 *  @return 0        If the operation was successful.
 *  @return -ENODATA If the library does not have a valid date time UTC.
 *  @return -EINVAL  If the passed in pointer is NULL.
 */
int date_time_now(int64_t *unix_time_ms);

/** @brief Get the current date time UTC in seconds
 *
 *  @warning If the function fails, the passed in variable retains its
 *           old value.
 *
 *  @param[out] unix_time_ms Pointer to a variable to store the current date
 *                           time UTC.
 *
 *  @return 0        If the operation was successful.
 *  @return -ENODATA If the library does not have a valid date time UTC.
 *  @return -EINVAL  If the passed in pointer is NULL.
 */
int date_time_now_second(uint32_t *unix_time_s);

/** @brief Convenience function that checks if the library has obtained
 *	   an initial valid date time.
 *
 *  @note If this function returns false there is no point of
 *	  subsequent calls to other functions in this API that
 *	  depend on the validity of the internal date time. We
 *	  know that they would fail beforehand.
 *
 *  @return true  The library has obtained an initial date time.
 *  @return false The library has not obtained an initial date time.
 */
bool date_time_is_valid(void);

/** @brief Get current timezone offset in seconds.
 *
 *  @return	-1	The library's time was not valid
 *	@return	Timezone offset in seconds.
 */
int date_time_get_timezone(void);

/** @brief Register an event handler for Date time library events.
 *
 *  @warning The library only allows for one event handler to be registered
 *           at a time. A passed in event handler in this function will
 *           overwrite the previously set event handler.
 *
 *  @param evt_handler Event handler. Handler is de-registered if parameter is
 *                     NULL.
 */
void date_time_register_handler(date_time_evt_handler_t evt_handler);

/** @brief Asynchronous update of internal date time UTC. This function
 *         initiates a date time update regardless of the internal update
 *         interval. If an event handler is provided it will be updated
 *         with library events, accordingly.
 *
 *  @param evt_handler Event handler. If the passed in pointer is NULL the
 *                     previous registered event handler is not de-registered.
 *                     This means that library events will still be received in
 *                     the previously registered event handler.
 *
 *  @return 0 If the operation was successful.
 */
int date_time_update_async(date_time_evt_handler_t evt_handler);

/** @brief Clear the current date time held by the library.
 *
 *  @return 0 If the operation was successful.
 */
int date_time_clear(void);

/** @brief Clear a timestamp in unix time ms.
 *
 *  @param[in, out] unix_timestamp Pointer to a unix timestamp.
 *
 *  @return 0        If the operation was successful.
 *  @return -EINVAL  If the passed in pointer is NULL.
 */
int date_time_timestamp_clear(int64_t *unix_timestamp);

/**
 * @brief Start date/time thread 
 * 
 */
void date_time_start_work(void);
#ifdef __cplusplus
}
#endif

/** @} */

#endif /* DATE_TIME_H_ */

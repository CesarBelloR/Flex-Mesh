/***************************************************************************/
/*!
\file       PCF85263A.h
\brief      Real-time clock/calendar with alarm function, battery switch-over 
            and timestamp input application prototype interface (API)

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef PCF85263A_H_
#define PCF85263A_H_

#include <stdint.h>
#include <sys/timeutil.h>
#include <time.h>
#include "pcf85263a_registers.h"

/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
struct pcf85263a_rtc_time_registers {
	pcf85263a_100th_seconds_reg_t rtc_100th_sec;
	pcf85263a_seconds_reg_t rtc_sec;
	pcf85263a_minutes_reg_t rtc_min;
	pcf85263a_hours_reg_t rtc_hours;
	pcf85263a_days_reg_t rtc_date;
	pcf85263a_weekdays_reg_t rtc_weekday;
	pcf85263a_months_reg_t rtc_month;
	pcf85263a_years_reg_t rtc_year;
} __packed;

struct pcf85263a_rtc_alarm_1_registers {
	pcf85263a_rtc_alarm_second_alarm_1_reg_t rtc_sec;
	pcf85263a_rtc_alarm_minute_alarm_1_reg_t rtc_min;
	pcf85263a_rtc_alarm_hour_alarm_1_reg_t rtc_hours;
	pcf85263a_rtc_alarm_day_alarm_1_reg_t rtc_date;
	pcf85263a_rtc_alarm_month_alarm_1_reg_t rtc_month;
} __packed;

struct pcf85263a_rtc_alarm_2_registers {
	pcf85263a_rtc_alarm_minute_alarm_2_reg_t rtc_min;
	pcf85263a_rtc_alarm_hour_alarm_2_reg_t rtc_hours;
	pcf85263a_rtc_alarm_weekday_alarm_2_reg_t rtc_weekday;
} __packed;

struct pcf85263a_rtc_tsr1_registers {
	pcf85263a_tsr1_seconds_reg_t rtc_sec;
	pcf85263a_tsr1_minutes_reg_t rtc_min;
	pcf85263a_tsr1_hours_reg_t rtc_hours;
	pcf85263a_tsr1_days_reg_t rtc_date;
	pcf85263a_tsr1_months_reg_t rtc_month;
	pcf85263a_tsr1_years_reg_t rtc_year;
} __packed;

struct pcf85263a_rtc_tsr2_registers {
	pcf85263a_tsr2_seconds_reg_t rtc_sec;
	pcf85263a_tsr2_minutes_reg_t rtc_min;
	pcf85263a_tsr2_hours_reg_t rtc_hours;
	pcf85263a_tsr2_days_reg_t rtc_date;
	pcf85263a_tsr2_months_reg_t rtc_month;
	pcf85263a_tsr2_years_reg_t rtc_year;
} __packed;

struct pcf85263a_rtc_tsr3_registers {
	pcf85263a_tsr3_seconds_reg_t rtc_sec;
	pcf85263a_tsr3_minutes_reg_t rtc_min;
	pcf85263a_tsr3_hours_reg_t rtc_hours;
	pcf85263a_tsr3_days_reg_t rtc_date;
	pcf85263a_tsr3_months_reg_t rtc_month;
	pcf85263a_tsr3_years_reg_t rtc_year;
} __packed;

typedef struct {
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
	uint8_t days;
	uint8_t months;
} pcf85263a_alarm_type_1_config_t;

typedef struct {
	uint8_t enable_seconds;
	uint8_t enable_minutes;
	uint8_t enable_hours;
	uint8_t enable_days;
	uint8_t enable_months;
} pcf85263a_alarm_type_1_flag_t;

typedef struct {
	uint8_t minutes;
	uint8_t hours;
	uint8_t weekdays;
} pcf85263a_alarm_type_2_config_t;

typedef struct {
	uint8_t enable_minutes;
	uint8_t enable_hours;
	uint8_t enable_weekdays;
} pcf85263a_alarm_type_2_flag_t;

typedef struct {
	uint8_t enable_level_pulse;
	uint8_t enable_periodic;
	uint8_t enable_offset_correction;
	uint8_t enable_alarm_1;
	uint8_t enable_alarm_2;
	uint8_t enable_timestamp;
	uint8_t enable_battery_switch;
	uint8_t enable_wdg;
} pcf85263a_interrupt_flag_t;

#define PCF85263A_RTC_HOUR_MODE_24 (0x00)
#define PCF85263A_RTC_HOUR_MODE_12 (0x01)

#define PCF85263A_RTC_HOUR_MODE_12_AM (0x00)
#define PCF85263A_RTC_HOUR_MODE_12_PM (0x01)

/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes the PCF85263A RTC
 *
 * @param device the I2C channel such as I2C_0, I2C_1
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int pcf85263a_init(const char *device);

/** @brief Set the RTC to a given Unix time
 *
 * The RTC advances one tick per second with no access to sub-second
 * precision. This function will convert the given unix_time into seconds,
 * minutes, hours, day of the week, day of the month, month and year.
 * A Unix time of '0' means a timestamp of 00:00:00 UTC on Thursday 1st January
 * 1970.
 *
 * @param unix_time Unix time to set the rtc to.
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int pcf85263a_rtc_set_time(time_t unix_time);

/** @brief Get Unix Time
 *
 * @param unix_time Pointer to where store unix time that get from RTC
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int pcf85263a_rtc_get_time(time_t *unix_time);

/** @brief Initializes the Watchdog feature in PCF85263A
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid state of PCF85263A
 */
int pcf85263a_watchdog_init(void);

/** @brief Feed for watchdog to avoid the reset event
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid state of PCF85263A
 */
int pcf85263a_watchdog_feed(void);

/** @brief Configure the Alarm mode 1 - Seconds/Minutes/Hours/Day/Month
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid state of PCF85263A
 */
int pcf85263a_alarm_config_type_1(pcf85263a_alarm_type_1_config_t config);

/** @brief Enable the Alarm mode 1  - Seconds/Minutes/Hours/Day/Month
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid state of PCF85263A
 */
int pcf85263a_alarm_enable_type_1(pcf85263a_alarm_type_1_flag_t flag);

/** @brief Disable the Alarm mode 1 - Seconds/Minutes/Hours/Day/Month
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid state of PCF85263A
 */
int pcf85263a_alarm_disable_type_1(void);

/** @brief Configure the Alarm mode 2 - Minutes/Hours/Weekday
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid state of PCF85263A
 */
int pcf85263a_alarm_config_type_2(pcf85263a_alarm_type_2_config_t config);

/** @brief Enable the Alarm mode 2  - Seconds/Minutes/Hours/Day/Month
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid state of PCF85263A
 */
int pcf85263a_alarm_enable_type_2(pcf85263a_alarm_type_2_flag_t flag);

/** @brief Disable the Alarm mode 2 - Minutes/Hours/Weekday
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid state of PCF85263A
 */
int pcf85263a_alarm_disable_type_2(void);

/** @brief Enable/disable interrupt flag
 *
 * @param flag @ref pcf85263a_interrupt_flag_t
 * @retval None
 */
void pcf85263a_interrupt_enable(pcf85263a_interrupt_flag_t flag);

/** @brief Enable/disable the IO for interrupt
 *
 * @param enable or disable the IO
 * @retval None
 */
void pcf85263a_set_interrupt_io(bool enable);
#endif /* PCF85263A_H_ */
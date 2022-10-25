/***************************************************************************/
/*!
\file       PCF85263A.h
\brief      Real-time clock/calendar with alarm function, battery switch-over 
            and timestamp input register structure header file

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef PCF85263A_REGISTER_H_
#define PCF85263A_REGISTER_H_

#include <stdint.h>
/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/

/*! Define structure for register 100th seconds */
typedef union {
    uint8_t byte;
    struct {
        uint8_t seconds;
    };
} pcf85263a_100th_seconds_reg_t;

/*! Define structure for register seconds */
typedef union {
    uint8_t byte;
    struct {
        uint8_t seconds : 7;
        uint8_t os : 1;
    };
} pcf85263a_seconds_reg_t;

/*! Define structure for register minutes */
typedef union {
    uint8_t byte;
    struct {
        uint8_t minutes : 7;
        uint8_t emon : 1;
    };
} pcf85263a_minutes_reg_t;

/*! Define structure for register hours  */
typedef union {
    uint8_t byte;
    struct {
        uint8_t hours_12mode : 5;
        uint8_t ampm : 1;
        uint8_t unused_1 : 2;
    };
    struct {
        uint8_t hours_24mode : 6;
        uint8_t unused_2 : 2;
    };
} pcf85263a_hours_reg_t;

/*! Define structure for register days */
typedef union {
    uint8_t byte;
    struct {
        uint8_t days : 6;
        uint8_t unused_1 : 2;
    };
} pcf85263a_days_reg_t;

/*! Define structure for register weekdays */
typedef union {
    uint8_t byte;
    struct {
        uint8_t weekdays : 3;
        uint8_t unused_1 : 5;
    };
} pcf85263a_weekdays_reg_t;

/*! Define structure for register months */
typedef union {
    uint8_t byte;
    struct {
        uint8_t months : 5;
        uint8_t unused_1 : 3;
    };
} pcf85263a_months_reg_t;

/*! Define structure for register years */
typedef union {
    uint8_t byte;
    struct {
        uint8_t years;
    };
} pcf85263a_years_reg_t;

/*! Define structure for register RTC alarm - Second alarm 1 */
typedef union {
    uint8_t byte;
    struct {
        uint8_t sec_alarm : 7;
        uint8_t unused_1 : 1;
    };
} pcf85263a_rtc_alarm_second_alarm_1_reg_t;

/*! Define structure for register RTC alarm - Minutes alarm 1 */
typedef union {
    uint8_t byte;
    struct {
        uint8_t minute_alarm : 7;
        uint8_t unused_1 : 1;
    };
} pcf85263a_rtc_alarm_minute_alarm_1_reg_t;

/*! Define structure for register RTC alarm - Hour alarm 1 */
typedef union {
    uint8_t byte;
    struct {
        uint8_t hr_alarm1_12mode : 5;
        uint8_t ampm : 1;
        uint8_t unused_1 : 2;
    };
    struct {
        uint8_t hr_alarm1_24mode : 6;
        uint8_t unused_2 : 2;
    };
} pcf85263a_rtc_alarm_hour_alarm_1_reg_t;

/*! Define structure for register RTC alarm - Day alarm 1 */
typedef union {
    uint8_t byte;
    struct {
        uint8_t day_alarm : 6;
        uint8_t unused_1 : 2;
    };
} pcf85263a_rtc_alarm_day_alarm_1_reg_t;

/*! Define structure for register RTC alarm - Month alarm 1 */
typedef union {
    uint8_t byte;
    struct {
        uint8_t month_alarm : 5;
        uint8_t unused_1 : 3;
    };
} pcf85263a_rtc_alarm_month_alarm_1_reg_t;

/*! Define structure for register RTC alarm - Minutes alarm 2 */
typedef pcf85263a_rtc_alarm_minute_alarm_1_reg_t pcf85263a_rtc_alarm_minute_alarm_2_reg_t;

/*! Define structure for register RTC alarm - Hour alarm 2 */
typedef pcf85263a_rtc_alarm_hour_alarm_1_reg_t pcf85263a_rtc_alarm_hour_alarm_2_reg_t;

/*! Define structure for register RTC alarm - Weekday alarm 2 */
typedef union {
    uint8_t byte;
    struct {
        uint8_t wday_alarm : 3;
        uint8_t unused_1 : 5;
    };
} pcf85263a_rtc_alarm_weekday_alarm_2_reg_t;

/*! Define structure for register RTC alarm - Enables */
typedef union {
    uint8_t byte;
    struct {
        uint8_t sec_a1e : 1;
        uint8_t min_a1e : 1;
        uint8_t hr_a1e : 1;
        uint8_t day_a1e : 1;
        uint8_t mon_a1e : 1;
        uint8_t min_a2e : 1;
        uint8_t hr_a2e : 1;
        uint8_t wday_a2e : 1;
    };
} pcf85263a_rtc_alarm_enable_reg_t;

/*! Define structure for register RTC timestamp 1 (TSR1) - seconds*/
typedef pcf85263a_seconds_reg_t pcf85263a_tsr1_seconds_reg_t;

/*! Define structure for register RTC timestamp 1 (TSR1) - minutes */
typedef pcf85263a_minutes_reg_t pcf85263a_tsr1_minutes_reg_t;

/*! Define structure for register RTC timestamp 1 (TSR1) - hours  */
typedef pcf85263a_hours_reg_t pcf85263a_tsr1_hours_reg_t;

/*! Define structure for register RTC timestamp 1 (TSR1) - days */
typedef pcf85263a_days_reg_t pcf85263a_tsr1_days_reg_t;

/*! Define structure for register RTC timestamp 1 (TSR1) - months */
typedef pcf85263a_months_reg_t pcf85263a_tsr1_months_reg_t;

/*! Define structure for register RTC timestamp 1 (TSR1) - years */
typedef pcf85263a_years_reg_t pcf85263a_tsr1_years_reg_t;

/*! Define structure for register RTC timestamp 2 (TSR2) - seconds*/
typedef pcf85263a_seconds_reg_t pcf85263a_tsr2_seconds_reg_t;

/*! Define structure for register RTC timestamp 2 (TSR2) - minutes */
typedef pcf85263a_minutes_reg_t pcf85263a_tsr2_minutes_reg_t;

/*! Define structure for register RTC timestamp 2 (TSR2) - hours  */
typedef pcf85263a_hours_reg_t pcf85263a_tsr2_hours_reg_t;

/*! Define structure for register RTC timestamp 2 (TSR2) - days */
typedef pcf85263a_days_reg_t pcf85263a_tsr2_days_reg_t;

/*! Define structure for register RTC timestamp 2 (TSR2) - months */
typedef pcf85263a_months_reg_t pcf85263a_tsr2_months_reg_t;

/*! Define structure for register RTC timestamp 2 (TSR2) - years */
typedef pcf85263a_years_reg_t pcf85263a_tsr2_years_reg_t;

/*! Define structure for register RTC timestamp 3 (TSR3) - seconds*/
typedef pcf85263a_seconds_reg_t pcf85263a_tsr3_seconds_reg_t;

/*! Define structure for register RTC timestamp 3 (TSR3) - minutes */
typedef pcf85263a_minutes_reg_t pcf85263a_tsr3_minutes_reg_t;

/*! Define structure for register RTC timestamp 3 (TSR3) - hours  */
typedef pcf85263a_hours_reg_t pcf85263a_tsr3_hours_reg_t;

/*! Define structure for register RTC timestamp 3 (TSR3) - days */
typedef pcf85263a_days_reg_t pcf85263a_tsr3_days_reg_t;

/*! Define structure for register RTC timestamp 3 (TSR3) - months */
typedef pcf85263a_months_reg_t pcf85263a_tsr3_months_reg_t;

/*! Define structure for register RTC timestamp 3 (TSR3) - years */
typedef pcf85263a_years_reg_t pcf85263a_tsr3_years_reg_t;

/*! Define structure for register RTC mode time */
typedef union {
    uint8_t byte;
    struct {
        uint8_t tsr1m : 2;
        uint8_t tsr2m : 3;
        uint8_t unused_1 : 1;
        uint8_t tsr3m : 2;
    };
} pcf85263a_rtc_tsr_mode_reg_t;

/*! Define structure for register Stop-watch time - 100th seconds time */
typedef union {
    uint8_t byte;
    struct {
        uint8_t seconds;
    };
} pcf85263a_stop_watch_100th_seconds_reg_t;

/*! Define structure for register Stop-watch time - seconds time */
typedef union {
    uint8_t byte;
    struct {
        uint8_t seconds : 7;
        uint8_t os : 1;
    };
} pcf85263a_stop_watch_seconds_reg_t;

/*! Define structure for register Stop-watch time - minutes time */
typedef union {
    uint8_t byte;
    struct {
        uint8_t minutes : 7;
        uint8_t emon : 1;
    };
} pcf85263a_stop_watch_minutes_reg_t;

/*! Define structure for register Stop-watch time - hours xx_xx_00 time */
typedef union {
    uint8_t byte;
    struct {
        uint8_t hours;
    };
} pcf85263a_stop_watch_hours_xx_xx_00_reg_t;

/*! Define structure for register Stop-watch time - hours xx_00_xx time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_xx_00_xx_reg_t;

/*! Define structure for register Stop-watch time - hours 00_xx_xx time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_00_xx_xx_reg_t;

/*! Define structure for register Stop-watch Alarm 1 - seconds */
typedef union {
    uint8_t byte;
    struct {
        uint8_t seconds : 7;
        uint8_t unused_1 : 1;
    };
} pcf85263a_stop_watch_alarm_1_seconds_reg_t;

/*! Define structure for register Stop-watch Alarm 1 - minutes */
typedef union {
    uint8_t byte;
    struct {
        uint8_t minutes : 7;
        uint8_t unused_1 : 1;
    };
} pcf85263a_stop_watch_alarm_1_minutes_reg_t;

/*! Define structure for register Stop-watch Alarm 1 - hours xx_xx_00 time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_xx_xx_00_alarm_1_reg_t;

/*! Define structure for register Stop-watch Alarm 1 - hours xx_00_xx time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_xx_00_xx_alarm_1_reg_t;

/*! Define structure for register Stop-watch Alarm 1 - hours 00_xx_xx time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_00_xx_xx_alarm_1_reg_t;

/*! Define structure for register Stop-watch Alarm 2 - minutes */
typedef pcf85263a_stop_watch_alarm_1_minutes_reg_t pcf85263a_stop_watch_alarm_2_minutes_reg_t;

/*! Define structure for register Stop-watch Alarm 2 - hours xx_00 time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_xx_00_alarm_2_reg_t;

/*! Define structure for register Stop-watch Alarm 2 - hours 00_xx time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_00_xx_alarm_2_reg_t;

/*! Define structure for register RTC Stop-watch Alarm - Enables */
typedef union {
    uint8_t byte;
    struct {
        uint8_t sec_a1e : 1;
        uint8_t min_a1e : 1;
        uint8_t hr_xx_xx_00_a1e : 1;
        uint8_t hr_xx_00_xx_a1e : 1;
        uint8_t hr_00_xx_xx_a1e : 1;
        uint8_t min_a2e : 1;
        uint8_t hr_xx_00_a2e : 1;
        uint8_t hr_00_xx_a2e : 1;
    };
} pcf85263a_stop_watch_alarm_enable_reg_t;

/*! Define structure for register Stop-watch timestamp 1 (TSR1) - seconds */
typedef pcf85263a_stop_watch_alarm_1_seconds_reg_t pcf85263a_stop_watch_tsr1_seconds_reg_t;

/*! Define structure for register Stop-watch timestamp 1 (TSR1) - minutes */
typedef pcf85263a_stop_watch_alarm_1_minutes_reg_t pcf85263a_stop_watch_tsr1_minutes_reg_t;

/*! Define structure for register Stop-watch timestamp 1 (TSR1) - hours xx_xx_00 time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_xx_xx_00_tsr1_reg_t;

/*! Define structure for register Stop-watch timestamp 1 (TSR1) - hours xx_00_xx time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_xx_00_xx_tsr1_reg_t;

/*! Define structure for register Stop-watch timestamp 1 (TSR1) - hours 00_xx_xx time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_00_xx_xx_tsr1_reg_t;

/*! Define structure for register Stop-watch timestamp 2 (TSR2) - seconds */
typedef pcf85263a_stop_watch_alarm_1_seconds_reg_t pcf85263a_stop_watch_tsr2_seconds_reg_t;

/*! Define structure for register Stop-watch timestamp 2 (TSR2) - minutes */
typedef pcf85263a_stop_watch_alarm_1_minutes_reg_t pcf85263a_stop_watch_tsr2_minutes_reg_t;

/*! Define structure for register Stop-watch timestamp 2 (TSR2) - hours xx_xx_00 time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_xx_xx_00_tsr2_reg_t;

/*! Define structure for register Stop-watch timestamp 2 (TSR2) - hours xx_00_xx time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_xx_00_xx_tsr2_reg_t;

/*! Define structure for register Stop-watch timestamp 2 (TSR2) - hours 00_xx_xx time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85263a_stop_watch_hours_00_xx_xx_tsr2_reg_t;

/*! Define structure for register Stop-watch timestamp 3 (TSR3) - seconds */
typedef pcf85263a_stop_watch_alarm_1_seconds_reg_t pcf85363a_stop_watch_tsr3_seconds_reg_t;

/*! Define structure for register Stop-watch timestamp 3 (TSR3) - minutes */
typedef pcf85263a_stop_watch_alarm_1_minutes_reg_t pcf85363a_stop_watch_tsr3_minutes_reg_t;

/*! Define structure for register Stop-watch timestamp 3 (TSR3) - hours xx_xx_00 time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85363a_stop_watch_hours_xx_xx_00_tsr3_reg_t;

/*! Define structure for register Stop-watch timestamp 3 (TSR3) - hours xx_00_xx time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85363a_stop_watch_hours_xx_00_xx_tsr3_reg_t;

/*! Define structure for register Stop-watch timestamp 3 (TSR3) - hours 00_xx_xx time */
typedef pcf85263a_stop_watch_hours_xx_xx_00_reg_t pcf85363a_stop_watch_hours_00_xx_xx_tsr3_reg_t;

/*! Define structure for register Stop-watch mode control time */
typedef union {
    uint8_t byte;
    struct {
        uint8_t tsr1m : 2;
        uint8_t tsr2m : 3;
        uint8_t unused_1 : 1;
        uint8_t tsr3m : 2;
    };
} pcf85263a_stop_watch_tsr_mode_reg_t;

/*! Define structure for register control registers */
typedef union {
    uint8_t byte;
    struct {
        uint8_t offset;
    };
} pcf85263a_offset_reg_t;

/*! Define structure for register control - oscillator registers */
typedef union {
    uint8_t byte;
    struct {
        uint8_t cl : 2;
        uint8_t oscd : 2;
        uint8_t lowj : 1;
        uint8_t hour_mode : 1;
        uint8_t offm : 1;
        uint8_t clkiv : 1;
    };
} pcf85263a_oscillator_reg_t;

/*! Define structure for register control - battery switch registers */
typedef union {
    uint8_t byte;
    struct {
        uint8_t bsth : 1;
        uint8_t bsm : 2;
        uint8_t bsrr : 1;
        uint8_t bsoff : 1;
        uint8_t unused_1 : 3;
    };
} pcf85263a_battery_switch_reg_t;

/*! Define structure for register control - pin io registers */
typedef union {
    uint8_t byte;
    struct {
        uint8_t intapm : 2;
        uint8_t tspm : 2;
        uint8_t tsim : 1;
        uint8_t tsl : 1;
        uint8_t tspull : 1;
        uint8_t cklpm  : 1;
    };
} pcf85263a_pin_io_reg_t;

/*! Define structure for register control - function registers */
typedef union {
    uint8_t byte;
    struct {
        uint8_t cof : 3;
        uint8_t stopm : 1;
        uint8_t rtcm : 1;
        uint8_t pi : 2;
        uint8_t mode_100th : 1;
    };
} pcf85263a_function_reg_t;

/*! Define structure for register control - interrupt channel A registers */
typedef union {
    uint8_t byte;
    struct {
        uint8_t wdiea : 1;
        uint8_t bsiea : 1;
        uint8_t tsriea : 1;
        uint8_t a2iea : 1;
        uint8_t a1iea : 1;
        uint8_t oiea : 1;
        uint8_t piea : 1;
        uint8_t ilpa : 1;
    };
} pcf85263a_inta_reg_t;

/*! Define structure for register control - interrupt channel B registers */
typedef union {
    uint8_t byte;
    struct {
        uint8_t wdieb : 1;
        uint8_t bsieb : 1;
        uint8_t tsrieb : 1;
        uint8_t a2ieb : 1;
        uint8_t a1ieb : 1;
        uint8_t oieb : 1;
        uint8_t pieb : 1;
        uint8_t ilpb : 1;
    };
} pcf85263a_intb_reg_t;

/*! Define structure for register control - flags registers */
typedef union {
    uint8_t byte;
    struct {
        uint8_t tsr1f : 1;
        uint8_t tsr2f : 1;
        uint8_t tsr3f : 1;
        uint8_t bsf : 1;
        uint8_t wdf : 1;
        uint8_t a1f : 1;
        uint8_t a2f : 1;
        uint8_t pif : 1;
    };
} pcf85263a_flags_reg_t;

/*! Define structure for register control - watchdog registers */
typedef union {
    uint8_t byte;
    struct {
        uint8_t wds : 2;
        uint8_t wdr : 5;
        uint8_t wdm : 1;
    };
} pcf85263a_watchdog_reg_t;

/*! Define structure for register control - stop enable registers */
typedef union {
    uint8_t byte;
    struct {
        uint8_t stop : 1;
        uint8_t unused_1 : 7;
    };
} pcf85263a_stop_enable_reg_t;

/*! Define structure for register control - reset registers */
typedef union {
    uint8_t byte;
    struct {
        uint8_t cts : 1;
        uint8_t unused_1 : 2;
        uint8_t sr : 1;
        uint8_t unused_2 : 3;
        uint8_t cpr : 1;
    };
} pcf85263a_resets_reg_t;

#endif /* PCF85263A_REGISTER_H_ */
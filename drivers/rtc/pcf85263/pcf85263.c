#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/sys/util.h>
#include <time.h>
#include "pcf85263a.h"
#include "pcf85263a_registers.h"

LOG_MODULE_REGISTER(PCF85263A, CONFIG_PCF85263_LOG_LEVEL);

#define PCF85263A_RTC_I2C_7BIT_ADDR (0x51)

/* PCF85263A registers for RTC mode */
enum {
    PCF85263A_RTC_MODE_100TH_SECONDS_REG = 0x00,
    PCF85263A_RTC_MODE_SECONDS_REG = 0x01,
    PCF85263A_RTC_MODE_MINUTES_REG = 0x02,
    PCF85263A_RTC_MODE_HOURS_REG = 0x03,
    PCF85263A_RTC_MODE_DAYS_REG = 0x04,
    PCF85263A_RTC_MODE_WEEKDAYS_REG = 0x05,
    PCF85263A_RTC_MODE_MONTHS_REG = 0x06,
    PCF85263A_RTC_MODE_YEARS_REG = 0x07,
    PCF85263A_RTC_MODE_SECOND_ALARM1_REG = 0x08,
    PCF85263A_RTC_MODE_MINUTES_ALARM1_REG = 0x09,
    PCF85263A_RTC_MODE_HOUR_ALARM1_REG = 0x0A,
    PCF85263A_RTC_MODE_DAY_ALARM1_REG = 0x0B,
    PCF85263A_RTC_MODE_MONTH_ALARM1_REG = 0x0C,
    PCF85263A_RTC_MODE_MINUTES_ALARM2_REG = 0x0D,
    PCF85263A_RTC_MODE_HOUR_ALARM2_REG = 0x0E,
    PCF85263A_RTC_MODE_WEEKEDAY_ALARM2_REG = 0x0F,
    PCF85263A_RTC_MODE_ALARM_ENABLE_REG = 0x10,
    PCF85263A_RTC_MODE_TSR1_SECOND_REG = 0x11,
    PCF85263A_RTC_MODE_TSR1_MINUTES_REG = 0x12,
    PCF85263A_RTC_MODE_TSR1_HOURS_REG = 0x13,
    PCF85263A_RTC_MODE_TSR1_DAYS_REG = 0x14,
    PCF85263A_RTC_MODE_TSR1_MONTHS_REG = 0x15,
    PCF85263A_RTC_MODE_TSR1_YEARS_REG = 0x16,
    PCF85263A_RTC_MODE_TSR2_SECOND_REG = 0x17,
    PCF85263A_RTC_MODE_TSR2_MINUTES_REG = 0x8,
    PCF85263A_RTC_MODE_TSR2_HOURS_REG = 0x19,
    PCF85263A_RTC_MODE_TSR2_DAYS_REG = 0x1A,
    PCF85263A_RTC_MODE_TSR2_MONTHS_REG = 0x1B,
    PCF85263A_RTC_MODE_TSR2_YEARS_REG = 0x1C,
    PCF85263A_RTC_MODE_TSR3_SECOND_REG = 0x1D,
    PCF85263A_RTC_MODE_TSR3_MINUTES_REG = 0xE,
    PCF85263A_RTC_MODE_TSR3_HOURS_REG = 0x1F,
    PCF85263A_RTC_MODE_TSR3_DAYS_REG = 0x20,
    PCF85263A_RTC_MODE_TSR3_MONTHS_REG = 0x21,
    PCF85263A_RTC_MODE_TSR3_YEARS_REG = 0x22,
    PCF85263A_RTC_MODE_TSR_MODE_REG = 0x23,
};

/* PCF85263A registers for Stop-Watch mode */
enum {
    PCF85263A_STOP_WATCH_100TH_SECONDS_REG = 0x00,
    PCF85263A_STOP_WATCH_SECODNS_REG = 0x01,
    PCF85263A_STOP_WATCH_MINUTES_REG = 0x02,
    PCF85263A_STOP_WATCH_HOURS_XX_XX_00_REG = 0x03,
    PCF85263A_STOP_WATCH_HOURS_XX_00_XX_REG = 0x04,
    PCF85263A_STOP_WATCH_HOURS_00_XX_XX_REG = 0x05,
    PCF85263A_STOP_WATCH_SECOND_ALARM1_REG = 0x08,
    PCF85263A_STOP_WATCH_MINUTE_ALARM1_REG = 0x09,
    PCF85263A_STOP_WATCH_HOURS_XX_XX_00_ALARM1_REG = 0x0A,
    PCF85263A_STOP_WATCH_HOURS_XX_00_XX_ALARM1_REG = 0x0B,
    PCF85263A_STOP_WATCH_HOURS_00_XX_XX_ALARM1_REG = 0x0C,
    PCF85263A_STOP_WATCH_MINUTE_ALARM2_REG = 0x0D,
    PCF85263A_STOP_WATCH_HOURS_XX_00_ALARM2_REG = 0x0E,
    PCF85263A_STOP_WATCH_HOURS_00_XX_ALARM2_REG = 0x0F,
    PCF85263A_STOP_WATCH_ALARM_ENABLE_REG = 0x10,
    PCF85263A_STOP_WATCH_TSR1_SECONDS_REG = 0x11,
    PCF85263A_STOP_WATCH_TSR1_MINUTES_REG = 0x12,
    PCF85263A_STOP_WATCH_TSR1_HR_XX_XX_00_REG = 0x13,
    PCF85263A_STOP_WATCH_TSR1_HR_XX_00_XX_REG = 0x14,
    PCF85263A_STOP_WATCH_TSR1_HR_00_XX_XX_REG = 0x15,
    PCF85263A_STOP_WATCH_TSR2_SECONDS_REG = 0x17,
    PCF85263A_STOP_WATCH_TSR2_MINUTES_REG = 0x18,
    PCF85263A_STOP_WATCH_TSR2_HR_XX_XX_00_REG = 0x19,
    PCF85263A_STOP_WATCH_TSR2_HR_XX_00_XX_REG = 0x1A,
    PCF85263A_STOP_WATCH_TSR2_HR_00_XX_XX_REG = 0x1B,
    PCF85263A_STOP_WATCH_TSR3_SECONDS_REG = 0x1D,
    PCF85263A_STOP_WATCH_TSR3_MINUTES_REG = 0x1E,
    PCF85263A_STOP_WATCH_TSR3_HR_XX_XX_00_REG = 0x1F,
    PCF85263A_STOP_WATCH_TSR3_HR_XX_00_XX_REG = 0x20,
    PCF85263A_STOP_WATCH_TSR3_HR_00_XX_XX_REG = 0x21,
    PCF85263A_STOP_WATCH_RSR_MODE_REG = 0x23,
};

/* PCF85263A registers for control */
enum {
    PCF85263A_OFFSET_REG = 0x24,
    PCF85263A_OSCILLATOR_REG = 0x25,
    PCF85263A_BATTERY_SWITCH_REG = 0x26,
    PCF85263A_PIN_IO_REG = 0x27,
    PCF85263A_FUNCTION_REG = 0x28,
    PCF85263A_INTA_ENABLE_REG = 0x29,
    PCF85263A_INTB_ENABLE_REG = 0x2A,
    PCF85263A_FLAGS_REG = 0x2B,
    PCF85263A_RAM_BYTE_REG = 0x2C,
    PCF85263A_WATCH_DOG_REG = 0x2D,
    PCF85263A_STOP_ENABLE_REG = 0x2E,
    PCF85263A_RESETS_REG = 0x2F
};

#define UNIX_YEAR_OFFSET		(69)
#define MAX_WRITE_SIZE          (64)
#define PCF85263A_REGISTER_COUNT(a,b) (b - a + 1)

struct pcf85263_config {
	const struct device *i2c_dev;
	uint8_t addr;
};

struct pcf85263_data {
	struct k_sem lock;
    bool is_error;
	struct pcf85263a_rtc_time_registers rtc_registers;
	struct pcf85263a_rtc_alarm_1_registers rtc_alm1_registers;
	struct pcf85263a_rtc_alarm_2_registers rtc_alm2_registers;
    struct pcf85263a_rtc_tsr1_registers rtc_tsr1_registers;
    struct pcf85263a_rtc_tsr2_registers rtc_tsr2_registers;
    struct pcf85263a_rtc_tsr3_registers rtc_tsr3_registers;
    pcf85263a_oscillator_reg_t osc_registers;
    pcf85263a_watchdog_reg_t watchdog_registers;
};

static struct pcf85263_data m_pcf85263_data;
static struct pcf85263_config m_pcf85263_config;
static struct k_work_delayable m_pcf85263_watchdog_work;

/** @brief Convert bcd time in device registers to UNIX time
 *
 * @retval returns unix time.
 */
static time_t decode_rtc(void)
{
	struct pcf85263_data *data = &m_pcf85263_data;
	time_t time_unix = 0;
	struct tm time = { 0 };
	time.tm_sec = bcd2bin(data->rtc_registers.rtc_sec.seconds);
	time.tm_min = bcd2bin(data->rtc_registers.rtc_min.minutes);
    if (data->osc_registers.hour_mode == PCF85263A_RTC_HOUR_MODE_24) {
        time.tm_hour = bcd2bin(data->rtc_registers.rtc_hours.hours_24mode);
    } else {
        uint8_t hour = bcd2bin(data->rtc_registers.rtc_hours.hours_12mode);
        if (data->rtc_registers.rtc_hours.ampm == PCF85263A_RTC_HOUR_MODE_12_PM) {
            hour += 12;
        };
        time.tm_hour = hour;
    }
	
	time.tm_mday = bcd2bin(data->rtc_registers.rtc_date.days);
	time.tm_wday = data->rtc_registers.rtc_weekday.weekdays;

	/* tm struct starts months at 0, PCF85263A starts at 1 */
	time.tm_mon = bcd2bin(data->rtc_registers.rtc_month.months) - 1;
	/* tm struct uses years since 1900 but unix time uses years since 1970 */
	time.tm_year = bcd2bin(data->rtc_registers.rtc_year.years) +
		UNIX_YEAR_OFFSET;

	time_unix = timeutil_timegm(&time);

	LOG_DBG("Unix time is %u", (uint32_t)time_unix);

	return time_unix;
}

/** @brief Encode time struct tm into PCF85263A rtc registers
 *
 * @param time_buffer tm struct containing time to be encoded into PCF85263A
 * registers.
 *
 * @retval return 0 on success, or a negative error code from invalid
 * parameter.
 */
static int encode_rtc(struct tm *time_buffer)
{
	struct pcf85263_data *data = &m_pcf85263_data;
	uint8_t month;
	uint8_t year_since_epoch;

	/* In a tm struct, months start at 0, pcf85263 starts with 1 */
	month = time_buffer->tm_mon + 1;

	if (time_buffer->tm_year < UNIX_YEAR_OFFSET) {
		return -EINVAL;
	}
	year_since_epoch = time_buffer->tm_year - UNIX_YEAR_OFFSET;

	/* Set external oscillator configuration bit */
	data->rtc_registers.rtc_100th_sec.seconds = 0;
	data->rtc_registers.rtc_sec.seconds = bin2bcd(time_buffer->tm_sec);
    data->rtc_registers.rtc_sec.os = 1;
	data->rtc_registers.rtc_min.minutes = bin2bcd(time_buffer->tm_min);

    if (data->osc_registers.hour_mode == PCF85263A_RTC_HOUR_MODE_24) {
        data->rtc_registers.rtc_hours.hours_24mode = time_buffer->tm_hour;
    } else {
        if (time_buffer->tm_hour >= 12) {
            time_buffer->tm_hour = time_buffer->tm_hour - 12;
            data->rtc_registers.rtc_hours.ampm = 1;
        } else {
            data->rtc_registers.rtc_hours.ampm = 0;
        }
        data->rtc_registers.rtc_hours.hours_12mode = bin2bcd(time_buffer->tm_hour);
    }

	data->rtc_registers.rtc_hours.byte = bin2bcd(time_buffer->tm_hour);
	data->rtc_registers.rtc_weekday.weekdays = bin2bcd(time_buffer->tm_wday);
	data->rtc_registers.rtc_date.days = bin2bcd(time_buffer->tm_mday);
	data->rtc_registers.rtc_month.months = bin2bcd(month);
	data->rtc_registers.rtc_year.years = bin2bcd(year_since_epoch);
    /* Stop RTC */
    data->rtc_registers.rtc_stop = 0x01;
    /* Reset prescaler */
    data->rtc_registers.rtc_reset = 0xA4;
	return 0;
}

static int read_register(uint8_t addr, uint8_t *val)
{
	const struct pcf85263_config *cfg = &m_pcf85263_config;

	int rc = i2c_write_read(cfg->i2c_dev, cfg->addr,
				&addr, sizeof(addr),
				val, 1);

	return rc;
}

static int read_registers(uint8_t addr, uint8_t *val, int num_reg) {
	const struct pcf85263_config *cfg = &m_pcf85263_config;

	int rc = i2c_write_read(cfg->i2c_dev, cfg->addr,
				&addr, sizeof(addr),
				val, num_reg);

	return rc;
}

static int write_register(uint8_t addr, uint8_t value)
{
	const struct pcf85263_config *cfg = &m_pcf85263_config;
	int rc = 0;

	uint8_t tx_buf[2] = {addr, value};

	rc = i2c_write(cfg->i2c_dev, tx_buf, sizeof(tx_buf), cfg->addr);

	return rc;
}

static int write_data_block(uint8_t offset_addr, uint8_t size)
{
	struct pcf85263_data *data = &m_pcf85263_data;
	const struct pcf85263_config *cfg = &m_pcf85263_config;
	int rc = 0;
	uint8_t tx_buf[MAX_WRITE_SIZE + 1];
	uint8_t *write_block_start;

	if (size > MAX_WRITE_SIZE) {
		return -EINVAL;
	}

	if (offset_addr > PCF85263A_RESETS_REG) {
		return -EINVAL;
	}

	if (offset_addr == PCF85263A_STOP_ENABLE_REG) {
		write_block_start = (uint8_t *)&data->rtc_registers;
	} else if (offset_addr == PCF85263A_RTC_MODE_SECOND_ALARM1_REG) {
		write_block_start = (uint8_t *)&data->rtc_alm1_registers;
	} else if (offset_addr == PCF85263A_RTC_MODE_MINUTES_ALARM2_REG) {
		write_block_start = (uint8_t *)&data->rtc_alm2_registers;
	} else if (offset_addr == PCF85263A_RTC_MODE_TSR1_SECOND_REG) {
        write_block_start = (uint8_t *)&data->rtc_tsr1_registers;
	} else if (offset_addr == PCF85263A_RTC_MODE_TSR2_SECOND_REG) {
        write_block_start = (uint8_t *)&data->rtc_tsr2_registers;
	} else if (offset_addr == PCF85263A_RTC_MODE_TSR3_SECOND_REG) {
        write_block_start = (uint8_t *)&data->rtc_tsr3_registers;
	} else {
        return -EINVAL;
    }

	/* Load register address into first byte then fill in data values */
	tx_buf[0] = offset_addr;
	memcpy(&tx_buf[1], write_block_start, size);
    
	rc = i2c_write(cfg->i2c_dev, tx_buf, size + 1, cfg->addr);
	return rc;
}

int pcf85263a_rtc_set_time(time_t unix_time)
{
	struct tm time_buffer = { 0 };
	int rc = 0;

	if (unix_time > UINT32_MAX) {
		LOG_ERR("Unix time must be 32-bit");
		return -EINVAL;
	}

	/* Convert unix_time to civil time */
	gmtime_r(&unix_time, &time_buffer);
	LOG_DBG("Desired time is %4d-%02d-%02d %2d:%02d:%02d", (time_buffer.tm_year + 1900),
		(time_buffer.tm_mon + 1), time_buffer.tm_mday, time_buffer.tm_hour,
		time_buffer.tm_min, time_buffer.tm_sec);

	/* Encode time */
	rc = encode_rtc(&time_buffer);
	if (rc < 0) {
		goto out;
	}

	/* Write to device */
	rc = write_data_block(PCF85263A_STOP_ENABLE_REG, 
        PCF85263A_REGISTER_COUNT(PCF85263A_RTC_MODE_100TH_SECONDS_REG, PCF85263A_RTC_MODE_YEARS_REG) + 2);
    if (rc != 0) {
        LOG_ERR("Failed to update RTC time");
        goto out;
    }

    /* Start the RTC */
    rc = write_register(PCF85263A_STOP_ENABLE_REG, 0x00);
    if (rc != 0) {
        LOG_ERR("Failed to start the RTC");
        goto out;
    }

out:

	return rc;
}

int pcf85263a_rtc_get_time(time_t* unix_time) {
    const struct pcf85263_config *cfg = &m_pcf85263_config;
    struct pcf85263_data *data = &m_pcf85263_data;

	uint8_t addr = PCF85263A_STOP_ENABLE_REG;
    if (data->is_error) {
        return 0;
    }
    
	int rc = i2c_write_read(cfg->i2c_dev, cfg->addr,
				&addr, sizeof(addr),
				&data->rtc_registers, PCF85263A_REGISTER_COUNT(PCF85263A_RTC_MODE_100TH_SECONDS_REG, 
                    PCF85263A_RTC_MODE_YEARS_REG) + 2);
	if (rc == 0) {
		*unix_time = decode_rtc();
	} else {
        data->is_error = true;
    }
    
    return rc;
}

int pcf85263a_init(const char* device)
{
    struct pcf85263_data *data = &m_pcf85263_data;
    struct pcf85263_config *cfg = &m_pcf85263_config;

    cfg->i2c_dev = (struct device*)device_get_binding(device);
    if (cfg->i2c_dev == NULL) {
        LOG_ERR("Failed to get device_get_binding %s", (device));
        return -EINVAL;
    }

    cfg->addr = PCF85263A_RTC_I2C_7BIT_ADDR;
    data->is_error = false;
 	/* Initialize and take the lock */
	k_sem_init(&data->lock, 0, 1);
    return 0;
}

static void pcf85263a_watchdog_handler(struct k_work *work)
{
    pcf85263a_watchdog_feed();
    LOG_INF("Feed for watchdog");
	k_work_schedule(&m_pcf85263_watchdog_work, K_SECONDS(CONFIG_PCF85263_WATCHDOG_FEED_INTERNAL_SECONDS));
}

int pcf85263a_watchdog_init(void)
{
    struct pcf85263_data *data = &m_pcf85263_data;
    /* Set watchdog configuration register */
    pcf85263a_watchdog_reg_t watchdog_reg = {0x00};
    /* Repeat mode */
    watchdog_reg.wdm = 0UL;
    /* Maximum timeout of watchdog (124s) */
    watchdog_reg.wdr = 31UL;
    /* Configure for step 1s per tick */
    watchdog_reg.wds = 01;
    data->watchdog_registers = watchdog_reg;

    int ret = write_register(PCF85263A_WATCH_DOG_REG, data->watchdog_registers.byte);
    if (ret == 0) {
        k_work_init_delayable(&m_pcf85263_watchdog_work, pcf85263a_watchdog_handler);
        k_work_schedule(&m_pcf85263_watchdog_work, 
            K_SECONDS(CONFIG_PCF85263_WATCHDOG_FEED_INTERNAL_SECONDS));
        LOG_INF("Initialized Watchdog with PCF85263A successfully");
    } else {
        LOG_ERR("Failed to initialize Watchdog with PCF85263A");
    }
    return ret;
}

int pcf85263a_watchdog_feed(void)
{
    struct pcf85263_data *data = &m_pcf85263_data;
    return write_register(PCF85263A_WATCH_DOG_REG, data->watchdog_registers.byte);
}

int pcf85263a_watchdog_stop_feed(void) 
{
    k_work_cancel_delayable(&m_pcf85263_watchdog_work);
    return 0;
}

int pcf85263a_alarm_config_type_1(pcf85263a_alarm_type_1_config_t info)
{
    int rc = 0;
    struct pcf85263_data *data = &m_pcf85263_data;

    data->rtc_alm1_registers.rtc_sec.sec_alarm = bin2bcd(info.seconds);
    data->rtc_alm1_registers.rtc_min.minute_alarm = bin2bcd(info.minutes);

    if (data->osc_registers.hour_mode == PCF85263A_RTC_HOUR_MODE_24) {
        data->rtc_alm1_registers.rtc_hours.hr_alarm1_24mode = bin2bcd(info.hours);
    } else {
        if (info.hours >= 12) {
            info.hours = info.hours - 12;
            data->rtc_alm1_registers.rtc_hours.ampm = 1;
        } else {
            data->rtc_alm1_registers.rtc_hours.ampm = 0;
        }
        data->rtc_alm1_registers.rtc_hours.hr_alarm1_12mode = bin2bcd(info.hours);
    }

    data->rtc_alm1_registers.rtc_date.day_alarm = bin2bcd(info.days);
    data->rtc_alm1_registers.rtc_month.month_alarm = bin2bcd(info.months - 1); 

	/* Write to device */
    int num_reg = PCF85263A_REGISTER_COUNT(PCF85263A_RTC_MODE_SECOND_ALARM1_REG, PCF85263A_RTC_MODE_MONTH_ALARM1_REG);
	rc = write_data_block(PCF85263A_RTC_MODE_SECOND_ALARM1_REG, num_reg);
    
    if (rc != 0) {
        LOG_ERR("Failed to set Alarm Type 1");
        return rc;
    }

    uint8_t reg_read[16] = {0x00};
    rc = read_registers(PCF85263A_RTC_MODE_SECOND_ALARM1_REG, reg_read, num_reg);
    LOG_HEXDUMP_INF(reg_read, num_reg, "ALARM");
    LOG_DBG("Configured Alarm Type 1 successful");
    return rc;
}

int pcf85263a_alarm_enable_type_1(pcf85263a_alarm_type_1_flag_t flag) {
    pcf85263a_rtc_alarm_enable_reg_t reg = {0x00};
    int rc = 0;

    rc = read_register(PCF85263A_RTC_MODE_ALARM_ENABLE_REG, &reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to read register PCF85263A_RTC_MODE_ALARM_ENABLE_REG error %d", rc);
        return rc;
    }

    reg.sec_a1e = flag.enable_seconds;
    reg.min_a1e = flag.enable_minutes;
    reg.hr_a1e = flag.enable_hours;
    reg.day_a1e = flag.enable_days;
    reg.mon_a1e = flag.enable_months;

    LOG_DBG("Alarm Enable Register 0x%02x", reg.byte);
    rc = write_register(PCF85263A_RTC_MODE_ALARM_ENABLE_REG, reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to write register PCF85263A_RTC_MODE_ALARM_ENABLE_REG error %d", rc);
        return rc;
    }

    return 0;
}

int pcf85263a_alarm_disable_type_1(void)
{
    pcf85263a_rtc_alarm_enable_reg_t reg = {0x00};
    int rc = 0;

    rc = read_register(PCF85263A_RTC_MODE_ALARM_ENABLE_REG, &reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to read register PCF85263A_RTC_MODE_ALARM_ENABLE_REG error %d", rc);
        return rc;
    }

    reg.sec_a1e = 0;
    reg.min_a1e = 0;
    reg.hr_a1e = 0;
    reg.day_a1e = 0;
    reg.mon_a1e = 0;

    rc = write_register(PCF85263A_RTC_MODE_ALARM_ENABLE_REG, reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to write register PCF85263A_RTC_MODE_ALARM_ENABLE_REG error %d", rc);
        return rc;
    }

    return 0;
}

int pcf85263a_alarm_config_type_2(pcf85263a_alarm_type_2_config_t config) {
    int rc = 0;
    struct pcf85263_data *data = &m_pcf85263_data;

    data->rtc_alm2_registers.rtc_min.minute_alarm = bin2bcd(config.minutes);

    if (data->osc_registers.hour_mode == PCF85263A_RTC_HOUR_MODE_24) {
        data->rtc_alm2_registers.rtc_hours.hr_alarm1_24mode = bin2bcd(config.hours);
    } else {
        if (config.hours >= 12) {
            config.hours = config.hours - 12;
            data->rtc_alm2_registers.rtc_hours.ampm = 1;
        } else {
            data->rtc_alm2_registers.rtc_hours.ampm = 0;
        }
        data->rtc_alm2_registers.rtc_hours.hr_alarm1_12mode = bin2bcd(config.hours);
    }

    data->rtc_alm2_registers.rtc_weekday.wday_alarm = bin2bcd(config.weekdays);

	/* Write to device */
	rc = write_data_block(PCF85263A_RTC_MODE_MINUTES_ALARM2_REG, 
        PCF85263A_REGISTER_COUNT(PCF85263A_RTC_MODE_MINUTES_ALARM2_REG, PCF85263A_RTC_MODE_WEEKEDAY_ALARM2_REG));
    
    if (rc != 0) {
        LOG_ERR("Failed to set Alarm Type 2");
        return rc;
    }

    LOG_DBG("Configured Alarm Type 2 successful");
    return rc;
}

int pcf85263a_alarm_enable_type_2(pcf85263a_alarm_type_2_flag_t flag) {
    pcf85263a_rtc_alarm_enable_reg_t reg = {0x00};
    int rc = 0;

    rc = read_register(PCF85263A_RTC_MODE_ALARM_ENABLE_REG, &reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to read register PCF85263A_RTC_MODE_ALARM_ENABLE_REG error %d", rc);
        return rc;
    }

    reg.min_a2e = flag.enable_minutes;
    reg.hr_a2e = flag.enable_hours;
    reg.wday_a2e = flag.enable_weekdays;

    rc = write_register(PCF85263A_RTC_MODE_ALARM_ENABLE_REG, reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to write register PCF85263A_RTC_MODE_ALARM_ENABLE_REG error %d", rc);
        return rc;
    }

    return 0;
}

int pcf85263a_alarm_disable_type_2(void) {
    pcf85263a_rtc_alarm_enable_reg_t reg = {0x00};
    int rc = 0;

    rc = read_register(PCF85263A_RTC_MODE_ALARM_ENABLE_REG, &reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to read register PCF85263A_RTC_MODE_ALARM_ENABLE_REG error %d", rc);
        return rc;
    }

    reg.min_a2e = 0;
    reg.hr_a2e = 0;
    reg.wday_a2e = 0;

    rc = write_register(PCF85263A_RTC_MODE_ALARM_ENABLE_REG, reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to write register PCF85263A_RTC_MODE_ALARM_ENABLE_REG error %d", rc);
        return rc;
    }

    return 0;
}
void pcf85263a_interrupt_enable(pcf85263a_interrupt_flag_t flag) {
    pcf85263a_inta_reg_t reg = {0x00};
    int rc = 0;

    rc = read_register(PCF85263A_INTA_ENABLE_REG, &reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to read register PCF85263A_INTA_ENABLE_REG error %d", rc);
        return;
    }

    reg.wdiea = flag.enable_wdg;
    reg.bsiea = flag.enable_battery_switch;
    reg.tsriea = flag.enable_timestamp;
    reg.a2iea = flag.enable_alarm_2;
    reg.a1iea = flag.enable_alarm_1;
    reg.oiea = flag.enable_offset_correction;
    reg.piea = flag.enable_periodic;
    reg.ilpa = flag.enable_level_pulse;
    LOG_DBG("Interrupt Register 0x%02x", reg.byte);
    rc = write_register(PCF85263A_INTA_ENABLE_REG, reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to write register PCF85263A_INTA_ENABLE_REG error %d", rc);
        return;
    }
}

void pcf85263a_set_interrupt_io(bool enable) {
    pcf85263a_pin_io_reg_t reg = {0x00};
    int rc = 0;

    rc = read_register(PCF85263A_PIN_IO_REG, &reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to read register PCF85263A_PIN_IO_REG error %d", rc);
        return;
    }

    reg.intapm = enable ? 2 : 0;
    LOG_DBG("IO Register 0x%02x", reg.byte);
    rc = write_register(PCF85263A_PIN_IO_REG, reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to write register PCF85263A_PIN_IO_REG error %d", rc);
        return;
    }
}

void pcf85263a_set_clkpin(bool enable)
{
    pcf85263a_pin_io_reg_t reg = {0x00};
    int rc = 0;

    rc = read_register(PCF85263A_PIN_IO_REG, &reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to read register PCF85263A_PIN_IO_REG error %d", rc);
        return;
    }

    reg.cklpm = enable ? 0 : 1;
    LOG_DBG("IO Register 0x%02x", reg.byte);
    rc = write_register(PCF85263A_PIN_IO_REG, reg.byte);
    if (rc != 0) {
        LOG_ERR("Failed to write register PCF85263A_PIN_IO_REG error %d", rc);
        return;
    }
}

uint8_t pcf85263a_get_alarm_min_type_1(void) {
    uint8_t min_buf[1] = {0x00};
    int rc = read_register(PCF85263A_RTC_MODE_MINUTES_ALARM1_REG, min_buf);
    if (rc == 0) {
        return bcd2bin(min_buf[0]);
    }

    return 0;
}
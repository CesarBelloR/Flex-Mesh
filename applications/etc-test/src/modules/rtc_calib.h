#ifndef RTC_CALIB_H__
#define RTC_CALIB_H__

/** Calibrate the PCF85263 RTC using hardware timers and a reference clock.
 * For this calibration to succeed, a reference clock needs to be connected
 * to the DEV_PIN. The reference clock speed needs to be 2.5 MHz.
 *
 * After calibration, write the offset to the RTC and save it to flash.
 * 
*/
int calibrate_rtc(void);

/** Set the RTC calibration offset in ppm and save it to flash.
 * 
 * Note: Passing float instead of the pointer resulted in weird behaviour.
 * Float inside this function would be different than value before calling function.
 * 
 * @param offset_ppm Pointer to the float offset in ppm.
*/
int set_rtc_offset(float *offset_ppm);

/** Get the currently saved offset in PPM
 * 
*/
float get_rtc_offset(void);

/** Initialize the RTC with the offset saved in flash. 
 * 
*/
void rtc_calib_init(void);

#endif /* RTC_CALIB_H__ */
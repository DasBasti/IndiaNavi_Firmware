/*
 * generic RTC driver
 *
 *  Created on: Jan 6, 2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_HW_RTC_H_
#define PLATINENMACHER_HW_RTC_H_

#include <stdint.h>
#include "error.h"

typedef struct {
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
} rtc_time_t;

typedef struct rtc {
	char *name;
	void (*onAlarm1)(struct rtc *rtc);
	void (*onAlarm2)(struct rtc *rtc);
	void *rtc_handle;
} rtc_t;

rtc_t* rtc_init(char *name);
error_code_t rtc_set_time(rtc_time_t *newTime);
rtc_time_t* rtc_get_time(rtc_t *rtc);
void rtc_allarm_cb(rtc_t* rtc);
#endif /* PLATINENMACHER_HW_RTC_H_ */

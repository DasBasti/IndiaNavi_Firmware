/*
 * rtc.c
 *
 *  Created on: Jan 6, 2021
 *      Author: bastian
 */

#include "hw/rtc.h"
#include "memory.h"

rtc_t* rtc_init(char *name) {
	rtc_t* rtc = RTOS_Malloc(sizeof(rtc_t));
	rtc->name = name;
	return rtc;
}

error_code_t rtc_set_time(rtc_time_t *newTime) {
	return PM_OK;
}

rtc_time_t* rtc_get_time(rtc_t *rtc) {
	rtc_time_t* time = RTOS_Malloc(sizeof(rtc_time_t));
	return time;
}

void rtc_allarm_cb(rtc_t *rtc) {
	if(rtc->onAlarm1){
		rtc->onAlarm1(rtc);
	}
	if(rtc->onAlarm2){
		rtc->onAlarm2(rtc);
	}
}


/*
 * Battery, charger and deep sleep
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "battery_state.h"
#include "gui.h"
#include "navi/gps.h"
#include "navi/power.h"
#include "navi/system_events.h"
#include "pins.h"

static const char* TAG = "POWER";

#define POWER_TASK_STACK_SIZE (1024 * 6)

static TaskHandle_t power_task;

static volatile int32_t battery_level = 0;
static volatile bool charging;
static volatile bool state_known; // battery level and charger were read

/* set while the battery is empty: the device then wakes up regularly to look for a charger */
static volatile bool wake_up_for_charger;

int32_t power_battery_level(void)
{
    return battery_level;
}

bool power_is_charging(void)
{
    return charging;
}

bool power_state_known(void)
{
    return state_known;
}

void power_set_wake_up_for_charger(bool wake_up)
{
    wake_up_for_charger = wake_up;
}

/*
 * The charger was connected or removed. The event starts or stops WiFi, it must not get lost in a full queue.
 */
static void set_charging(bool is_charging)
{
    system_post_event(is_charging ? TASK_EVENT_START_CHARGING : TASK_EVENT_STOP_CHARGING, portMAX_DELAY);
    charging = is_charging;
}

/**
 * @brief Function to read battery voltage
 * @retval int: battery level in percent
 */
static int readBatteryPercent(adc_oneshot_unit_handle_t adc_handle, charger_state_t* charger)
{
    int batteryVoltage;
    int chargerVoltage;

    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, VBAT_ADC, &batteryVoltage));
    ESP_LOGI(TAG, "Battery Voltage: %d", batteryVoltage);
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, VIN_ADC, &chargerVoltage));
    ESP_LOGI(TAG, "Charger Voltage: %d", chargerVoltage);

    switch (charger_state_update(charger, chargerVoltage, batteryVoltage)) {
    case CHARGER_CONNECTED:
        set_charging(true);
        break;
    case CHARGER_REMOVED:
        set_charging(false);
        break;
    default:
        break;
    }

    const int min = 1550;
    const int max = 2330;
    if (batteryVoltage > max)
        return 100;

    if (batteryVoltage < min)
        return 0;

    /* ganz simpler dreisatz, den man in wenigen
       sekunden im Kopf lösen kann */
    int value = batteryVoltage - min;

    return value * 100 / (max - min);
}

error_code_t enter_deep_sleep_if_not_charging()
{
    if (charging)
        return DEFERRED;

    // no GPIO signals a charger in deep sleep (VIN is an analog input), so look for it from time to time
    if (wake_up_for_charger)
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(BATTERY_EMPTY_WAKEUP_INTERVAL_S * 1000000ULL));

    const gpio_config_t config = {
        .pin_bit_mask = BIT(BTN),
        .mode = GPIO_MODE_INPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(BTN, BTN_LEVEL));
    rtc_gpio_pullup_en(BTN);
    rtc_gpio_pulldown_dis(BTN);

    ESP_LOGI(TAG, "enter deep sleep now...");
    esp_deep_sleep_start();
    return PM_OK;
}

/**
 * Short look at the charger after a timer wakeup, before anything is started.
 * The power task uses the ADC later, so the unit is released again.
 */
bool power_charger_connected_at_wakeup(void)
{
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    int battery = 0, charger = 0;
    if (adc_oneshot_new_unit(&init_config, &adc) != ESP_OK)
        return false;
    if (adc_oneshot_config_channel(adc, VBAT_ADC, &config) == ESP_OK
        && adc_oneshot_config_channel(adc, VIN_ADC, &config) == ESP_OK
        && adc_oneshot_read(adc, VBAT_ADC, &battery) == ESP_OK
        && adc_oneshot_read(adc, VIN_ADC, &charger) == ESP_OK) {
        adc_oneshot_del_unit(adc);
        return charger - 5 > battery;
    }
    adc_oneshot_del_unit(adc);
    return false;
}

/* time the battery empty screen may take, then the device sleeps without it */
#define BATTERY_EMPTY_SCREEN_TIMEOUT_US (120LL * 1000 * 1000)

static void sleep_with_empty_battery(void)
{
    gps_enter_standby();
    enter_deep_sleep_if_not_charging();
}

/**
 * The battery is empty: show it on the display and power down. Called after every reading while it is empty.
 */
static void power_down_battery_empty(int64_t* empty_since_us)
{
    if (!gui_display_ready()) {
        // the display waits for a charged battery and still shows the screen from before
        ESP_LOGW(TAG, "Battery empty, the display is off, sleep");
        sleep_with_empty_battery();
        return;
    }
    int64_t now = esp_timer_get_time();
    if (*empty_since_us < 0) {
        ESP_LOGW(TAG, "Battery empty (%ld%%), show it and power down", battery_level);
        *empty_since_us = now;
        gui_set_app_mode(APP_MODE_BATTERY_EMPTY);
    } else if (now - *empty_since_us > BATTERY_EMPTY_SCREEN_TIMEOUT_US) {
        ESP_LOGE(TAG, "Battery empty screen was not shown, sleep without it");
        sleep_with_empty_battery();
    }
}

static void power_task_main(void* argument)
{
    TickType_t delay_time = 10;
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, VBAT_ADC, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, VIN_ADC, &config));

    battery_state_t battery_state = { 0 };
    charger_state_t charger_state = { 0 };
    int64_t battery_empty_since_us = -1;
    for (;;) {
        battery_level = readBatteryPercent(adc1_handle, &charger_state);
        if (!charger_state_is_pending(&charger_state))
            state_known = true;
        if (battery_state_update(&battery_state, battery_level, charging)) {
            wake_up_for_charger = true;
            power_down_battery_empty(&battery_empty_since_us);
        } else {
            battery_empty_since_us = -1;
            if (!gui_battery_empty_shown())
                wake_up_for_charger = false;
            // charging on the battery empty screen: turn on as soon as the battery has some charge
            if (gui_battery_empty_shown() && battery_state_recovered(battery_level, charging)) {
                ESP_LOGI(TAG, "Battery charged to %ld%%, turn on", battery_level);
                gui_set_app_mode(APP_MODE_GPS_CREATE);
                trigger_rendering();
            }
        }

        if (charging || charger_state_is_pending(&charger_state))
            delay_time = 1000;
        else if (battery_state_is_low(&battery_state))
            delay_time = BATTERY_EMPTY_CHECK_INTERVAL_MS;
        else
            delay_time = 60000;
        vTaskDelay(pdMS_TO_TICKS(delay_time));
    }
}

void power_start_task(void)
{
    if (power_task)
        return;
    if (xTaskCreate(power_task_main, "power", POWER_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY, &power_task) != pdPASS) {
        ESP_LOGE(TAG, "Can not create power task");
        power_task = NULL;
    }
}


#include "display.h"
#include "gui.h"
#include "gui/graph.h"
#include "gui/label.h"
#include "pins.h"
#include "tasks.h"

#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
static graph_point_t* battery_voltage;
static graph_point_t* charger_voltage;
static uint32_t battery_voltage_offset = 0;
static const uint32_t battery_voltage_num = 4096; // adjust to a sensible amount
static graph_t* graph;
static graph_t* graph_zahl;
char bat_graph[10];
char bat_graph1[10];
char bat_graph2[10];
static label_t* bat_graph_label;
static label_t* bat_graph_label1;
static label_t* bat_graph_label2;
static label_t* gps;
char gps_info[1024];
static label_t* task_info_label;
char task_info[1024];

extern uint32_t gps_ticks;

static label_t* sd_info_label;
char sd_info[1024];

error_code_t record_battery_voltage(const display_t* dsp, void* comp)
{
    // record the battery voltage for this iteration
    int currentBatteryVoltage;
    int currentChargerVoltage;
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_7, &config));
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &currentBatteryVoltage));
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_7, &currentChargerVoltage));

    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));

    if (battery_voltage_offset < battery_voltage_num && charger_voltage && battery_voltage) {
        charger_voltage[battery_voltage_offset].value = (float)currentChargerVoltage;
        battery_voltage[battery_voltage_offset].value = (float)currentBatteryVoltage;
        battery_voltage_offset++;
    }

    if (graph && graph_zahl && bat_graph_label1 && bat_graph_label2) {
        graph->data_len = battery_voltage_offset;
        graph_zahl->data_len = battery_voltage_offset;
        if (xSemaphoreTake(print_semaphore, 1000)) {
            snprintf(bat_graph, 10, "%lu pts", battery_voltage_offset);
            snprintf(bat_graph1, 10, "BV: %u", currentBatteryVoltage);
            snprintf(bat_graph2, 10, "CV: %u", currentChargerVoltage);
            xSemaphoreGive(print_semaphore);
        }
    }

    return PM_OK;
}

error_code_t update_gps_info_label(const display_t* dsp, void* comp)
{
    gpio_t pwr = {};
    pwr.pin = GPS_VCC_nEN;
    if (map_position && xSemaphoreTake(print_semaphore, 1000)) {
        snprintf(gps_info, 1024, "GPS Info\n Power: %d\n Fix: %d HDOP:%f\n Lat: %f\n Lon:%f\n Ele:%f\n Sats in view/use %d/%d\n Ticks: %lu",
            !gpio_read(&pwr),
            map_position->fix,
            map_position->hdop,
            map_position->latitude,
            map_position->longitude,
            map_position->altitude,
            map_position->satellites_in_view,
            map_position->satellites_in_use,
            gps_ticks);

        xSemaphoreGive(print_semaphore);
    }

    return label_render(dsp, comp);
}
const char* tab_format[] = {
    " %s\t\t%u/%d%%\n",
    " %s\t%u/%d%%\n",
};
const char* tab_format_less[] = {
    " %s\t\t%u/<1%%\n",
    " %s\t%u/<1%%\n",
};
error_code_t update_task_info_label(const display_t* dsp, void* comp)
{
    TaskStatus_t* pxTaskStatusArray;
    volatile UBaseType_t uxArraySize, x;
    uint32_t ulTotalRunTime, ulStatsAsPercentage;
    /* Make sure the write buffer does not contain a string. */
    char* pcWriteBuffer = task_info;

    /* Take a snapshot of the number of tasks in case it changes while this
    function is executing. */
    uxArraySize = uxTaskGetNumberOfTasks();

    /* Allocate a TaskStatus_t structure for each task.  An array could be
    allocated statically at compile time. */
    pxTaskStatusArray = RTOS_Malloc(uxArraySize * sizeof(TaskStatus_t));

    if (pxTaskStatusArray != NULL && xSemaphoreTake(print_semaphore, 1000)) {
        /* Generate raw status information about each task. */
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray,
            uxArraySize,
            &ulTotalRunTime);

        /* For percentage calculations. */
        ulTotalRunTime /= 100UL;

        sprintf(pcWriteBuffer, "Task\t\tRuntime/Percent\n");
        pcWriteBuffer += strlen((char*)pcWriteBuffer);

        /* Avoid divide by zero errors. */
        if (ulTotalRunTime > 0) {
            /* For each populated position in the pxTaskStatusArray array,
            format the raw data as human readable ASCII data. */
            for (x = 0; x < uxArraySize; x++) {
                /* What percentage of the total run time has the task used?
                This will always be rounded down to the nearest integer.
                ulTotalRunTimeDiv100 has already been divided by 100. */
                ulStatsAsPercentage = pxTaskStatusArray[x].ulRunTimeCounter / ulTotalRunTime;

                uint8_t tabs = (strlen(pxTaskStatusArray[x].pcTaskName) + 1) / 8;

                if (ulStatsAsPercentage > 0UL) {
                    sprintf(pcWriteBuffer, tab_format[tabs],
                        pxTaskStatusArray[x].pcTaskName,
                        pxTaskStatusArray[x].ulRunTimeCounter,
                        ulStatsAsPercentage);
                } else {
                    /* If the percentage is zero here then the task has
                    consumed less than 1% of the total run time. */
                    sprintf(pcWriteBuffer, tab_format_less[tabs],
                        pxTaskStatusArray[x].pcTaskName,
                        pxTaskStatusArray[x].ulRunTimeCounter);
                }

                pcWriteBuffer += strlen((char*)pcWriteBuffer);
            }
        }

        /* The array is no longer needed, free the memory it consumes. */
        RTOS_Free(pxTaskStatusArray);
        xSemaphoreGive(print_semaphore);
    }
    return label_render(dsp, comp);
}

error_code_t update_sd_info_label(const display_t* dsp, void* comp)
{
    gpio_t pwr = {};
    pwr.pin = SD_VCC_nEN;
    if (xSemaphoreTake(print_semaphore, 1000)) {
        snprintf(sd_info, 1024, "SD Info\n Power: %d\n Semaphore Count: %d",
            !gpio_read(&pwr),
            uxSemaphoreGetCount(sd_semaphore));

        xSemaphoreGive(print_semaphore);
    }

    return label_render(dsp, comp);
}

void test_screen_create(const display_t* display)
{
    // add a hook to record the current battery voltage each render execution
    battery_voltage = RTOS_Malloc(sizeof(graph_point_t) * battery_voltage_num);
    charger_voltage = RTOS_Malloc(sizeof(graph_point_t) * battery_voltage_num);
    add_pre_render_callback(record_battery_voltage);

    graph = graph_create(0, display->size.height - 200, display->size.width, 200, battery_voltage, battery_voltage_offset, &f8x8);
    graph_set_range(graph, 1500, 2900);
    graph->line_color = BLUE;
    graph->background_color = WHITE;

    graph_zahl = graph_create(0, display->size.height - 200, display->size.width, 200, charger_voltage, battery_voltage_offset, &f8x8);
    graph_set_range(graph_zahl, 1500, 2900);
    graph_zahl->line_color = RED;
    graph_zahl->background_color = TRANSPARENT;

    bat_graph_label = label_create(bat_graph, &f8x8, 10, graph->box.top + 2, display->size.width - 10, 10);
    bat_graph_label->alignHorizontal = RIGHT;

    bat_graph_label1 = label_create(bat_graph1, &f8x8, 10, graph->box.top - 10, display->size.width - 10, 10);
    bat_graph_label1->alignHorizontal = RIGHT;
    bat_graph_label1->textColor = BLUE;

    bat_graph_label2 = label_create(bat_graph2, &f8x8, 10, graph->box.top - 20, display->size.width - 10, 10);
    bat_graph_label2->alignHorizontal = RIGHT;
    bat_graph_label2->textColor = RED;

    add_to_render_pipeline(graph_renderer, graph, RL_GUI_ELEMENTS);
    // add_to_render_pipeline(graph_renderer, graph_zahl, RL_GUI_ELEMENTS);
    add_to_render_pipeline(label_render, bat_graph_label, RL_GUI_ELEMENTS);
    add_to_render_pipeline(label_render, bat_graph_label1, RL_GUI_ELEMENTS);
    add_to_render_pipeline(label_render, bat_graph_label2, RL_GUI_ELEMENTS);

    gps = label_create(gps_info, &f8x8, 5, 50, display->size.width - 5, 9 * (f8x8.height + 2));

    add_to_render_pipeline(update_gps_info_label, gps, RL_GUI_ELEMENTS);

    task_info_label = label_create(task_info, &f8x8, 5, gps->box.top + gps->box.height + 2, gps->box.width, 15 * (f8x8.height + 2));
    add_to_render_pipeline(update_task_info_label, task_info_label, RL_GUI_ELEMENTS);

    sd_info_label = label_create(sd_info, &f8x8, 5, task_info_label->box.top + task_info_label->box.height + 2, gps->box.width, 4 * (f8x8.height + 2));
    add_to_render_pipeline(update_sd_info_label, sd_info_label, RL_GUI_ELEMENTS);
}

/*
 * GPS Parser Task
 *
 *  Created on: Jan 7, 2021
 *      Author: bastian
 */

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "gui.h"
#include "pins.h"
#include "tasks.h"
#include "time.h"
#include <esp_log.h>
#include <sys/time.h>

#include "l96.h"
#include "nmea_parser.h"
#include "pmtk_parser.h"
#include "pq_parser.h"

#include <math.h>
#include <string.h>

#include <driver/uart.h>
#include <esp_log.h>
#include <icons_32.h>

static const char* TAG = "GPS";
char timeString[20];

nmea_parser_handle_t nmea_hdl;
static async_file_t AFILE;
static async_file_t BFILE;
char timezone_file[100];
uint8_t hour;
uint32_t gps_ticks = 0;

QueueHandle_t gpstrack_queue;

static char gpx_header[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"  standalone=\"yes\"?>\n"
                           "<gpx version=\"1.1\" creator=\"IndiaNavi\" xmlns=\"http://www.topografix.com/GPX/1/1\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd\" xmlns:oa=\"http://www.outdooractive.com/GPX/Extensions/1\">\n"
                           "<metadata>\n"
                           "<name>WanderNavi IndiaNavi</name>\n"
                           "<link href=\"https://platinenmacher.tech\"/>\n"
                           "<time>%s</time>\n"
                           "<extensions><oa:oaCategory>hikingTourTrail</oa:oaCategory></extensions>\n"
                           "</metadata>\n"
                           "<trk>\n"
                           "<name>IndiaNavi GPS Log</name>\n"
                           "<type>hikingTourTrail</type>\n"
                           "<trkseg>\n";

static char* gpx_footer = "</trkseg></trk></gpx>\n";

typedef struct {
    map_position_t position;
    time_t timestamp;
} log_position_t;

static map_position_t current_position = {
#ifdef NO_GPS
    .longitude = 8.581875,
    .latitude = 49.626846,
    .satellites_in_use = 3,
    .satellites_in_view = 10,
    .fix = GPS_FIX_GPS,
#else
    .fix = GPS_FIX_INVALID,
#endif
};

bool gps_is_position_known()
{
    return current_position.fix != GPS_FIX_INVALID;
}

/**
 * @brief GPS Event Handler
 *
 * @param event_handler_arg handler specific arguments
 * @param event_base event base, here is fixed to ESP_NMEA_EVENT
 * @param event_id event id
 * @param event_data event specific arguments
 */
static void
gps_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
#ifdef NO_GPS
    return;
#endif
    const gps_t* _gps;
    switch (event_id) {
    case GPS_UPDATE:
        _gps = (gps_t*)event_data;
        current_position.longitude = _gps->longitude;
        current_position.latitude = _gps->latitude;
        current_position.altitude = _gps->altitude;
        current_position.hdop = _gps->dop_h;
        current_position.fix = _gps->fix;
        current_position.satellites_in_use = _gps->sats_in_use;
        current_position.satellites_in_view = _gps->sats_in_view;

        struct tm t = { 0 };               // Initalize to all 0's
        t.tm_year = _gps->date.year + 100; // This is year+100, so 121 = 2021
        t.tm_mon = _gps->date.month - 1;
        t.tm_mday = _gps->date.day;
        t.tm_hour = _gps->tim.hour + 1;
        t.tm_min = _gps->tim.minute;
        t.tm_sec = _gps->tim.second;

        struct timeval tv = { mktime(&t), 0 }; // epoch time (seconds)
        settimeofday(&tv, NULL);

        gps_ticks++;
        if (gpstrack_queue != NULL) {
            log_position_t log_position = { .position = current_position, .timestamp = mktime(&t) };
            xQueueSendFromISR(gpstrack_queue, &log_position, 0);
        }
        break;
    case GPS_UNKNOWN:
        /* print unknown statements */
        char message_buf[255];
        strncpy(message_buf, (char*)event_data, 255);
        ESP_LOGW(TAG, "Unknown statement:%s", message_buf);
        break;
    default:
        break;
    }
}

void gps_stop_parser()
{
    ESP_LOGI(TAG, "stop parser");
    if (nmea_hdl) {
        nmea_parser_remove_handler(nmea_hdl, gps_event_handler);
        nmea_parser_deinit(nmea_hdl);
    }
}

void gps_enter_standby()
{
    ESP_ERROR_CHECK(nmea_send_command(nmea_hdl, L96_ENTER_STANDBY));
}

void StartGpsTask(void const* argument)
{
    uint8_t minute = 0;
    /* make current gps position known globally */
    map_position = &current_position;

    ESP_LOGI(TAG, "init gpio %d\n\r", GPS_VCC_nEN);
    /* create power regulator */
    regulator_t* reg;
    gpio_t* reg_gpio = gpio_create(OUTPUT, 0, GPS_VCC_nEN);
    reg_gpio->onValue = GPIO_RESET;

    ESP_LOGI(TAG, "init regulator\n\r");
    reg = regulator_gpio_create(reg_gpio);

    /* L96 module can be restarted by driving the RESET to a low level voltage for at least 10ms and then releasing it.*/
    vTaskDelay(pdMS_TO_TICKS(100));
    reg->enable(reg);

    ESP_LOGI(TAG, "Wait for SD-Card");
    waitForSDInit();
    ESP_LOGI(TAG, "Load timezone information");
    async_file_t* tz_file = &AFILE;
    tz_file->filename = "//TIMEZONE";
    tz_file->dest = timezone_file;
    tz_file->loaded = false;
    loadFile(tz_file);
    uint8_t delay = 0;
    ESP_LOGI(TAG, "Load timezone information queued");
    while (!tz_file->loaded) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        if (delay++ == 20)
            break;
    }
    ESP_LOGI(TAG, "Load timezone information loaded");
    if (tz_file->loaded) {
        char tz[50] = { 0 };
        readline(timezone_file, tz);
        setenv("TZ", tz, 1);
        ESP_LOGI(TAG, "Set timezone to: %s", tz);
    } else {
        setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
        ESP_LOGI(TAG, "Use default timezone");
    }
    tzset();

    gpstrack_queue = xQueueCreate(10, sizeof(log_position_t));

    ESP_LOGI(TAG, "UART config");
    /* NMEA parser configuration */
    nmea_parser_config_t config = {
        .uart = {
            .uart_port = UART_NUM_2,
            .rx_pin = GPS_UART2_RX,
            .tx_pin = GPS_UART2_TX,
            .baud_rate = 9600,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .event_queue_size = 64,
        },
        .plugins = { { .detect = pmtk_detect, .parse = pmtk_parse }, { .detect = pq_detect, .parse = pq_parse } }
    };
    /* init NMEA parser library */
    nmea_hdl = nmea_parser_init(&config);
    /* register event handler for NMEA parser library */
    nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
    // send initial commands to GPS module
    ESP_ERROR_CHECK(nmea_send_command(nmea_hdl, L96_SEARCH_GPS_GLONASS_GALILEO));
    ESP_ERROR_CHECK(nmea_send_command(nmea_hdl, L96_ENTER_GLP));

    async_file_t* gps_track = &BFILE;
    gps_track->filename = "//log.gpx";
    if (PM_OK == openFileForWriting(gps_track)) {
        char* gpxbuffer = RTOS_Malloc(1024);
        if (gpxbuffer) {
            struct tm timeinfo;
            time_t now;
            time(&now);
            localtime_r(&now, &timeinfo);
            ctime_r(&now, timeString);
            save_snprintf(gpxbuffer, 1024, gpx_header, timeString);
            uint32_t bytes_written;
            writeToFile(gps_track, gpxbuffer, strlen(gpxbuffer), &bytes_written);
            ESP_LOGI(TAG, "Written %lu", bytes_written);
            RTOS_Free(gpxbuffer);
        }
    } else {
        ESP_LOGE(TAG, "Cannot open log file for writing");
    }

    for (;;) {
        // struct tm timeinfo;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm* timeinfo = localtime(&tv.tv_sec);
        if (clock_label && minute != timeinfo->tm_min) {
            trigger_rendering();
        }

        /* check for lon and lat to be a number */
        if ((current_position.fix != GPS_FIX_INVALID)) {
            ESP_LOGI(TAG, "Fix: %d", current_position.fix);
        }

        log_position_t position;
        char trkpt[] = "<trkpt lat=\"%f\" lon=\"%f\"><ele>%f</ele><time>%s</time></trkpt>\n";
        char trkpt_buf[255];
        if (gps_track->loaded) {
            while (xQueueReceive(gpstrack_queue, &position, 0) == pdTRUE) {
                if (position.position.fix == GPS_FIX_GPS) {
                    ctime_r(&position.timestamp, timeString);
                    save_snprintf(trkpt_buf, sizeof(trkpt_buf), trkpt, position.position.latitude, position.position.longitude, position.position.altitude, timeString);
                    uint32_t bytes_written;
                    writeToFile(gps_track, trkpt_buf, strlen(trkpt_buf), &bytes_written);
                    ESP_LOGI(TAG, "GPS queue: %f, %f, written %lu", position.position.latitude, position.position.longitude, bytes_written);
                }
            }
        }
        // Update every minute
        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}

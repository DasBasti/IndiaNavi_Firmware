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

#include "helper.h"
#include "l96.h"
#include "nmea_parser.h"
#include "pmtk_parser.h"
#include "pq_parser.h"

#include <math.h>
#include <string.h>

#include <driver/uart.h>
#include <esp_log.h>
#include <icons_16.h>

static const char* TAG = "GPS";

nmea_parser_handle_t nmea_hdl;
static async_file_t AFILE;
static async_file_t BFILE;
char timezone_file[100];
uint32_t gps_ticks = 0;

/* minimum time between two points in the track log */
#define TRACK_LOG_INTERVAL_S 5
/* time between attempts to open the track log if it failed */
#define TRACK_LOG_RETRY_S 60
/* the clock was never set before 2020-01-01 */
#define VALID_TIME_MIN 1577836800

QueueHandle_t gpstrack_queue;

static const char gpx_header[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"  standalone=\"yes\"?>\n"
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

static const char gpx_footer[] = "</trkseg></trk></gpx>\n";
static const char gpx_segment_start[] = "<trkseg>\n";
static const char gpx_new_segment[] = "</trkseg>\n<trkseg>\n";

/* ISO 8601 UTC time as required by GPX, e.g. 2021-01-07T12:34:56Z */
#define GPX_TIME_LEN sizeof("2021-01-07T12:34:56Z")

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

/* days since 1970-01-01 of a date in the proleptic Gregorian calendar */
static int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
    y -= m <= 2;
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = (uint32_t)(y - era * 400);
    const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

/*
 * Epoch time from the UTC date and time of the GPS module
 *
 * Returns false while the module has no valid time, the fields are empty
 * then and hold garbage.
 */
static bool gps_utc_time(const gps_t* gps, time_t* utc)
{
    if (!gps->valid)
        return false;
    // years are 2 digits counted from 2000
    if (gps->date.year < 20 || gps->date.year > 99
        || gps->date.month < 1 || gps->date.month > 12
        || gps->date.day < 1 || gps->date.day > 31
        || gps->tim.hour > 23 || gps->tim.minute > 59 || gps->tim.second > 60)
        return false;

    int32_t days = days_from_civil(2000 + gps->date.year, gps->date.month, gps->date.day);
    *utc = (time_t)days * 86400 + gps->tim.hour * 3600 + gps->tim.minute * 60 + gps->tim.second;
    return true;
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

        time_t utc;
        if (gps_utc_time(_gps, &utc)) {
            struct timeval tv = { utc, 0 };
            settimeofday(&tv, NULL);
        } else {
            utc = time(NULL);
        }

        gps_ticks++;
        if (gpstrack_queue != NULL) {
            log_position_t log_position = { .position = current_position, .timestamp = utc };
            // called from the parser task, not from an ISR
            xQueueSend(gpstrack_queue, &log_position, 0);
        }
        break;
    case GPS_UNKNOWN:
        /* print unknown statements */
        char message_buf[255];
        strncpy(message_buf, (char*)event_data, sizeof(message_buf) - 1);
        message_buf[sizeof(message_buf) - 1] = 0;
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
        nmea_hdl = NULL;
    }
}

void gps_enter_standby()
{
    if (!nmea_hdl) {
        ESP_LOGW(TAG, "parser not running, can not enter standby");
        return;
    }
    esp_err_t err = nmea_send_command(nmea_hdl, L96_ENTER_STANDBY);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "enter standby failed: %s", esp_err_to_name(err));
}

static void format_gpx_time(time_t t, char* buf, size_t size)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, size, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static bool write_track_log(async_file_t* log, const char* data)
{
    uint32_t len = strlen(data);
    uint32_t bytes_written;
    return writeToFile(log, (void*)data, len, &bytes_written) == PM_OK && bytes_written == len;
}

/*
 * Open the track log and place the write pointer in front of the GPX footer.
 *
 * The file is appended to across reboots and every write puts the footer
 * back, so the file is valid GPX after each point. Each boot starts a new
 * track segment unless the last one is still empty.
 */
static bool open_track_log(async_file_t* log)
{
    if (PM_OK != openFileForUpdate(log))
        return false;

    const uint32_t footer_len = strlen(gpx_footer);
    uint32_t size = f_size(log->file);
    bool ok;
    if (size == 0) {
        char* gpxbuffer = RTOS_Malloc(1024);
        if (!gpxbuffer) {
            closeFile(log);
            return false;
        }
        char time_buf[GPX_TIME_LEN];
        format_gpx_time(time(NULL), time_buf, sizeof(time_buf));
        save_snprintf(gpxbuffer, 1024, gpx_header, time_buf);
        ok = write_track_log(log, gpxbuffer) && write_track_log(log, gpx_footer);
        RTOS_Free(gpxbuffer);
    } else {
        // look at the end of the file: footer, and is the last segment empty?
        char tail[sizeof(gpx_segment_start) + sizeof(gpx_footer)] = { 0 };
        uint32_t tail_len = sizeof(tail) - 1;
        if (tail_len > size)
            tail_len = size;
        uint32_t bytes_read = 0;
        if (seekFile(log, size - tail_len) == PM_OK)
            readFromFile(log, tail, tail_len, &bytes_read);
        tail[bytes_read] = 0;

        char* footer = bytes_read >= footer_len ? tail + bytes_read - footer_len : NULL;
        if (footer && 0 == strcmp(footer, gpx_footer)) {
            *footer = 0;
            bool segment_empty = bytes_read == tail_len && tail_len == sizeof(tail) - 1
                && 0 == strcmp(tail, gpx_segment_start);
            ok = seekFile(log, size - footer_len) == PM_OK
                && (segment_empty || write_track_log(log, gpx_new_segment))
                && write_track_log(log, gpx_footer);
        } else {
            // no footer, written by an older firmware: continue at the end
            ESP_LOGW(TAG, "GPX footer missing in %s", log->filename);
            ok = seekFile(log, size) == PM_OK && write_track_log(log, gpx_footer);
        }
    }

    if (ok)
        ok = seekFile(log, f_size(log->file) - footer_len) == PM_OK;
    if (!ok) {
        ESP_LOGE(TAG, "Cannot prepare %s", log->filename);
        closeFile(log);
    }
    return ok;
}

static void log_track_point(async_file_t* log, const log_position_t* position)
{
    char time_buf[GPX_TIME_LEN];
    char trkpt_buf[255];
    format_gpx_time(position->timestamp, time_buf, sizeof(time_buf));
    save_snprintf(trkpt_buf, sizeof(trkpt_buf), "<trkpt lat=\"%f\" lon=\"%f\"><ele>%.1f</ele><time>%s</time></trkpt>\n%s",
        position->position.latitude, position->position.longitude, position->position.altitude, time_buf, gpx_footer);

    uint32_t pos = f_tell(log->file);
    bool ok = write_track_log(log, trkpt_buf);
    // the next point overwrites the footer
    if (seekFile(log, ok ? f_tell(log->file) - strlen(gpx_footer) : pos) != PM_OK)
        ESP_LOGE(TAG, "Cannot seek in %s", log->filename);
    ESP_LOGI(TAG, "GPS log: %f, %f %s", position->position.latitude, position->position.longitude, ok ? "written" : "failed");
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
    tz_file->dest_size = sizeof(timezone_file);
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
        readline_n(timezone_file, tz, sizeof(tz));
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
    if (nmea_hdl) {
        /* register event handler for NMEA parser library */
        nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
        // send initial commands to GPS module
        if (nmea_send_command(nmea_hdl, L96_SEARCH_GPS_GLONASS_GALILEO) != ESP_OK
            || nmea_send_command(nmea_hdl, L96_ENTER_GLP) != ESP_OK)
            ESP_LOGE(TAG, "Sending initial commands failed");
    } else {
        ESP_LOGE(TAG, "NMEA parser init failed");
    }

    async_file_t* gps_track = &BFILE;
    gps_track->filename = "//log.gpx";
    bool track_log_open = open_track_log(gps_track);
    if (!track_log_open)
        ESP_LOGE(TAG, "Cannot open log file for writing");
    time_t last_logged = 0;
    TickType_t last_open_attempt = xTaskGetTickCount();

    for (;;) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm timeinfo;
        localtime_r(&tv.tv_sec, &timeinfo);
        if (clock_label && minute != timeinfo.tm_min) {
            minute = timeinfo.tm_min;
            trigger_rendering();
        }

        log_position_t position;
        if (!gpstrack_queue) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (xQueueReceive(gpstrack_queue, &position, pdMS_TO_TICKS(1000)) != pdTRUE)
            continue;
        if (position.position.fix == GPS_FIX_INVALID || position.timestamp < VALID_TIME_MIN)
            continue;
        if (position.timestamp - last_logged < TRACK_LOG_INTERVAL_S)
            continue;
        if (!track_log_open && xTaskGetTickCount() - last_open_attempt >= pdMS_TO_TICKS(TRACK_LOG_RETRY_S * 1000)) {
            last_open_attempt = xTaskGetTickCount();
            track_log_open = open_track_log(gps_track);
        }
        if (track_log_open) {
            log_track_point(gps_track, &position);
            last_logged = position.timestamp;
        }
    }
}

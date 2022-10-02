/*
 * GPS Parser Task
 *
 *  Created on: Jan 7, 2021
 *      Author: bastian
 */

#include <freertos/FreeRTOS.h>
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

#include <math.h>
#include <string.h>

#include "icons_32/icons_32.h"
#include <driver/uart.h>
#include <esp_log.h>

static const char* TAG = "GPS";
char timeString[20];

nmea_parser_handle_t nmea_hdl;
async_file_t AFILE;
char timezone_file[100];
uint8_t hour;
waypoint_t* waypoints = NULL;

static map_position_t current_position = {
#ifdef NO_GPS
    .longitude = 8.68575379,
    .latitude = 49.7258546,
    .satellites_in_use = 3,
    .satellites_in_view = 10,
    .fix = GPS_FIX_GPS,
#else
    .fix = GPS_FIX_INVALID,
#endif
};

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
        //ESP_LOGI(TAG, "%d:%d:%d", _gps->tim.hour, _gps->tim.minute, _gps->tim.second);
        ESP_LOGI(TAG, "sats: %d/%d", _gps->sats_in_use, _gps->sats_in_view);
        current_position.satellites_in_use = _gps->sats_in_use;
        current_position.satellites_in_view = _gps->sats_in_view;
        if (hour != _gps->tim.hour) {
            hour = _gps->tim.hour;
            struct tm t = { 0 };               // Initalize to all 0's
            t.tm_year = _gps->date.year + 100; // This is year+100, so 121 = 2021
            t.tm_mon = _gps->date.month - 1;
            t.tm_mday = _gps->date.day;
            t.tm_hour = _gps->tim.hour;
            t.tm_min = _gps->tim.minute;
            t.tm_sec = _gps->tim.second;

            struct timeval tv = { mktime(&t), 0 }; // epoch time (seconds)
            settimeofday(&tv, NULL);
        }
        break;
    case GPS_UNKNOWN:
        /* print unknown statements */
        ESP_LOGW(TAG, "Unknown statement:%s", (char*)event_data);
        break;
    default:
        break;
    }
}

void calculate_waypoints(waypoint_t* wp_t)
{
    //TODO: into map component
    /*float _xf, _yf;
	_xf = flon2tile(wp_t->lon, tile_zoom);
	wp_t->tile_x = floor(_xf);
	_yf = flat2tile(wp_t->lat, tile_zoom);
	wp_t->tile_y = floor(_yf);
	wp_t->pos_x = floor((_xf - wp_t->tile_x) * 256); // offset from tile 0
	wp_t->pos_y = floor((_yf - wp_t->tile_y) * 256); // offset from tile 0
	*/
    //ESP_LOGI(TAG, "Calculate waypoint %d @ %d / %d", wp_t->tile_x, wp_t->tile_y, wp_t->num);
}

/* Change Zoom level and trigger rendering. 
   Tiles must be available on SD card! 
*/
/**
 * Callbacks from renderer
 */

error_code_t render_waypoint_marker(const display_t* dsp, void* comp)
{
    // TODO: move to map component
    /*
	waypoint_t *wp = (waypoint_t *)comp;
	if ((current_position.fix!= GPS_FIX_INVALID) && (wp->tile_x != 0) && (wp->tile_y != 0))
	{
		for (uint8_t i = 0; i < 2; i++)
			for (uint8_t j = 0; j < 3; j++)
			{
				uint8_t idx = i * 3 + j;
				// check if we have the tile on the screen
				if (map_tiles[idx].x == wp->tile_x && map_tiles[idx].y == wp->tile_y)
				{
					uint16_t x = wp->pos_x + (i * 256);
					uint16_t y = wp->pos_y + (j * 256);
					if (!zoom_level_selected) // close up
						display_circle_fill(dsp, x, y, 3, wp->color);
					else // far
						display_circle_fill(dsp, x, y, 2, wp->color);
					//ESP_LOGI(TAG, "WP @ x/y %d/%d tile %d/%d", x, y, wp->tile_x, wp->tile_y);
					if (wp->next)
					{
						// line to next waypoint
						uint32_t x2 = wp->next->pos_x + (wp->next->tile_x - wp->tile_x + i) * 256;
						uint32_t y2 = wp->next->pos_y + (wp->next->tile_y - wp->tile_y + j) * 256;
						//ESP_LOGI(TAG, "WP Line to %d/%d tile %d/%d", x2,y2, wp->next->tile_x, wp->next->tile_y);
						display_line_draw(dsp, x, y, x2, y2, wp->color);
						if (!zoom_level_selected)
						{
							display_line_draw(dsp, x + 1, y, x2 + 1, y2, wp->color);
							display_line_draw(dsp, x - 1, y, x2 - 1, y2, wp->color);
						}
						uint16_t vec_len = length(x, y, x2, y2);
						display_line_draw(dsp, x, y, x + (x / vec_len * 5), y + (y / vec_len * 5), BLACK);
					}
					display_pixel_draw(dsp, x, y, WHITE);
				}
			}
		return PM_OK;
	}
	*/
    return ABORT;
}

void gps_stop_parser()
{
    ESP_LOGI(TAG, "stop parser");
    if (nmea_hdl) {
        nmea_parser_remove_handler(nmea_hdl, gps_event_handler);
        nmea_parser_deinit(nmea_hdl);
    }
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

    /* initialize GPS module */
    reg->enable(reg);

    /* delay to avoid race condition. this is bad! */
    vTaskDelay(10000 / portTICK_PERIOD_MS);
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
        }
    };
    /* init NMEA parser library */
    nmea_hdl = nmea_parser_init(&config);
    /* register event handler for NMEA parser library */
    nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
    // send initial commands to GPS module
    ESP_ERROR_CHECK(nmea_send_command(nmea_hdl, L96_SEARCH_GPS_GLONASS_GALILEO, sizeof(L96_SEARCH_GPS_GLONASS_GALILEO)));
    ESP_ERROR_CHECK(nmea_send_command(nmea_hdl, L96_ENTER_FULL_ON, sizeof(L96_ENTER_FULL_ON)));

    for (;;) {
        //struct tm timeinfo;
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

        // Update every minute
        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}

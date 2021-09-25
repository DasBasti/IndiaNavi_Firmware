/*
 * GPS Parser Task
 *
 *  Created on: Jan 7, 2021
 *      Author: bastian
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "gui.h"
#include "tasks.h"
#include "pins.h"
#include <esp_log.h>
#include "time.h"
#include <sys/time.h>

#include "nmea_parser.h"

#include <string.h>
#include <math.h>

#include <esp_log.h>
#include <driver/uart.h>
#include "icons_32/icons_32.h"

static const char *TAG = "GPS";
#ifdef DEBUG
float _longitude = 8.68575379, _latitude = 49.7258546, _altitude, _hdop;
bool _fix = GPS_FIX_GPS;
#else
float _longitude, _latitude, _altitude, _hdop;
gps_fix_t _fix = GPS_FIX_INVALID;
#endif
static uint8_t _sats_in_use = 0, _sats_in_view = 0;
char timeString[20];
uint8_t zoom_level_selected = 0;
uint8_t zoom_level[] = {16, 14};
uint8_t zoom_level_scaleBox_width[] = {63, 77};
char *zoom_level_scaleBox_text[] = {"100m", "500m"};
static uint8_t tile_zoom = 16;
static uint16_t x = 0, y = 0, x_old = 0, y_old = 0, pos_x = 0, pos_y = 0;
static uint8_t right_side = 0;
float xf, yf;
nmea_parser_handle_t nmea_hdl;
async_file_t AFILE;
char timezone_file[100];
uint8_t hour;
waypoint_t *waypoints = NULL;
/**
 * @brief GPS Event Handler
 *
 * @param event_handler_arg handler specific arguments
 * @param event_base event base, here is fixed to ESP_NMEA_EVENT
 * @param event_id event id
 * @param event_data event specific arguments
 */
static void
gps_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	const gps_t *_gps;
	switch (event_id)
	{
	case GPS_UPDATE:
		_gps = (gps_t *)event_data;
		_longitude = _gps->longitude;
		_latitude = _gps->latitude;
		_altitude = _gps->altitude;
		_hdop = _gps->dop_h;
		_fix = _gps->fix;
		//ESP_LOGI(TAG, "%d:%d:%d", _gps->tim.hour, _gps->tim.minute, _gps->tim.second);
		ESP_LOGI(TAG, "sats: %d/%d", _gps->sats_in_use, _gps->sats_in_view);
		_sats_in_use = _gps->sats_in_use;
		_sats_in_view = _gps->sats_in_view;
		if (hour != _gps->tim.hour)
		{
			hour = _gps->tim.hour;
			struct tm t = {0};				   // Initalize to all 0's
			t.tm_year = _gps->date.year + 100; // This is year+100, so 121 = 2021
			t.tm_mon = _gps->date.month - 1;
			t.tm_mday = _gps->date.day;
			t.tm_hour = _gps->tim.hour;
			t.tm_min = _gps->tim.minute;
			t.tm_sec = _gps->tim.second;

			struct timeval tv = {mktime(&t), 0}; // epoch time (seconds)
			settimeofday(&tv, NULL);
		}
		break;
	case GPS_UNKNOWN:
		/* print unknown statements */
		ESP_LOGW(TAG, "Unknown statement:%s", (char *)event_data);
		break;
	default:
		break;
	}
}

float flon2tile(float lon, uint8_t zoom)
{
	return ((lon + 180) / 360) * pow(2, zoom);
}

uint32_t lon2tile(float lon, uint8_t zoom)
{
	return floor(flon2tile(lon, zoom));
}

float flat2tile(float lat, uint8_t zoom)
{
	return ((1 - log(tan((lat * M_PI) / 180) + 1 / cos((lat * M_PI) / 180)) / M_PI) / 2) * pow(2, zoom);
}

uint32_t lat2tile(float lat, uint8_t zoom)
{
	return floor(flat2tile(lat, zoom));
}

void update_tiles()
{
	if (xSemaphoreTake(gui_semaphore, portMAX_DELAY) == pdTRUE)
	{
		// get tile number of tile with position on it as float and integer
		if (_longitude != 0.0)
		{
			xf = flon2tile(_longitude, tile_zoom);
			x = floor(xf);
		}
		// also for y axis
		if (_latitude != 0.0)
		{
			yf = flat2tile(_latitude, tile_zoom);
			y = floor(yf);
		}
		// get offset to tile corner of tile with position
		pos_x = floor((xf - x) * 256); // offset from tile 0
		pos_y = floor((yf - y) * 256); // offset from tile 0
		ESP_LOGI(TAG, "GPS: Position update");
		/* grab necessary tiles */
		// TODO: use threshold to trigger new render not leaving tile
		if ((x != x_old) | (y != y_old))
		{
			ESP_LOGI(TAG, "GPS: Tiles need update, too.");
			x_old = x;
			y_old = y;
			for (uint8_t i = 0; i < 2; i++)
			{
				for (uint8_t j = 0; j < 3; j++)
				{
					uint8_t idx = i * 3 + j;
					if (pos_x < 128)
					{
						map_tiles[idx].x = x - 1 + i;
						right_side = 1;
					}
					else
					{
						map_tiles[idx].x = x + i;
						right_side = 0;
					}
					map_tiles[idx].y = y + (-1 + j);
					map_tiles[idx].z = tile_zoom;
					map_tiles[idx].image->loaded = 0;
				}
			}
		}

		pos_y += 256;
		if (right_side)
			pos_x += 256;

		xSemaphoreGive(gui_semaphore);
		trigger_rendering();
	}
	else
	{
		ESP_LOGI(TAG, "gui semaphore taken");
	}
}

void calculate_waypoints(waypoint_t *wp_t)
{
	float _xf, _yf;
	_xf = flon2tile(wp_t->lon, tile_zoom);
	wp_t->tile_x = floor(_xf);
	_yf = flat2tile(wp_t->lat, tile_zoom);
	wp_t->tile_y = floor(_yf);
	wp_t->pos_x = floor((_xf - wp_t->tile_x) * 256); // offset from tile 0
	wp_t->pos_y = floor((_yf - wp_t->tile_y) * 256); // offset from tile 0
													 //ESP_LOGI(TAG, "Calculate waypoint %d @ %d / %d", wp_t->tile_x, wp_t->tile_y, wp_t->num);
}

/* Change Zoom level and trigger rendering. 
   Tiles must be available on SD card! 
*/
void toggleZoom()
{
	zoom_level_selected = !zoom_level_selected;
	tile_zoom = zoom_level[zoom_level_selected];
	ESP_LOGI(TAG, "Set zoom level to: %d", tile_zoom);
	waypoint_t *wp_t = waypoints;
	while (wp_t)
	{
		calculate_waypoints(wp_t);
		wp_t = wp_t->next;
	}
	scaleBox->box.width = zoom_level_scaleBox_width[zoom_level_selected];
	scaleBox->text = zoom_level_scaleBox_text[zoom_level_selected];

	update_tiles();
	trigger_rendering();
}

/**
 * Callbacks from renderer
 */
error_code_t updateInfoText(display_t *dsp, void *comp)
{
	/*sprintf(infoBox->text, "%d/%d/%d Sat:%d", tile_zoom, x, y,
	 gga_frame.satellites_tracked);
	 */
	if (_fix)
	{
		xSemaphoreTake(print_semaphore, portMAX_DELAY);
		sprintf(infoBox->text, "GPS: %fN %fE %.02fm (HDOP:%f)",
				_latitude, _longitude, _altitude, _hdop);
		xSemaphoreGive(print_semaphore);
	}
	return PM_OK;
}

error_code_t updateSatsInView(display_t *dsp, void *comp)
{
	xSemaphoreTake(print_semaphore, portMAX_DELAY);
	sprintf(gps_indicator_label->text, "%d", _sats_in_use);
	xSemaphoreGive(print_semaphore);

	return PM_OK;
}

error_code_t render_position_marker(display_t *dsp, void *comp)
{
	label_t *label = (label_t *)comp;
	uint8_t hdop = floor(_hdop / 2);
	hdop += 8;
	if (_fix & (GPS_FIX_GPS | GPS_FIX_DGPS))
	{
		label->box.left = pos_x - label->box.width / 2;
		label->box.top = pos_y - label->box.height / 2;
		display_circle_fill(dsp, pos_x, pos_y, 6, BLUE);
		display_circle_fill(dsp, pos_x, pos_y, 2, WHITE);
		display_circle_draw(dsp, pos_x, pos_y, hdop, BLACK);
		return PM_OK;
	}
	return ABORT;
}

error_code_t render_waypoint_marker(display_t *dsp, void *comp)
{
	waypoint_t *wp = (waypoint_t *)comp;
	if (_fix & (GPS_FIX_GPS | GPS_FIX_DGPS) & (wp->tile_x != 0) & (wp->tile_y != 0))
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
	return ABORT;
}

void pre_render_cb()
{
	char *waypoint_file = RTOS_Malloc(32768);
	char *wp_line = RTOS_Malloc(50);
	async_file_t *wp_file = &AFILE;
	wp_file->filename = "//TRACK";
	wp_file->dest = waypoint_file;
	wp_file->loaded = false;
	loadFile(wp_file);
	uint8_t delay = 0;
	ESP_LOGI(TAG, "Load waypoint information queued");
	while (!wp_file->loaded)
	{
		vTaskDelay(100 / portTICK_PERIOD_MS);
		if (delay++ == 20)
			break;
	}
	ESP_LOGI(TAG, "Loaded waypoint information. Calculating...");
	uint64_t start = esp_timer_get_time();

	free_render_pipeline(RL_PATH);

	waypoint_t *wp_ = waypoints;
	while (wp_)
	{
		waypoint_t *nwp;
		nwp = wp_;
		wp_ = wp_->next;
		free(nwp);
	}

	waypoint_t *prev_wp = NULL;
	waypoint_t cur_wp = {};
	cur_wp.color = BLUE;
	if (wp_file->loaded)
	{
		ESP_LOGI(TAG, "Load waypoint information ");
		char *f = waypoint_file;
		while (1)
		{
			f = readline(f, wp_line);
			if (!f)
				break;
			float flon = atoff(strtok(wp_line, " "));
			float flat = atoff(strtok(NULL, " "));
			//ESP_LOGI(TAG, "Read waypoint: %f - %f", flon, flat);
			// only load wp that are on currently shown tiles

			cur_wp.lon = flon;
			cur_wp.lat = flat;
			calculate_waypoints(&cur_wp);
			//ESP_LOGI(TAG, "Waypoint %d: %d/%d", cur_wp.num, cur_wp.tile_x, cur_wp.tile_y);
			for (uint8_t i = 0; i < 6; i++)
			{
				if (cur_wp.tile_x == map_tiles[i].x && cur_wp.tile_y == map_tiles[i].y)
				{
					waypoint_t *wp_t = RTOS_Malloc(sizeof(waypoint_t));
					memcpy(wp_t, &cur_wp, sizeof(waypoint_t));
					if (prev_wp)
					{
						prev_wp->next = wp_t;
					}
					else
					{
						waypoints = wp_t;
					}
					prev_wp = wp_t;
					add_to_render_pipeline(render_waypoint_marker, wp_t, RL_PATH);
					// increase for next tile
					cur_wp.num++;
					break;
				}
			}
			if (cur_wp.num % 10 == 0)
			{
				vTaskDelay(1);
			}
		}
		ESP_LOGI(TAG, "Number of waypoints: %d", cur_wp.num);
	}
	else
	{
		ESP_LOGI(TAG, "Load waypoint information failed");
	}
	RTOS_Free(waypoint_file);
	RTOS_Free(wp_line);
	ESP_LOGI(TAG, "Load waypoint information done. Took: %d ms", (uint32_t)(esp_timer_get_time() - start) / 1000);
	ESP_LOGI(TAG, "Heap Free: %d Byte", xPortGetFreeHeapSize());
}

void gps_stop_parser()
{
	ESP_LOGI(TAG, "stop parser");
	if (nmea_hdl)
	{
		nmea_parser_remove_handler(nmea_hdl, gps_event_handler);
		nmea_parser_deinit(nmea_hdl);
	}
}

void StartGpsTask(void const *argument)
{
	uint8_t minute = 0;

	ESP_LOGI(TAG, "init gpio %d\n\r", GPS_VCC_nEN);
	/* create power regulator */
	regulator_t *reg;
	gpio_t *reg_gpio = gpio_create(OUTPUT, 0, GPS_VCC_nEN);
	reg_gpio->onValue = GPIO_RESET;

	ESP_LOGI(TAG, "init regulator\n\r");
	reg = regulator_gpio_create(reg_gpio);

	/* register echo command 

	echo_cmd.command = "gps";
	echo_cmd.function_cb = echo_cmd_cb;
	ESP_LOGI(TAG, "GPS: init command %s\n\r", echo_cmd.command);
	command_register(&echo_cmd);
*/
	/* initialize GPS module */
	reg->enable(reg);

	ESP_LOGI(TAG, "wait for infoBox init");
	/* wait for infoBox to be created, assign modifier callback */
	while (!infoBox)
		vTaskDelay(100 / portTICK_PERIOD_MS);
	infoBox->onBeforeRender = updateInfoText;

	while (!gps_indicator_label)
		vTaskDelay(100 / portTICK_PERIOD_MS);
	gps_indicator_label->onBeforeRender = updateSatsInView;

	ESP_LOGI(TAG, "wait for GUI init");
	/* wait for map tiles to be created */
	wait_until_gui_ready();

	/* delay to avoid race condition. this is bad! */
	vTaskDelay(10000 / portTICK_PERIOD_MS);
	ESP_LOGI(TAG, "Load timezone information");
	async_file_t *tz_file = &AFILE;
	tz_file->filename = "//TIMEZONE";
	tz_file->dest = timezone_file;
	tz_file->loaded = false;
	loadFile(tz_file);
	uint8_t delay = 0;
	ESP_LOGI(TAG, "Load timezone information queued");
	while (!tz_file->loaded)
	{
		vTaskDelay(100 / portTICK_PERIOD_MS);
		if (delay++ == 20)
			break;
	}
	ESP_LOGI(TAG, "Load timezone information loaded");
	if (tz_file->loaded)
	{
		char tz[50] = {0};
		readline(timezone_file, tz);
		setenv("TZ", tz, 1);
		ESP_LOGI(TAG, "Set timezone to: %s", tz);
	}
	else
	{
		setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
		ESP_LOGI(TAG, "Use default timezone");
	}
	tzset();

	positon_marker->onBeforeRender = render_position_marker;

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
			.event_queue_size = 16}};
	/* init NMEA parser library */
	nmea_hdl = nmea_parser_init(&config);
	/* register event handler for NMEA parser library */
#ifndef DEBUG
	// start the parser on release builds
	nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
#else
	uint8_t t = 0; // toggle
#endif
	for (;;)
	{

		/* get Time from GPS
		 check if gps indicator from gui.h is available (already allocated)
		 */
		if (gps_indicator_label)
		{
			image_t *icon = gps_indicator_label->child;
			if (_fix & (GPS_FIX_GPS | GPS_FIX_DGPS))
				icon->data = GPS_lock;
			else
				icon->data = GPS;
		}
		xSemaphoreTake(print_semaphore, portMAX_DELAY);
		xSemaphoreGive(print_semaphore);
		// Update every minute
		//struct tm timeinfo;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		struct tm *timeinfo = localtime(&tv.tv_sec);
		if (clock_label && minute != timeinfo->tm_min)
		{
			trigger_rendering();
		}

		ESP_LOGI(TAG, "%fN %fE %fm: %d", _latitude, _longitude, _altitude, _fix);
		/* check for lon and lat to be a number */
		if (_fix)
		{
			update_tiles();
		}
#ifdef DEBUG
		vTaskDelay(60000 / portTICK_PERIOD_MS);
		if (t)
		{
			_longitude = 8.055898;
			_latitude = 49.427578;
		}
		else
		{
			_longitude = 8.043859;
			_latitude = 49.394235;
		}
		t = !t;
#endif
	}
}

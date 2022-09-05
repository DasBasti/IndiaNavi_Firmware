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
#include "l96.h"

#include <string.h>
#include <math.h>

#include <esp_log.h>
#include <driver/uart.h>
#include "icons_32/icons_32.h"

static const char *TAG = "GPS";
#ifdef nDEBUG
map_position_t current_position = {
	.longitude = 8.68575379,
	.latitude = 49.7258546,
	.fix = GPS_FIX_GPS
	};
#else
map_position_t current_position = {};
#endif
static uint8_t _sats_in_use = 0, _sats_in_view = 0;
char timeString[20];
uint8_t zoom_level_selected = 0;
uint8_t zoom_level[] = {16, 14};
uint8_t zoom_level_scaleBox_width[] = {63, 77};
char *zoom_level_scaleBox_text[] = {"100m", "500m"};
static uint8_t tile_zoom = 16;

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
		current_position.longitude = _gps->longitude;
		current_position.latitude = _gps->latitude;
		current_position.altitude = _gps->altitude;
		current_position.hdop = _gps->dop_h;
		current_position.fix= _gps->fix;
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

void update_tiles()
{
	if (xSemaphoreTake(gui_semaphore, portMAX_DELAY) == pdTRUE)
	{
		// TODO: -> event?
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
	//TODO: into map component
	/*float _xf, _yf;
	_xf = flon2tile(wp_t->lon, tile_zoom);
	wp_t->tile_x = floor(_xf);
	_yf = flat2tile(wp_t->lat, tile_zoom);
	wp_t->tile_y = floor(_yf);
	wp_t->pos_x = floor((_xf - wp_t->tile_x) * 256); // offset from tile 0
	wp_t->pos_y = floor((_yf - wp_t->tile_y) * 256); // offset from tile 0
	*/												 //ESP_LOGI(TAG, "Calculate waypoint %d @ %d / %d", wp_t->tile_x, wp_t->tile_y, wp_t->num);
}

/* Change Zoom level and trigger rendering. 
   Tiles must be available on SD card! 
*/
void toggleZoom()
{
	zoom_level_selected = !zoom_level_selected;
	map_update_zoom_level(map, zoom_level[zoom_level_selected]);

	ESP_LOGI(TAG, "Set zoom level to: %d", tile_zoom);
	// TODO: Update Waypoints
	scaleBox->box.width = zoom_level_scaleBox_width[zoom_level_selected];
	scaleBox->text = zoom_level_scaleBox_text[zoom_level_selected];

	map_update_position(map, current_position);
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
	if (current_position.fix!= GPS_FIX_INVALID)
	{
		xSemaphoreTake(print_semaphore, portMAX_DELAY);
		sprintf(infoBox->text, "GPS: %fN %fE %.02fm (HDOP:%f)",
				current_position.longitude, current_position.longitude, current_position.altitude, current_position.hdop);
		xSemaphoreGive(print_semaphore);
	}
	else
	{
		xSemaphoreTake(print_semaphore, portMAX_DELAY);
		sprintf(infoBox->text, "No GPS Signal found!");
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
	// TODO: move to map component
	/*
	label_t *label = (label_t *)comp;
	uint8_t hdop = floor(_hdop / 2);
	hdop += 8;
	if (current_position.fix!= GPS_FIX_INVALID)
	{
		label->box.left = pos_x - label->box.width / 2;
		label->box.top = pos_y - label->box.height / 2;
		display_circle_fill(dsp, pos_x, pos_y, 6, BLUE);
		display_circle_fill(dsp, pos_x, pos_y, 2, WHITE);
		display_circle_draw(dsp, pos_x, pos_y, hdop, BLACK);
		return PM_OK;
	}
	*/
	return ABORT;
}

error_code_t render_waypoint_marker(display_t *dsp, void *comp)
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

/*
 * Load tile data from SD Card on render command
 *
 * We share a global memory buffer since it is not enough memory available
 * to hold all 6 tiles in memory at once.
 * 
 * returns PK_OK if loaded, UNAVAILABLE if buffer or sd semaphore is not available and
 * TIMEOUT if loading timed out
 */
error_code_t load_map_tile_on_demand(const display_t *dsp, void *image)
{
	// TODO: move to GUI
	uint8_t timeout = 0;
	uint8_t *imageBuf = RTOS_Malloc(256 * 256 / 2);
	ESP_LOGI(TAG, "Image at: 0x%X", (uint)imageBuf);
	if (!imageBuf)
		return UNAVAILABLE;

	image_t *img = (image_t *)image;
	if (!uxSemaphoreGetCount(sd_semaphore))
	{ // binary semaphore returns 1 on not taken
		RTOS_Free(imageBuf);
		return UNAVAILABLE;
	}

	img->data = imageBuf;
	loadTile(img->parent); // queue loading

	while (img->loaded != LOADED)
	{
		label_t *l = (label_t *)img->child;
		if (img->loaded == ERROR)
		{
			l->text = "Error";
			goto freeImageMemory;
		}
		if (img->loaded == NOT_FOUND)
		{
			//l->text = "Not Found";
			goto freeImageMemory;
		}
		vTaskDelay(10);
		timeout++;
		if (timeout == 100)
		{
			ESP_LOGI(TAG, "load timeout!");
			goto freeImageMemory;
		}
	}
	return PM_OK;

freeImageMemory:
	RTOS_Free(imageBuf);
	return TIMEOUT;
}

error_code_t check_if_map_tile_is_loaded(const display_t *dsp, void *image)
{
	// TODO: move to map component
	image_t *img = (image_t *)image;
	label_t *lbl = img->child;
	if (img->loaded != LOADED)
	{
		label_render(dsp, lbl);
	}
	else
	{
		RTOS_Free(img->data);
	}
	return PM_OK;
}

void pre_render_cb()
{
	// Only render wb if we are fixed
	if (current_position.fix== GPS_FIX_INVALID)
		return;

	map_update_zoom_level(map, 16);
	map_update_position(map, current_position);	

		//TODO: move to map component
	/*char *waypoint_file = RTOS_Malloc(32768);
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
		bool header = true;
		while (1)
		{
			f = readline(f, wp_line);
			if (!f)
				break;
			if (wp_line[0] == '-' && wp_line[1] == '-')
			{
				header = false;
				continue;
			};
			if (header)
			{
				continue;
			}

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
	*/
}

/**
 * Generate the GPS screen element for rendering the map tiles in the background

void gps_screen_element(const display_t *dsp)
{
	for (uint8_t i = 0; i < 2; i++)
		for (uint8_t j = 0; j < 3; j++)
		{
			uint8_t idx = i * 3 + j;
			map_tiles[idx].image = image_create(NULL, i * 255, j * 255, 256,
												256);
			map_tiles[idx].label = label_create(RTOS_Malloc(21), &f8x8, i * 255,
												j * 255, 256, 256);
			save_sprintf(map_tiles[idx].label->text, "loading...");

			map_tiles[idx].label->alignHorizontal = CENTER;
			map_tiles[idx].label->alignVertical = MIDDLE;
			map_tiles[idx].label->backgroundColor = WHITE;
			map_tiles[idx].image->onBeforeRender = load_map_tile_on_demand;
			map_tiles[idx].image->onAfterRender = check_if_map_tile_is_loaded;
			map_tiles[idx].image->child = map_tiles[idx].label;
			map_tiles[idx].image->loaded = 0;
			map_tiles[idx].image->parent = &map_tiles[idx];
			map_tiles[idx].x = i;
			map_tiles[idx].y = j;
			add_to_render_pipeline(image_render, map_tiles[idx].image, RL_MAP);
		}
}
 */
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

	map_update_zoom_level(map, 0);
	map_update_position(map, current_position);
	map_tile_attach_onBeforeRender_callback(map, load_map_tile_on_demand);
	map_tile_attach_onAfterRender_callback(map, check_if_map_tile_is_loaded);

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
	while (1)
		vTaskDelay(100);

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
	// send initial commands to GPS module
	ESP_ERROR_CHECK(nmea_send_command(nmea_hdl, L96_SEARCH_GPS_GLONASS_GALILEO, sizeof(L96_SEARCH_GPS_GLONASS_GALILEO)));
	ESP_ERROR_CHECK(nmea_send_command(nmea_hdl, L96_ENTER_FULL_ON, sizeof(L96_ENTER_FULL_ON)));

	for (;;)
	{

		/* get Time from GPS
		 check if gps indicator from gui.h is available (already allocated)
		 */
		if (gps_indicator_label)
		{
			image_t *icon = gps_indicator_label->child;
			if (current_position.fix!= GPS_FIX_INVALID)
				icon->data = GPS_lock;
			else
				icon->data = GPS;
		}
		// Update every minute
		//struct tm timeinfo;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		struct tm *timeinfo = localtime(&tv.tv_sec);
		if (clock_label && minute != timeinfo->tm_min)
		{
			trigger_rendering();
		}

		/* check for lon and lat to be a number */
		if ((current_position.fix!= GPS_FIX_INVALID))
		{
			ESP_LOGI(TAG, "Fix: %d", current_position.fix);
			update_tiles();
		}

		vTaskDelay(60000 / portTICK_PERIOD_MS);
#ifdef DEBUG
		if (t)
		{
			current_position.longitude = 8.055898;
			current_position.latitude = 49.427578;
		}
		else
		{
			current_position.longitude = 8.043859;
			current_position.latitude = 49.394235;
		}
		t = !t;
#endif
	}
}

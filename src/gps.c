/*
 * GPS Parser Task
 *
 *  Created on: Jan 7, 2021
 *      Author: bastian
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>

#include "freertos/task.h"
#include "nmea_parser.h"

#include <string.h>
#include <math.h>
#include <Platinenmacher.h>

#include "icons_32/icons_32.h"
#include "tasks.h"
#include "gui.h"
#include "pins.h"

#include <esp_log.h>
#include <driver/uart.h>

static const char *TAG = "GPS";

#define TIME_ZONE (+2)	 //Berlin + Sommerzeit
#define YEAR_BASE (2000) //date in GPS starts from 2000
float _longitude, _latitude, _altitude, _hdop;
bool _fix;
int _minute, _hour;
int8_t timezone;
char timeString[20];
const uint8_t tile_zoom = 16;
uint16_t x = 0, y = 0, x_old = 0, y_old = 0, pos_x = 0, pos_y = 0;
float xf, yf;
nmea_parser_handle_t nmea_hdl;
async_file_t AFILE;
char timezone_file[100];

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
		_minute = _gps->tim.minute;
		_hour = _gps->tim.hour + TIME_ZONE;
		/* print information parsed from GPS statements */
		/*ESP_LOGI(TAG, "%d/%d/%d %d:%d:%d => \r\n"
					  "\t\t\t\t\t\tlatitude   = %.05f°N\r\n"
					  "\t\t\t\t\t\tlongitude = %.05f°E\r\n"
					  "\t\t\t\t\t\taltitude   = %.02fm\r\n"
					  "\t\t\t\t\t\tspeed      = %fm/s",
				 gps_info->date.year + YEAR_BASE, gps_info->date.month, gps_info->date.day,
				 gps_info->tim.hour + TIME_ZONE, gps_info->tim.minute, gps_info->tim.second,
				 gps_info->latitude, gps_info->longitude, gps_info->altitude, gps_info->speed);
		*/
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

#if 0
/**
 * Console commands
 */
void echo_cmd_cb(command_t *cmd)
{
	if (strcmp(cmd->args, "gga") == 0)
	{
		bit_toggle(echo, MINMEA_SENTENCE_GGA);
	}
	else if (strcmp(cmd->args, "txt") == 0)
	{
		bit_toggle(echo, MINMEA_SENTENCE_TXT);
	}
	else if (strcmp(cmd->args, "raw") == 0)
	{
		bit_toggle(echo, (MINMEA_SENTENCE_ZDA + 1));
	}
	else if (strcmp(cmd->args, "pos") == 0)
	{
		ESP_LOGI(TAG, "GPS: Set position");
		bit_toggle(echo, (MINMEA_SENTENCE_ZDA + 2));
		if (echo & (1 << (MINMEA_SENTENCE_ZDA + 2)))
			parseSentence(
				"$GNGGA,113519.000,4938.78241,N,00834.39293,E,1,05,1.4,99.4,M,48.2,M,,*75");
	}
}
#endif
/**
 * Callbacks from renderer
 */
error_code_t updateInfoText(display_t *dsp, void *comp)
{
	/*sprintf(infoBox->text, "%d/%d/%d Sat:%d", tile_zoom, x, y,
	 gga_frame.satellites_tracked);
	 */
	xSemaphoreTake(print_semaphore, portMAX_DELAY);
	sprintf(infoBox->text, "GPS: %fN %fE %.02fm",
			_latitude, _longitude, _altitude);
	xSemaphoreGive(print_semaphore);
	return PM_OK;
}

error_code_t render_position_marker(display_t *dsp, void *comp)
{
	label_t *label = (label_t *)comp;
	uint8_t hdop = floor(_hdop / 5);
	if (hdop < 8)
		hdop = 8;
	if (_fix & (GPS_FIX_GPS | GPS_FIX_DGPS))
	{
		label->box.left = pos_x - label->box.width / 2;
		label->box.top = pos_y - label->box.height / 2;
		display_circle_fill(dsp, pos_x, pos_y, 6, BLUE);
		display_circle_draw(dsp, pos_x, pos_y, hdop, BLUE);
		return PM_OK;
	}
	return ABORT;
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
		char tz[4];
		readline(timezone_file, tz);
		timezone = atoi(tz);
		ESP_LOGI(TAG, "Set timezone to: %d", timezone);
	}
	else
	{
		timezone = TIME_ZONE;
		ESP_LOGI(TAG, "Use default timezone: %d", timezone);
	}

	positon_marker->onBeforeRender = render_position_marker;

	ESP_LOGI(TAG, "UART config");
	/* NMEA parser configuration */
	nmea_parser_config_t config = {
		.uart = {
			.uart_port = UART_NUM_2,
			.baud_rate = 9600,
			.data_bits = UART_DATA_8_BITS,
			.parity = UART_PARITY_DISABLE,
			.stop_bits = UART_STOP_BITS_1,
			.event_queue_size = 16}};
	/* init NMEA parser library */
	nmea_hdl = nmea_parser_init(&config);
	/* register event handler for NMEA parser library */
	nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);

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
		// Run every minute
		if (clock_label && minute != _minute)
		{
			xSemaphoreTake(print_semaphore, portMAX_DELAY);
			sprintf(timeString, "%02d:%02d", _hour,
					_minute);
			xSemaphoreGive(print_semaphore);
			clock_label->text = timeString;
			minute = _minute;
			//trigger_rendering();
		}

		ESP_LOGI(TAG, "%fN %fE %fm: %d", _latitude, _longitude, _altitude, _fix);
		/* check for lon and lat to be a number */
		if (_fix)
		{
			if (xSemaphoreTake(gui_semaphore, portMAX_DELAY) == pdTRUE)
			{
				if (_longitude != 0.0)
				{
					xf = flon2tile(_longitude, tile_zoom);
					x = floor(xf);
				}
				if (_latitude != 0.0)
				{
					yf = flat2tile(_latitude, tile_zoom);
					y = floor(yf);
				}
				pos_x = floor((xf - x) * 256); // offset from tile 0
				pos_y = floor((yf - y) * 256); // offset from tile 0
				ESP_LOGI(TAG, "GPS: Position update");
				/* grab necessary tiles */
				uint8_t right_side = 0;
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

		vTaskDelay(60000 / portTICK_PERIOD_MS);
	}
}

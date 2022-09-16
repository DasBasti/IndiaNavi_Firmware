/*
 * acep_5in65_7c.c
 *
 *  Created on: Jan 2, 2021
 *      Author: bastian
 */
#include <string.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <freertos/task.h>
#include "acep_5in65_7c.h"
#include <esp_log.h>
const char *TAG = "eink";
// The framebuffer for the display
#define FB_SIZE (ACEP_5IN65_WIDTH * ACEP_5IN65_HEIGHT / 2)
uint8_t fb[FB_SIZE] = {0};
// SPI handle
spi_device_handle_t spi;

static error_code_t ACEP_5IN65_Display(uint8_t *image);

/*
 * write pixel in framebuffer
 */
error_code_t ACEP_5IN65_Write(const display_t *dsp, uint16_t x, uint16_t y,
							  uint8_t color)
{
	uint32_t position;
	switch (dsp->rotation)
	{
	case DISPLAY_ROTATE_270:												 // switch x and y and invert
		position = (((ACEP_5IN65_HEIGHT - (x + 1)) * ACEP_5IN65_WIDTH) + y); // -2 to start gui at 0/0
		break;
	case DISPLAY_ROTATE_90:												// switch x and y
		position = ((x * ACEP_5IN65_WIDTH) + ACEP_5IN65_WIDTH - y - 1); // and mirror both axis
		break;
	default: // default is rotate 0 and no change
		position = ((y * ACEP_5IN65_WIDTH) + x);
	}
	if (position < FB_SIZE * 2)
	{
		// position in fb is calculated by even and odd number
		if (position & 0x1)
		{
			fb[position >> 1] = ((fb[position >> 1] & 0xf0) + (color & 0x0f));
		}
		else
		{
			fb[position >> 1] = ((fb[position >> 1] & 0x0f) + ((color & 0x0f) << 4));
		}
		return PM_OK;
	}
	return OUT_OF_BOUNDS;
}

/*
 * Send framebuffer to display
 */
void ACEP_5IN65_Commit_Fb()
{
	if (ACEP_5IN65_Display(fb) == TIMEOUT)
		ESP_LOGE(TAG, "Timeout during commiting FB");
}

/**
 * Return color for x/y pixel
 */
uint8_t ACEP_5IN65_Decompress_Pixel(rect_t *size, uint16_t x, uint16_t y,
									const uint8_t *data)
{
	uint32_t pos = (y * size->width) + x;

	if (pos & 0x1)
	{
		return (data[pos >> 1] & 0x7);
	}
	return ((data[pos >> 1] >> 4) & 0x7);
}

/******************************************************************************
 function :	Toggle reset pin
 parameter:
 ******************************************************************************/
static void ACEP_5IN65_Reset(void)
{
	return;
}

/******************************************************************************
 function :	send command
 parameter:
 Reg : Command register
 ******************************************************************************/
static void ACEP_5IN65_SendCommand(uint8_t Reg)
{
	esp_err_t ret;
	spi_transaction_t t;
	memset(&t, 0, sizeof(t));					//Zero out the transaction
	t.length = 8;								//Transaction length is in bits.
	t.tx_buffer = &Reg;							//Data
	t.user = (void *)0;							//D/C needs to be set to 0
	ret = spi_device_polling_transmit(spi, &t); //Transmit!
	assert(ret == ESP_OK);						//Should have had no issues.
}

/******************************************************************************
 function :	send data
 parameter:
 Data : Write data
 ******************************************************************************/
static void ACEP_5IN65_SendData(uint8_t Data)
{
	esp_err_t ret;
	spi_transaction_t t;
	memset(&t, 0, sizeof(t));					//Zero out the transaction
	t.length = 8;								//Transaction length is in bits.
	t.tx_buffer = &Data;						//Data
	t.user = (void *)1;							//D/C needs to be set to 1
	ret = spi_device_polling_transmit(spi, &t); //Transmit!
	assert(ret == ESP_OK);						//Should have had no issues.
}
#if 0
static void ACEP_5IN65_SendDataStream(uint8_t *Data, uint32_t length) {
	gpio_set_level(EINK_DC, 1);
	gpio_set_level(EINK_CS, 0);
	spi_device_polling_transmit(spi, Data, length, 10);
	gpio_set_level(EINK_CS, 1);
}
#endif

static error_code_t ACEP_5IN65_BusyHigh(void) // If BUSYN=0 then waiting
{
	uint8_t timeout = 0;
	while (!(gpio_get_level(EINK_BUSY)))
	{
		vTaskDelay(100);
		if (timeout++ == 60)
		{
			return TIMEOUT;
		}
	}
	return PM_OK;
}

static error_code_t ACEP_5IN65_BusyLow(void) // If BUSYN=1 then waiting
{
	uint8_t timeout = 0;
	while (gpio_get_level(EINK_BUSY))
	{
		vTaskDelay(100);
		if (timeout++ == 60)
		{
			return TIMEOUT;
		}
	}
	return PM_OK;
}

//This function is called (in irq context!) just before a transmission starts. It will
//set the D/C line to the value indicated in the user field.
void ACEP_5IN65_pre_transfer_callback(spi_transaction_t *t)
{
	int dc = (int)t->user;
	gpio_set_level(EINK_DC, dc);
}

/*
 * Initialize display and the e-Paper registers
 */
display_t *ACEP_5IN65_Init(display_rotation_t rotation)
{
	esp_err_t ret;
	spi_bus_config_t buscfg = {
		.miso_io_num = -1,
		.mosi_io_num = 17,
		.sclk_io_num = 18,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 8};
	spi_device_interface_config_t devcfg = {
		.clock_speed_hz = 1 * 1000 * 1000,			//Clock out at 1 MHz
		.mode = 0,									//SPI mode 0
		.spics_io_num = 5,							//CS pin
		.queue_size = 1,							//We want to be able to queue 7 transactions at a time
		.pre_cb = ACEP_5IN65_pre_transfer_callback, //Specify pre-transfer callback to handle D/C line
	};
	//Initialize the SPI bus
	ret = spi_bus_initialize(EINK_SPI_HOST, &buscfg, 0);
	if (ret != ESP_OK)
		goto spi_bus_initialize_failed;
	//Attach the LCD to the SPI bus
	ret = spi_bus_add_device(EINK_SPI_HOST, &devcfg, &spi);
	if (ret != ESP_OK)
		goto spi_bus_add_device_failed;

	/* create display object */
	display_t *disp;
	switch (rotation)
	{
	case DISPLAY_ROTATE_90:
	case DISPLAY_ROTATE_270:
		disp = display_init(ACEP_5IN65_HEIGHT, ACEP_5IN65_WIDTH, 4, rotation);
		break;
	default:
		disp = display_init(ACEP_5IN65_WIDTH, ACEP_5IN65_HEIGHT, 4, rotation);
		break;
	}
	/* assign driver functions */
	disp->update = ACEP_5IN65_Commit_Fb;
	disp->write_pixel = ACEP_5IN65_Write;
	disp->decompress = ACEP_5IN65_Decompress_Pixel;

	gpio_set_direction(EINK_DC, GPIO_MODE_OUTPUT);
	gpio_set_pull_mode(EINK_DC, GPIO_PULLUP_ENABLE);
	gpio_set_direction(EINK_CS, GPIO_MODE_OUTPUT);
	gpio_set_direction(EINK_BUSY, GPIO_MODE_INPUT);
	gpio_set_pull_mode(EINK_BUSY, GPIO_PULLUP_ENABLE);
	gpio_set_level(EINK_DC, 0);
	gpio_set_level(EINK_CS, 0);

	ret = spi_device_acquire_bus(spi, portMAX_DELAY);
	ESP_ERROR_CHECK(ret);
	ACEP_5IN65_Reset();
	if (ACEP_5IN65_BusyHigh() == TIMEOUT)
		goto display_busyhigh_timeout;
	ACEP_5IN65_SendCommand(0x00);
	ACEP_5IN65_SendData(0xEF);
	ACEP_5IN65_SendData(0x08);
	ACEP_5IN65_SendCommand(0x01);
	ACEP_5IN65_SendData(0x37);
	ACEP_5IN65_SendData(0x00);
	ACEP_5IN65_SendData(0x23);
	ACEP_5IN65_SendData(0x23);
	ACEP_5IN65_SendCommand(0x03);
	ACEP_5IN65_SendData(0x00);
	ACEP_5IN65_SendCommand(0x06);
	ACEP_5IN65_SendData(0xC7);
	ACEP_5IN65_SendData(0xC7);
	ACEP_5IN65_SendData(0x1D);
	ACEP_5IN65_SendCommand(0x30);
	ACEP_5IN65_SendData(0x3C);
	ACEP_5IN65_SendCommand(0x41);
	ACEP_5IN65_SendData(0x80);
	ACEP_5IN65_SendCommand(0x50);
	ACEP_5IN65_SendData(0x3f);
	ACEP_5IN65_SendCommand(0x60);
	ACEP_5IN65_SendData(0x22);
	ACEP_5IN65_SendCommand(0x61);
	ACEP_5IN65_SendData(0x02);
	ACEP_5IN65_SendData(0x58);
	ACEP_5IN65_SendData(0x01);
	ACEP_5IN65_SendData(0xC0);
	ACEP_5IN65_SendCommand(0xE3);
	ACEP_5IN65_SendData(0xAA);
	ACEP_5IN65_SendCommand(0x82);
	ACEP_5IN65_SendData(0x80);

	vTaskDelay(10);
	ACEP_5IN65_SendCommand(0x50);
	ACEP_5IN65_SendData(0x37);
	spi_device_release_bus(spi);

	return disp;

display_busyhigh_timeout:
	spi_device_release_bus(spi);
	RTOS_Free(disp);
	spi_bus_remove_device(spi);
spi_bus_add_device_failed:
	spi_bus_free(EINK_SPI_HOST);
spi_bus_initialize_failed:
	return NULL;
}

/*
 * Send the image buffer in RAM to e-Paper and displays
 */
static error_code_t ACEP_5IN65_Display(uint8_t *image)
{
	spi_device_acquire_bus(spi, portMAX_DELAY);
	ACEP_5IN65_SendCommand(0x61); //Set Resolution setting
	ACEP_5IN65_SendData(0x02);
	ACEP_5IN65_SendData(0x58);
	ACEP_5IN65_SendData(0x01);
	ACEP_5IN65_SendData(0xC0);
	ACEP_5IN65_SendCommand(0x10);
	for (uint16_t i = 0; i < ACEP_5IN65_HEIGHT; i++)
	{
		for (uint16_t j = 0; j < ACEP_5IN65_WIDTH / 2; j++)
		{
			ACEP_5IN65_SendData(image[j + ((ACEP_5IN65_WIDTH / 2) * i)]);
		}
	}
	ACEP_5IN65_SendCommand(0x04); //0x04
	if (ACEP_5IN65_BusyHigh() == TIMEOUT)
		return TIMEOUT;
	ACEP_5IN65_SendCommand(0x12); //0x12
	if (ACEP_5IN65_BusyHigh() == TIMEOUT)
		return TIMEOUT;
	ACEP_5IN65_SendCommand(0x02); //0x02
	spi_device_release_bus(spi);
	if (ACEP_5IN65_BusyLow() == TIMEOUT)
		return TIMEOUT;
	return PM_OK;
}

/******************************************************************************
 function :	Sends the part image buffer in RAM to e-Paper and displays
 parameter:
 ******************************************************************************/
void ACEP_5IN65_Display_part(uint8_t *image, uint16_t xstart, uint16_t ystart,
							 uint16_t image_width, uint16_t image_heigh)
{
	unsigned long i, j;
	spi_device_acquire_bus(spi, portMAX_DELAY);
	ACEP_5IN65_SendCommand(0x61); //Set Resolution setting
	ACEP_5IN65_SendData(0x02);
	ACEP_5IN65_SendData(0x58);
	ACEP_5IN65_SendData(0x01);
	ACEP_5IN65_SendData(0xC0);
	ACEP_5IN65_SendCommand(0x10);
	for (i = 0; i < ACEP_5IN65_HEIGHT; i++)
	{
		for (j = 0; j < ACEP_5IN65_WIDTH / 2; j++)
		{
			if (i < image_heigh + ystart && i >= ystart && j < (image_width + xstart) / 2 && j >= xstart / 2)
			{
				ACEP_5IN65_SendData(
					image[(j - xstart / 2) + (image_width / 2 * (i - ystart))]);
			}
			else
			{
				ACEP_5IN65_SendData(0x11);
			}
		}
	}
	ACEP_5IN65_SendCommand(0x04); //0x04
	ACEP_5IN65_BusyHigh();
	ACEP_5IN65_SendCommand(0x12); //0x12
	ACEP_5IN65_BusyHigh();
	ACEP_5IN65_SendCommand(0x02); //0x02
	spi_device_release_bus(spi);
	ACEP_5IN65_BusyLow();
}

/******************************************************************************
 function :	Enter sleep mode
 parameter:
 ******************************************************************************/
void ACEP_5IN65_Sleep(void)
{
	ACEP_5IN65_SendCommand(0x07);
	ACEP_5IN65_SendData(0xA5);
}

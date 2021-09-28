/* Pin Names */
#ifndef __PINS_H__
#define __PINS_H__

#include <driver/gpio.h>

#define UART_TX 1
#define UART_RX 3

#define GPS_VCC_nEN 32
#define GPS_UART2_TX 23
#define GPS_UART2_RX 19

#define EINK_SPI_CLK 18
#define EINK_SPI_MOSI 17
#define EINK_SPI_MISO -1
#define EINK_SPI_nCS 5
#define EINK_DC 25
#define EINK_VCC_nEN 26
#define EINK_BUSSY 39

#define VBAT_ADC 34
#define VIN_ADC 35

#define SD_VCC_nEN 16
#define SD_SPI_D0 2
#define SD_SPI_D1 4
#define SD_SPI_D2 12
#define SD_SPI_D3 13
#define SD_SPI_CLK 14
#define SD_SPI_nCS 15
#define SD_CARD_nDET 36

#define BTN 27
#define BTN_LEVEL 0
#define LED 33

#define I2C_MASTER_NUM 0
#define I2C_SDA 0
#define I2C_SCL 22
#define I2C_INT 21
#define I2C_INT_LEVEL 0

#endif
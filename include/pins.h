/* Pin Names */
#ifndef __PINS_H__
#define __PINS_H__

#include <driver/gpio.h>

#ifdef ESP_S3

#define UART_TX 43
#define UART_RX 44

#define GPS_VCC_nEN 15
#define GPS_UART2_TX 1
#define GPS_UART2_RX 40

#define EINK_SPI_CLK 39
#define EINK_SPI_MOSI 13
#define EINK_SPI_MISO -1
#define EINK_SPI_nCS 38
#define EINK_DC 17
#define EINK_VCC_nEN 18
#define EINK_BUSY 5

#define VBAT_ADC 5
#define VIN_ADC 6

#define SD_VCC_nEN 14
#define SD_SPI_D0 48
#define SD_SPI_D1 21
#define SD_SPI_D2 20
#define SD_SPI_D3 9
#define SD_SPI_CLK 19
#define SD_SPI_nCS 47
#define SD_CARD_nDET 4

#define BTN 8
#define BTN_LEVEL 0
#define LED 16

#define I2C_MASTER_NUM 0
#define I2C_SDA 0
#define I2C_SCL 2
#define I2C_INT 42
#define I2C_INT_LEVEL 0

#else

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
#define EINK_BUSY 39

#define VBAT_ADC 6
#define VIN_ADC 7

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

#endif // ESP_S3

#endif
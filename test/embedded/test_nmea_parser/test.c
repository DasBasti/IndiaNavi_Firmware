#include <unity.h>

#include <driver/uart.h>
#include <nmea_parser.h>
#include <pins.h>

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
nmea_parser_handle_t nmea_hdl;
gps_t gps;
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
memcpy(&gps, event_data, sizeof(gps_t));
}

void setUp()
{
    nmea_hdl = nmea_parser_init(&config);
    nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
}

void test_nmea_parser_init(){
    TEST_ASSERT_NOT_NULL(nmea_hdl);
}

int app_main()
{
    UNITY_BEGIN();
    RUN_TEST(test_nmea_parser_init);
    UNITY_END();
}
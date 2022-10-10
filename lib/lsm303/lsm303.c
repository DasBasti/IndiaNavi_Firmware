#include <esp_system.h>
#include <esp_log.h>
#include <driver/i2c.h>

#include <lsm303.h>

#define LSM303_ACC_ADDR  0x1D
#define LSM303_MAG_ADDR  0x1E

/**
 * @brief All registers present in LSM303AH
 */
enum lsm303_register {
    // Accelerometer
    MODULE_8BIT_A = 0x0C,
    WHO_AM_I_A = 0x0F,
    CTRL1_A = 0x20,
    CTRL2_A,
    CTRL3_A,
    CTRL4_A,
    CTRL5_A,
    FIFO_CTRL_A,
    OUT_T_A,
    STATUS_A,
    OUT_X_L_A,
    OUT_X_H_A,
    OUT_Y_L_A,
    OUT_Y_H_A,
    OUT_Z_L_A,
    OUT_Z_H_A,
    FIFO_THS_A,
    FIFO_SRC_A,
    FIFO_SAMPLES_A,
    TAP_6D_THS_A,
    INT_DUR_A,
    WAKE_UP_THS_A,
    WAKE_UP_DUR_A,
    FREE_FALL_A,
    STATUS_DUP_A,
    WAKE_UP_SRC_A,
    TAP_SRC_A,
    SIXD_SRC_A,
    STEP_COUNTER_MINTHS_A,
    STEP_COUNTER_L_A,
    STEP_COUNTER_H_A,
    FUNC_CK_GATE_A,
    FUNC_SRC_A,
    FUNC_CTRL_A,
    // Magenetometer
    OFFSET_X_REG_L_M = 0x45,
    OFFSET_X_REG_H_M,
    OFFSET_Y_REG_L_M,
    OFFSET_Y_REG_H_M,
    OFFSET_Z_REG_L_M,
    OFFSET_Z_REG_H_M,
    WHO_AM_I_M = 0x4F,
    CFG_REG_A_M = 0x60,
    CFG_REG_B_M,
    CFG_REG_C_M,
    INT_CRTL_REG_M,
    INT_SOURCE_REG_M,
    INT_THS_L_REG_M,
    INT_THS_H_REG_M,
    STATUS_REG_M,
    OUTX_L_REG_M,
    OUTX_H_REG_M,
    OUTY_L_REG_M,
    OUTY_H_REG_M,
    OUTZ_L_REG_M,
    OUTZ_H_REG_M,
};

static int master_num;
static const char *TAG = "LSM303";

static esp_err_t i2c_master_write_slave(i2c_port_t i2c_num, uint8_t addr, uint8_t reg, uint8_t *data_wr, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
    if(size > 0)
        i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_master_read_slave(i2c_port_t i2c_num, uint8_t addr, uint8_t *data_rd, size_t size, uint32_t timeout)
{
    if (size == 0) {
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, timeout);
    i2c_cmd_link_delete(cmd);
    return ret;
}

/**
 * @brief Read a sequence of bytes from a lsm303 sensor registers
 */
esp_err_t lsm303_acc_register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    ESP_ERROR_CHECK(i2c_master_write_slave(master_num, LSM303_ACC_ADDR, reg_addr, NULL,0));
    return i2c_master_read_slave(master_num, LSM303_ACC_ADDR, data, len, LSM303_I2C_MASTER_TIMEOUT_MS / portTICK_RATE_MS);
}

/**
 * @brief Write a sequence of bytes to a lsm303 sensor registers
 */
esp_err_t lsm303_acc_register_write(uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_write_slave(master_num, LSM303_ACC_ADDR, reg_addr, data, len);
}

/**
 * @brief Write a byte to a lsm303 sensor register
 */
esp_err_t lsm303_acc_register_write_byte(uint8_t reg_addr, uint8_t data)
{
    int ret = ESP_FAIL;
    //uint8_t write_buf[2] = {reg_addr, data};

    //ret = i2c_master_write_to_device(master_num, LSM303_ACC_ADDR, write_buf, sizeof(write_buf), LSM303_I2C_MASTER_TIMEOUT_MS / portTICK_RATE_MS);

    return ret;
}


/**
 * @brief Initialize Driver
 */
esp_err_t lsm303_init(int i2c_master_port, int sda_io_num, int scl_io_num)
{
    uint8_t data;
    master_num = i2c_master_port;
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_io_num,         // select GPIO specific to your project
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = scl_io_num,         // select GPIO specific to your project
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,  // select frequency specific to your project
        //.clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL,          /*!< Optional, you can use I2C_SCLK_SRC_FLAG_* flags to choose i2c source clock here. */
    };

    ESP_ERROR_CHECK(i2c_param_config(i2c_master_port, &conf));

    ESP_ERROR_CHECK(i2c_driver_install(i2c_master_port, conf.mode, 0, 0, 0));

    ESP_ERROR_CHECK(lsm303_acc_register_read(WHO_AM_I_A, &data, 1));
    if (data != LSM303_WHO_AM_I_A_VALUE) 
    {
        ESP_LOGE(TAG, "WHO AM I register does not match! 0x%X != 0x%X", data, LSM303_WHO_AM_I_A_VALUE);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

/**
 * @brief Enable taping detection
 * 
 * @param double_taping enable
 */
esp_err_t lsm303_enable_taping(int double_taping)
{
    lsm303_acc_ctrl_1 ctrl_1 = {
        .odr = 7,
        .fs = 3,
        .hf_odr = 0,
        .bdu = 0
    };
    lsm303_acc_ctrl_3 ctrl_3 = {
        .st = 0,
        .tap_x_en = 1,
        .tap_y_en = 1,
        .tap_z_en = 1,
    };
    ESP_LOGI(TAG, "CTRL1: 0x%X", ctrl_1.byte);
    ESP_ERROR_CHECK(lsm303_acc_register_write(CTRL1_A, &ctrl_1.byte, 1));
    ESP_LOGI(TAG, "CTRL3: 0x%X", ctrl_3.byte);
    ESP_ERROR_CHECK(lsm303_acc_register_write(CTRL3_A, &ctrl_3.byte, 1));
    uint8_t tap_ths = 1;
    ESP_ERROR_CHECK(lsm303_acc_register_write(TAP_6D_THS_A, &tap_ths, 1));
    uint8_t wakeup = 0x80;
    ESP_ERROR_CHECK(lsm303_acc_register_write(WAKE_UP_THS_A, &wakeup, 1));
    

    return ESP_OK;
}


/**
 * @brief Read Tap source register
 */
esp_err_t lsm303_read_tap(uint8_t *tap_src)
{
    esp_err_t ret;
    ret = lsm303_acc_register_read(TAP_SRC_A, tap_src, 1);
    //ESP_LOGI(TAG, "TAP_SRC_A: 0x%X", *tap_src);
    return ret;
}
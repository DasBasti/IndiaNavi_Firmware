
#ifndef __LSM303_H__
#define __LSM303_H__

#include <esp_system.h>
#include <driver/i2c.h>

#define LSM303_I2C_MASTER_TIMEOUT_MS  1000 

#define LSM303_WHO_AM_I_A_VALUE ((uint8_t) 0x43 )
#define LSM303_WHO_AM_I_M_VALUE ((uint8_t) 0x40 )

#define ACK_CHECK_EN 0x1                        /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0                       /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                             /*!< I2C ack value */
#define NACK_VAL 0x1                            /*!< I2C nack value */

/**
 * @brief Control Register 1
 * 
 * @param ODR Output Data Rate.
 *    ODR[3:0] | HF_ODR | Data Rate | Bit resolution | Mode
 *    0000     | -      | -         | -              | PD
 *    1000     | -      | 1         | 10             | LP
 *    1001     | -      | 12.5      | 10             | LP
 *    1010     | -      | 25        | 10             | LP
 *    1011     | -      | 50        | 10             | LP
 *    1100     | -      | 100       | 10             | LP
 *    1101     | -      | 200       | 10             | LP
 *    1110     | -      | 400       | 10             | LP
 *    1111     | -      | 800       | 10             | LP
 *    0001     | -      | 12.5      | 14             | HR
 *    0010     | -      | 25        | 14             | HR
 *    0011     | -      | 50        | 14             | HR
 *    0100     | -      | 100       | 14             | HR
 *    0101     | 0      | 200       | 14             | HR
 *    0110     | 0      | 400       | 14             | HR
 *    0111     | 0      | 800       | 14             | HR
 *    0101     | 1      | 1600      | 12             | HF
 *    0110     | 1      | 3200      | 12             | HF
 *    0111     | 1      | 6400      | 13             | HF
 * @param FS Full-scale selection.
 * @param HF_ODR High-frequency ODR mode enable.
 * @param BDU Block data update
 *   0: continuous update.
 *   1: output registers not updated until MSB and LSB read
 */
typedef union {
    struct {
        uint8_t odr :3;
        uint8_t fs :2;
        uint8_t hf_odr :1;
        uint8_t bdu :1;
    };
    uint8_t byte;
} lsm303_acc_ctrl_1;


/**
 * @brief Control Register 2
 * 
 * @param BOOT Reboot memory content.
 * @param SOFT_RESET Soft reset acts as reset for all control registers, then goes to 0.
 * @param FUNC_CFG_EN Access to advanced configuration registers from address 2Bh to 3Fh.
 * @param FDS_SLOPE High-pass filter data selection on output register and FIFO.
 * @param IF_ADD_INC Register address automatically incremented during multiple byte access. Default: enabled
 * @param I2C_DISABLE Disable I2C communication protocol.
 * @param SPI_ENABLE SPI interface read enable.
 */
typedef union {
    struct {
        uint8_t boot :1;
        uint8_t soft_reset :1;
        uint8_t _zero :1;
        uint8_t func_cfg_en :1;
        uint8_t fds_slope :1;
        uint8_t if_add_inc :1;
        uint8_t i2c_disable :1;
        uint8_t spi_enable :1;    
    };
    uint8_t byte;
} lsm303_acc_ctrl_2;

/**
 * @brief Control Register 3
 * 
 * @param ST Self-test enable
 *    ST2 | ST1 | Self-test mode
 *     0  |  0  | Normal mode
 *     0  |  1  | Positive sign self-test
 *     1  |  0  | Negative sign self-test
 *     1  |  1  | Not allowed
 * @param TAP_X_EN Tap recognition on X direction enable.
 * @param TAP_Y_EN Tap recognition on Y direction enable.
 * @param TAP_Z_EN Tap recognition on Z direction enable.
 * @param LIR Latch Interrupt. 
 * @param H_LACTIVE Interrupt active high, low.
 *    0: Active high
 *    1: Active low
 * @param PP_OD Push-pull/open-drain selection on interrupt pad.
 */
typedef union {
    struct {
        uint8_t st :2;
        uint8_t tap_x_en :1;
        uint8_t tap_y_en :1;
        uint8_t tap_z_en :1;
        uint8_t lir :1;
        uint8_t h_lactive :1;
        uint8_t pp_od :1;
    };
    uint8_t byte;
} lsm303_acc_ctrl_3;

/**
 * @brief Control Register 4
 * 
 * @param INT1_S_TAP Single-tap recognition is routed on INT1 pad.
 * @param INT1_WU Wakeup recognition is routed on INT1 pad.
 * @param INT1_FF Free-fall recognition is routed on INT1 pad.
 * @param INT1_TAP Double-tap recognition is routed on INT1 pad.
 * @param INT1_6D 6D recognition is routed on INT1 pad.
 * @param INT1_FTH FIFO threshold interrupt is routed on INT1 pad.
 * @param INT1_DRDY Data-Ready is routed on INT1 pad.
 */
typedef struct {
    uint8_t _unused :1;
    uint8_t int1_s_tap :1;
    uint8_t int1_wu :1;
    uint8_t int1_ff :1;
    uint8_t int1_tap :1;
    uint8_t int1_6d :1;
    uint8_t int1_fth :1;
    uint8_t int1_drdy :1;
} lsm303_acc_ctrl_4;

/**
 * @brief Control Register 5
 * 
 * @param DRDY_PULSED Data-ready interrupt mode selection: latched mode / pulsed mode.
 * @param INT2_BOOT Wakeup recognition is routed on INT1 pad.
 * @param INT2_ON_INT1 All signals routed on INT2 are also routed on INT1.
 * @param INT2_TILT Tilt event is routed on INT2 pad.
 * @param INT2_SIG_MOT Significant motion detection is routed on INT2 pad.
 * @param INT2_STEP Step detection is routed on INT2 pad. 
 * @param INT2_FTH FIFO threshold interrupt is routed on INT2 pad.
 * @param INT2_DRDY Data-Ready is routed on INT2 pad.
 */
typedef struct {
    uint8_t drdy_pulsed :1;
    uint8_t int2_boot :1;
    uint8_t int2_on_int1 :1;
    uint8_t int2_tilt :1;
    uint8_t int2_sig_mot :1;
    uint8_t int2_step :1;
    uint8_t int2_fth :1;
    uint8_t int2_drdy :1;
} lsm303_acc_ctrl_5;

typedef struct {
    uint8_t MODULE_8BIT_A;
    uint8_t WHO_AM_I_A;
    lsm303_acc_ctrl_1 CTRL1_A;
    lsm303_acc_ctrl_2 CTRL2_A;
    lsm303_acc_ctrl_3 CTRL3_A;
    lsm303_acc_ctrl_4 CTRL4_A;
    lsm303_acc_ctrl_5 CTRL5_A;
    uint8_t FIFO_CTRL_A;
    uint8_t OUT_T_A;
    uint8_t STATUS_A;
    uint8_t OUT_X_L_A;
    uint8_t OUT_X_H_A;
    uint8_t OUT_Y_L_A;
    uint8_t OUT_Y_H_A;
    uint8_t OUT_Z_L_A;
    uint8_t OUT_Z_H_A;
    uint8_t FIFO_THS_A;
    uint8_t FIFO_SRC_A;
    uint8_t FIFO_SAMPLES_A;
    uint8_t TAP_6D_THS_A;
    uint8_t INT_DUR_A;
    uint8_t WAKE_UP_THS_A;
    uint8_t WAKE_UP_DUR_A;
    uint8_t FREE_FALL_A;
    uint8_t STATUS_DUP_A;
    uint8_t WAKE_UP_SRC_A;
    uint8_t TAP_SRC_A;
    uint8_t SIXD_SRC_A;
    uint8_t STEP_COUNTER_MINTHS_A;
    uint8_t STEP_COUNTER_L_A;
    uint8_t STEP_COUNTER_H_A;
    uint8_t FUNC_CK_GATE_A;
    uint8_t FUNC_SRC_A;
    uint8_t FUNC_CTRL_A;
} lsm303_a_regmap_t;


typedef struct {
    uint8_t OFFSET_X_REG_L_M;
    uint8_t OFFSET_X_REG_H_M;
    uint8_t OFFSET_Y_REG_L_m;
    uint8_t OFFSET_Y_REG_H_M;
    uint8_t OFFSET_Z_REG_L_M;
    uint8_t OFFSET_Z_REG_H_M;
    uint8_t WHO_AM_I_M;
    uint8_t CFG_REG_A_M;
    uint8_t CFG_REG_B_M;
    uint8_t CFG_REG_C_M;
    uint8_t INT_CRTL_REG_M;
    uint8_t INT_SOURCE_REG_M;
    uint8_t INT_THS_L_REG_M;
    uint8_t INT_THS_H_REG_M;
    uint8_t STATUS_REG_M;
    uint8_t OUTX_L_REG_M;
    uint8_t OUTX_H_REG_M;
    uint8_t OUTY_L_REG_M;
    uint8_t OUTY_H_REG_M;
    uint8_t OUTZ_L_REG_M;
    uint8_t OUTZ_H_REG_M;
} lsm303_m_regmap_t;

/**
 * @brief Config Register A 0x60
 * 
 * COMP_TEMP_EN: Enable the magnetometer temperature compensation. Default value: 0 (disabled)
 * @param REBOOT Reboot magnetometer memory content. Default value: 0
 * @param SOFT_RST When this bit is set, the configuration registers and user registers are reset. Flash registers keep their values.
 * @param LP Low-power mode enable. Default: 0 - 0: high-resolution mode 1: low-power mode enabled
 * @param ODR Output data rate configuration
 *   ODR1 | ODR0 | Data rate (Hz)
 *    0   |  0   | 10 (default)
 *    0   |  1   | 20
 *    1   |  0   | 50
 *    1   |  1   | 100
 * 
 * @param MD Mode select bit. These bits select the mode of operation of the device
 *    MD1 | MD0 | Mode
 *     0  |  0  | Continuous mode
 *     0  |  1  | Single mode
 *     1  |  *  | Idle mode
 */
typedef struct {
    uint8_t comp_temp_en :1;
    uint8_t reboot :1;
    uint8_t soft_rst :1;
    uint8_t lp :1;
    uint8_t odr :2;
    uint8_t md :2;
} lsm303_mag_cfg_a;

/**
 * @brief Config Register B 0x61
 * 
 * @param OFF_CANC_ONE_SHOT Enables offset cancellation in single measurement mode
 * @param INT_on_DataOFF If ‘1’, the interrupt block recognition checks data after the hard-iron correction to discover the interrupt
 * @param Set_FREQ Selects the frequency of the set pulse.
 *   0: set pulse is released every 63 ODR; 
 *   1: set pulse is released only at power-on after PD condition.
 * @param OFF_CANC Enables offset cancellation
 * @param LPF Low-pass filter enable
 *   0: disable BW ODR/2
 *   1: enable  BW ODR/4
 */
typedef struct {
    uint8_t _unused :3;
    uint8_t off_canc_one_shot :1;
    uint8_t int_on_dataoff :1;
    uint8_t set_freq :1;
    uint8_t off_canc :1;
    uint8_t lpf :1;
} lsm303_mag_cfg_b;

/**
 * @brief Config Register C 0x62
 * 
 * @param INT_MAG_PIN If '1', the INTERRUPT signal is driven on INT_MAG_PIN.
 * @param I2C_DIS If ‘1’, the I2C interface is inhibited
 * @param BDU If enabled, reading of incorrect data is avoided when the user reads asynchronously. In fact if the read request arrives during an update of the output data, a latch is possible, reading incoherent high and low parts of the same register. Only one part is updated and the other one remains old.
 * @param BLE If ‘1’, an inversion of the low and high parts of the data occurs.
 * @param Self_test If ‘1’, the self-test is enabled.
 * @param INT_MAG If ‘1’, the DRDY pin is configured as a digital output.
 */
typedef struct {
    uint8_t _unused :1;
    uint8_t int_mag_pin :1;
    uint8_t i2c_dis :1;
    uint8_t bdu :1;
    uint8_t ble :1;
    uint8_t _zero :1;
    uint8_t self_test :1;
    uint8_t int_mag :1;
} lsm303_mag_cfg_c;

/**
 * @brief Interrupt Control Register 0x63
 * 
 * @param XIEN Enables the interrupt recognition for the X-axis.
 * @param YIEN Enables the interrupt recognition for the Y-axis.
 * @param ZIEN Enables the interrupt recognition for the Z-axis.
 * @param IEA Controls the polarity of the INT when an interrupt occurs.
 * @param IEL Controls whether the INT bit is latched (1) or pulsed (0).
 * Once latched, INT remains in the same state until INT_SOURCE_REG_M (64h) is read.
 * @param IEN Interrupt enable.
 */
typedef struct {
    uint8_t xien :1;
    uint8_t yien :1;
    uint8_t zien :1;
    uint8_t zero :2;
    uint8_t iea :1;
    uint8_t iel :1;
    uint8_t ien :1;
} lsm303_mag_int;

/**
 * @brief Interrupt Source Register 0x64
 * 
 * @param P_TH_S_X X-axis value exceeds the threshold positive side
 * @param P_TH_S_Y Y-axis value exceeds the threshold positive side
 * @param P_TH_S_Z Z-axis value exceeds the threshold positive side
 * @param N_TH_S_X X-axis value exceeds the threshold negative side
 * @param N_TH_S_Y Y-axis value exceeds the threshold negative side
 * @param N_TH_S_Z Z-axis value exceeds the threshold negative side
 * @param MROI MROI flag generation is alway enabled.This flag is reset by reading
 * @param INT This bit signals when the interrupt event occurs.
 */
typedef struct {
    uint8_t p_th_s_x :1;
    uint8_t p_th_s_y :1;
    uint8_t p_th_s_z :1;
    uint8_t n_th_s_x :1;
    uint8_t n_th_s_y :1;
    uint8_t n_th_s_z :1;
    uint8_t mroi :1;
    uint8_t int_flag :1;
} lsm303_mag_int_source_b;

/**
 * @brief Status Register
 * 
 * @param Zyxor X-, Y- and Z-axis data overrun.
 * @param zor   Z-axis data overrun.
 * @param yor   Y-axis data overrun.
 * @param xor   X-axis data overrun.
 * @param Zyxda X-, Y- and Z-axis new data available.
 * @param zda   Z-axis data available.
 * @param yda   Y-axis data available.
 * @param xda   X-axis data available.
 */
typedef struct {
    uint8_t zyxor :1;
    uint8_t zor :1;
    uint8_t yor :1;
    uint8_t xor :1;
    uint8_t zyxda :1;
    uint8_t zda :1;
    uint8_t yda :1;
    uint8_t xda :1;
} lsm303_mag_status_b;


esp_err_t lsm303_acc_register_read(uint8_t reg_addr, uint8_t *data, size_t len);
esp_err_t lsm303_acc_register_write_byte(uint8_t reg_addr, uint8_t data);
esp_err_t lsm303_mag_register_read(uint8_t reg_addr, uint8_t *data, size_t len);
esp_err_t lsm303_mag_register_write_byte(uint8_t reg_addr, uint8_t data);
esp_err_t lsm303_init(int i2c_master_port, int sda_io_num, int scl_io_num);

esp_err_t lsm303_mag_write_cfg();
esp_err_t lsm303_enable_taping(int double_taping);
esp_err_t lsm303_read_tap(uint8_t *tap_src);


#endif /* __LSM303_H__ */

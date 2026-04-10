/*********************************************************************************************************************
 * AI8051U IMU660RC native driver.
 ********************************************************************************************************************/
#ifndef _zf_device_imu660rc_h_
#define _zf_device_imu660rc_h_

#include "zf_common_typedef.h"
#include "zf_device_type.h"

#define IMU660RC_USE_INTERFACE       HARDWARE_SPI

#if (IMU660RC_USE_INTERFACE == HARDWARE_SPI)
#define IMU660RC_SPI_SPEED           ((uint32)5 * 1000 * 1000U)
#define IMU660RC_SPI                 SPI_1
#define IMU660RC_SPC_PIN             SPI1_CH1_SCLK_P17
#define IMU660RC_SDI_PIN             SPI1_CH1_MOSI_P15
#define IMU660RC_SDO_PIN             SPI1_CH1_MISO_P16
#define IMU660RC_CS_PIN              (IO_P47)
#define IMU660RC_CS(x)               ((x) ? (gpio_high(IMU660RC_CS_PIN)) : (gpio_low(IMU660RC_CS_PIN)))
#elif (IMU660RC_USE_INTERFACE == SOFT_SPI)
#define IMU660RC_SPC_PIN             (P17)
#define IMU660RC_SDI_PIN             (P15)
#define IMU660RC_SDO_PIN             (P16)
#define IMU660RC_CS_PIN              (P47)
#elif (IMU660RC_USE_INTERFACE == SOFT_IIC)
#define IMU660RC_SOFT_IIC_DELAY      (0)
#define IMU660RC_SCL_PIN             (IO_P17)
#define IMU660RC_SDA_PIN             (IO_P15)
#endif

#define IMU660RC_INT2_PIN            (INT1_P33)

#define IMU660RC_QUARTERNION_GET_GYRO    (1)
#define IMU660RC_QUARTERNION_GET_ACC     (1)
#define IMU660RC_ACC_SAMPLE_DEFAULT      (IMU660RC_ACC_SAMPLE_SGN_8G)
#define IMU660RC_GYRO_SAMPLE_DEFAULT     (IMU660RC_GYRO_SAMPLE_SGN_2000DPS)

typedef enum
{
    IMU660RC_MAIN_MEM_BANK = 0x00,
    IMU660RC_HUB_MEM_BANK = 0x40,
    IMU660RC_EMBED_MEM_BANK = 0x80
} imu660rc_mem_bank_enum;

typedef enum
{
    IMU660RC_ACC_SAMPLE_SGN_2G,
    IMU660RC_ACC_SAMPLE_SGN_4G,
    IMU660RC_ACC_SAMPLE_SGN_8G,
    IMU660RC_ACC_SAMPLE_SGN_16G
} imu660rc_acc_sample_config;

typedef enum
{
    IMU660RC_GYRO_SAMPLE_SGN_125DPS,
    IMU660RC_GYRO_SAMPLE_SGN_250DPS,
    IMU660RC_GYRO_SAMPLE_SGN_500DPS,
    IMU660RC_GYRO_SAMPLE_SGN_1000DPS,
    IMU660RC_GYRO_SAMPLE_SGN_2000DPS,
    IMU660RC_GYRO_SAMPLE_SGN_4000DPS
} imu660rc_gyro_sample_config;

typedef enum
{
    IMU660RC_QUARTERNION_15HZ,
    IMU660RC_QUARTERNION_30HZ,
    IMU660RC_QUARTERNION_60HZ,
    IMU660RC_QUARTERNION_120HZ,
    IMU660RC_QUARTERNION_240HZ,
    IMU660RC_QUARTERNION_480HZ,
    IMU660RC_QUARTERNION_DISABLE
} imu660rc_quarternion_rate_config;

#define IMU660RC_DEV_ADDR            (0x6B)
#define IMU660RC_SPI_W               (0x00)
#define IMU660RC_SPI_R               (0x80)
#define IMU660RC_TIMEOUT_COUNT       (0x00FF)

#define IMU660RC_FUNC_CFG_ACCESS     (0x01)
#define IMU660RC_INT2_CTRL           (0x0E)
#define IMU660RC_CHIP_ID             (0x0F)
#define IMU660RC_CTRL1               (0x10)
#define IMU660RC_CTRL2               (0x11)
#define IMU660RC_CTRL3               (0x12)
#define IMU660RC_CTRL4               (0x13)
#define IMU660RC_CTRL5               (0x14)
#define IMU660RC_CTRL6               (0x15)
#define IMU660RC_CTRL7               (0x16)
#define IMU660RC_CTRL8               (0x17)
#define IMU660RC_CTRL9               (0x18)
#define IMU660RC_CTRL10              (0x19)
#define IMU660RC_CTRL_STATUS         (0x1A)
#define IMU660RC_STATUS_REG          (0x1E)
#define IMU660RC_OUT_TEMP_L          (0x20)
#define IMU660RC_OUT_TEMP_H          (0x21)
#define IMU660RC_OUTX_L_G            (0x22)
#define IMU660RC_OUTX_H_G            (0x23)
#define IMU660RC_OUTY_L_G            (0x24)
#define IMU660RC_OUTY_H_G            (0x25)
#define IMU660RC_OUTZ_L_G            (0x26)
#define IMU660RC_OUTZ_H_G            (0x27)
#define IMU660RC_OUTX_L_A            (0x28)
#define IMU660RC_OUTX_H_A            (0x29)
#define IMU660RC_OUTY_L_A            (0x2A)
#define IMU660RC_OUTY_H_A            (0x2B)
#define IMU660RC_OUTZ_L_A            (0x2C)
#define IMU660RC_OUTZ_H_A            (0x2D)
#define IMU660RC_PAGE_SEL            (0x02)
#define IMU660RC_EMB_FUNC_EN_A       (0x04)
#define IMU660RC_PAGE_RW             (0x17)
#define IMU660RC_SFLP_ODR            (0x5E)
#define IMU660RC_EMB_FUNC_CFG        (0x63)

extern float imu660rc_transition_factor[2];
extern int16 imu660rc_gyro_x;
extern int16 imu660rc_gyro_y;
extern int16 imu660rc_gyro_z;
extern int16 imu660rc_acc_x;
extern int16 imu660rc_acc_y;
extern int16 imu660rc_acc_z;
extern float imu660rc_roll;
extern float imu660rc_pitch;
extern float imu660rc_yaw;
extern float imu660rc_quarternion[4];

void imu660rc_get_acc(void);
void imu660rc_get_gyro(void);
void imu660rc_get_quarternion(void);

#define imu660rc_acc_transition(acc_value)       ((float)(acc_value) / imu660rc_transition_factor[0])
#define imu660rc_gyro_transition(gyro_value)     ((float)(gyro_value) / imu660rc_transition_factor[1])

void imu660rc_callback(void);
uint8 imu660rc_init(imu660rc_quarternion_rate_config quarternion_rate);

#endif

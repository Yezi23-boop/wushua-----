/*********************************************************************************************************************
 * AI8051U IMU660RC native driver.
 ********************************************************************************************************************/
#include "math.h"
#include "zf_common_debug.h"
#include "zf_driver_delay.h"
#include "zf_driver_spi.h"
#include "zf_driver_gpio.h"
#include "zf_driver_soft_iic.h"
#include "zf_driver_exti.h"
#include "zf_common_interrupt.h"
#include "zf_device_imu660rc.h"

#pragma warning disable = 183
#pragma warning disable = 177

#ifndef M_PI
#define M_PI 3.1415926f
#endif

static uint8 imu660rc_quarternion_rate = IMU660RC_QUARTERNION_DISABLE;
static vuint8 imu660rc_quarternion_updated = 0;

float imu660rc_transition_factor[2] = {0};
int16 imu660rc_gyro_x = 0;
int16 imu660rc_gyro_y = 0;
int16 imu660rc_gyro_z = 0;
int16 imu660rc_acc_x = 0;
int16 imu660rc_acc_y = 0;
int16 imu660rc_acc_z = 0;
float imu660rc_roll = 0.0f;
float imu660rc_pitch = 0.0f;
float imu660rc_yaw = 0.0f;
float imu660rc_quarternion[4] = {0};

#if (IMU660RC_USE_INTERFACE == HARDWARE_SPI)
static void imu660rc_write_register(uint8 reg, uint8 dat)
{
    IMU660RC_CS(0);
    spi_write_8bit_register(IMU660RC_SPI, reg | IMU660RC_SPI_W, dat);
    IMU660RC_CS(1);
}

static void imu660rc_write_registers(uint8 reg, const uint8 *dat, uint32 len)
{
    IMU660RC_CS(0);
    spi_write_8bit_registers(IMU660RC_SPI, reg | IMU660RC_SPI_W, dat, len);
    IMU660RC_CS(1);
}

static uint8 imu660rc_read_register(uint8 reg)
{
    uint8 dat;

    IMU660RC_CS(0);
    dat = spi_read_8bit_register(IMU660RC_SPI, reg | IMU660RC_SPI_R);
    IMU660RC_CS(1);
    return dat;
}

static void imu660rc_read_registers(uint8 reg, uint8 *dat, uint32 len)
{
    IMU660RC_CS(0);
    spi_read_8bit_registers(IMU660RC_SPI, reg | IMU660RC_SPI_R, dat, len);
    IMU660RC_CS(1);
}
#elif (IMU660RC_USE_INTERFACE == SOFT_SPI)
#define IMU660RC_SCK(x)              IMU660RC_SPC_PIN = (x)
#define IMU660RC_MOSI(x)             IMU660RC_SDI_PIN = (x)
#define IMU660RC_MISO                IMU660RC_SDO_PIN
#define IMU660RC_CS(x)               IMU660RC_CS_PIN = (x)

static uint8 imu660rc_simspi_wr_byte(uint8 byte)
{
    uint8 i;

    for (i = 0; i < 8; i++)
    {
        IMU660RC_SCK(0);
        IMU660RC_MOSI(byte & 0x80);
        byte <<= 1;
        IMU660RC_SCK(1);
        byte |= IMU660RC_MISO;
    }
    IMU660RC_SCK(0);
    return byte;
}

static void imu660rc_simspi_w_reg_byte(uint8 cmd, uint8 val)
{
    cmd |= IMU660RC_SPI_W;
    imu660rc_simspi_wr_byte(cmd);
    imu660rc_simspi_wr_byte(val);
}

static void imu660rc_simspi_w_reg_bytes(uint8 cmd, const uint8 *dat_addr, uint32 len)
{
    cmd |= IMU660RC_SPI_W;
    imu660rc_simspi_wr_byte(cmd);
    while (len--)
    {
        imu660rc_simspi_wr_byte(*dat_addr++);
    }
}

static void imu660rc_simspi_r_reg_bytes(uint8 cmd, uint8 *val, uint32 num)
{
    cmd |= IMU660RC_SPI_R;
    imu660rc_simspi_wr_byte(cmd);
    while (num--)
    {
        *val++ = imu660rc_simspi_wr_byte(0);
    }
}

static void imu660rc_write_register(uint8 reg, uint8 dat)
{
    IMU660RC_CS(0);
    imu660rc_simspi_w_reg_byte(reg, dat);
    IMU660RC_CS(1);
}

static void imu660rc_write_registers(uint8 reg, const uint8 *dat, uint32 len)
{
    IMU660RC_CS(0);
    imu660rc_simspi_w_reg_bytes(reg, dat, len);
    IMU660RC_CS(1);
}

static uint8 imu660rc_read_register(uint8 reg)
{
    uint8 dat;

    IMU660RC_CS(0);
    imu660rc_simspi_r_reg_bytes(reg, &dat, 1);
    IMU660RC_CS(1);
    return dat;
}

static void imu660rc_read_registers(uint8 reg, uint8 *dat, uint32 len)
{
    IMU660RC_CS(0);
    imu660rc_simspi_r_reg_bytes(reg, dat, len);
    IMU660RC_CS(1);
}
#elif (IMU660RC_USE_INTERFACE == SOFT_IIC)
static soft_iic_info_struct imu660rc_iic_struct;

#define imu660rc_write_register(reg, dat)           (soft_iic_write_8bit_register(&imu660rc_iic_struct, (reg), (dat)))
#define imu660rc_write_registers(reg, dat, len)     (soft_iic_write_8bit_registers(&imu660rc_iic_struct, (reg), (dat), (len)))
#define imu660rc_read_register(reg)                 (soft_iic_read_8bit_register(&imu660rc_iic_struct, (reg)))
#define imu660rc_read_registers(reg, dat, len)      (soft_iic_read_8bit_registers(&imu660rc_iic_struct, (reg), (dat), (len)))
#endif

static uint32 fp16_to_float(uint16 h)
{
    uint32 f_sgn;
    uint16 h_exp;
    uint32 f_exp;
    uint32 f_sig;

    h_exp = (uint16)(h & 0x7c00u);
    f_sgn = ((uint32)h & 0x8000u) << 16;

    switch (h_exp)
    {
    case 0x0000u:
    {
        uint16 h_sig = (uint16)(h & 0x03ffu);

        if (0 == h_sig)
        {
            return f_sgn;
        }

        h_sig <<= 1;
        while (0 == (h_sig & 0x0400u))
        {
            h_sig <<= 1;
            h_exp++;
        }
        f_exp = ((uint32)(127 - 15 - h_exp)) << 23;
        f_sig = ((uint32)(h_sig & 0x03ffu)) << 13;
        return f_sgn + f_exp + f_sig;
    }
    case 0x7c00u:
        return f_sgn + 0x7f800000u + (((uint32)(h & 0x03ffu)) << 13);
    default:
        return f_sgn + (((uint32)(h & 0x7fffu) + 0x1c000u) << 13);
    }
}

static void quarternion_normalize(float quat[4], uint16 *fp16)
{
    float n = 0.0f;
    float temp[4];

    *(uint32 *)(&temp[0]) = fp16_to_float(fp16[0]);
    *(uint32 *)(&temp[1]) = fp16_to_float(fp16[1]);
    *(uint32 *)(&temp[2]) = fp16_to_float(fp16[2]);
    *(uint32 *)(&temp[3]) = fp16_to_float(fp16[3]);

    n = temp[0] * temp[0] + temp[1] * temp[1] + temp[2] * temp[2] + temp[3] * temp[3];
    n = sqrt(n);

    if (n > 0.001f)
    {
        n = (temp[3] < 0.0f) ? (-n) : n;
        quat[0] = temp[1] / n;
        quat[1] = temp[2] / n;
        quat[2] = temp[0] / n;
        quat[3] = temp[3] / n;
    }
}

static void quarternion_to_euler(float quat[4], float *roll, float *pitch, float *yaw)
{
    float euler[3];
    float sqx;
    float sqy;
    float sqz;

    sqx = quat[0] * quat[0];
    sqy = quat[1] * quat[1];
    sqz = quat[2] * quat[2];

    euler[0] = (float)atan2(2.0f * (quat[1] * quat[3] + quat[0] * quat[2]), 1.0f - 2.0f * (sqy + sqx));
    euler[1] = (float)(-asin(2.0f * (quat[0] * quat[3] - quat[1] * quat[2])));
    euler[2] = (float)atan2(2.0f * (quat[0] * quat[1] + quat[2] * quat[3]), 1.0f - 2.0f * (sqx + sqz));

    euler[0] = 180.0f * euler[0] / M_PI;
    euler[1] = 180.0f * euler[1] / M_PI;
    euler[2] = 180.0f * euler[2] / M_PI;
    euler[2] = (euler[2] < 0.0f) ? (euler[2] + 360.0f) : euler[2];

    *roll = euler[0];
    *pitch = euler[1];
    *yaw = euler[2];
}

static void imu660rc_set_mem_bank(imu660rc_mem_bank_enum bank)
{
    imu660rc_write_register(IMU660RC_FUNC_CFG_ACCESS, (uint8)bank);
}

static uint8 imu660rc_self_check(void)
{
    uint8 dat = 0;
    uint8 return_state = 0;
    uint16 timeout_count = 0;

    do
    {
        if (IMU660RC_TIMEOUT_COUNT < timeout_count++)
        {
            return_state = 1;
            break;
        }

        dat = imu660rc_read_register(IMU660RC_CHIP_ID);
        system_delay_ms(1);
    } while (0x70 != dat);

    return return_state;
}

void imu660rc_get_acc(void)
{
    uint8 dat[6];

    if (IMU660RC_QUARTERNION_DISABLE == imu660rc_quarternion_rate)
    {
        imu660rc_read_registers(IMU660RC_OUTX_L_A, dat, 6);
        imu660rc_acc_x = (int16)(((uint16)dat[1] << 8) | dat[0]);
        imu660rc_acc_y = (int16)(((uint16)dat[3] << 8) | dat[2]);
        imu660rc_acc_z = (int16)(((uint16)dat[5] << 8) | dat[4]);
    }
}

void imu660rc_get_gyro(void)
{
    uint8 dat[6];

    if (IMU660RC_QUARTERNION_DISABLE == imu660rc_quarternion_rate)
    {
        imu660rc_read_registers(IMU660RC_OUTX_L_G, dat, 6);
        imu660rc_gyro_x = (int16)(((uint16)dat[1] << 8) | dat[0]);
        imu660rc_gyro_y = (int16)(((uint16)dat[3] << 8) | dat[2]);
        imu660rc_gyro_z = (int16)(((uint16)dat[5] << 8) | dat[4]);
    }
}

void imu660rc_get_quarternion(void)
{
    uint8 i;
    uint16 buff[4];
    uint8 dat[6];
    uint8 *buff_ptr;

    if (IMU660RC_QUARTERNION_DISABLE != imu660rc_quarternion_rate)
    {
        buff_ptr = (uint8 *)buff;

        imu660rc_set_mem_bank(IMU660RC_EMBED_MEM_BANK);
        imu660rc_write_register(IMU660RC_PAGE_RW, 0x20);
        imu660rc_write_register(IMU660RC_PAGE_SEL, 0x31);

        for (i = 0; i < 4; i++)
        {
            imu660rc_write_register(0x08, (uint8)(0x4C + i * 2));
            buff_ptr[i * 2 + 1] = imu660rc_read_register(0x09);
            imu660rc_write_register(0x08, (uint8)(0x4C + i * 2 + 1));
            buff_ptr[i * 2] = imu660rc_read_register(0x09);
        }

        imu660rc_write_register(IMU660RC_PAGE_RW, 0x00);
        imu660rc_set_mem_bank(IMU660RC_MAIN_MEM_BANK);

        quarternion_normalize(imu660rc_quarternion, buff);

#if (1 == IMU660RC_QUARTERNION_GET_ACC)
        imu660rc_read_registers(IMU660RC_OUTX_L_A, dat, 6);
        imu660rc_acc_x = (int16)(((uint16)dat[1] << 8) | dat[0]);
        imu660rc_acc_y = (int16)(((uint16)dat[3] << 8) | dat[2]);
        imu660rc_acc_z = (int16)(((uint16)dat[5] << 8) | dat[4]);
#endif

#if (1 == IMU660RC_QUARTERNION_GET_GYRO)
        imu660rc_read_registers(IMU660RC_OUTX_L_G, dat, 6);
        imu660rc_gyro_x = (int16)(((uint16)dat[1] << 8) | dat[0]);
        imu660rc_gyro_y = (int16)(((uint16)dat[3] << 8) | dat[2]);
        imu660rc_gyro_z = (int16)(((uint16)dat[5] << 8) | dat[4]);
#endif
    }
}

uint8 imu660rc_service(void)
{
    uint8 updated = 0;

    if (imu660rc_quarternion_updated)
    {
        imu660rc_quarternion_updated = 0;
        quarternion_to_euler(imu660rc_quarternion, &imu660rc_roll, &imu660rc_pitch, &imu660rc_yaw);
        updated = 1;
    }

    return updated;
}

void imu660rc_callback(void)
{
    if (gpio_get_level((gpio_pin_enum)(IMU660RC_INT2_PIN & 0xFF)))
    {
        imu660rc_get_quarternion();
        imu660rc_quarternion_updated = 1;
    }
}

uint8 imu660rc_init(imu660rc_quarternion_rate_config quarternion_rate)
{
    uint8 return_state = 0;

    imu660rc_quarternion_rate = quarternion_rate;
    imu660rc_quarternion_updated = 0;

#if (IMU660RC_USE_INTERFACE == HARDWARE_SPI)
    spi_init(IMU660RC_SPI, SPI_MODE0, IMU660RC_SPI_SPEED, IMU660RC_SPC_PIN, IMU660RC_SDI_PIN, IMU660RC_SDO_PIN, SPI_CS_NULL);
    gpio_init(IMU660RC_CS_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);
#elif (IMU660RC_USE_INTERFACE == SOFT_IIC)
    soft_iic_init(&imu660rc_iic_struct, IMU660RC_DEV_ADDR, IMU660RC_SOFT_IIC_DELAY, IMU660RC_SCL_PIN, IMU660RC_SDA_PIN);
#endif

    system_delay_ms(10);

    do
    {
        if (imu660rc_self_check())
        {
            printf("imu660rc self check error.");
            return_state = 1;
            break;
        }

        imu660rc_write_register(IMU660RC_FUNC_CFG_ACCESS, 0x04);
        system_delay_ms(30);

        imu660rc_write_register(IMU660RC_CTRL3, 0x44);

        switch (IMU660RC_ACC_SAMPLE_DEFAULT)
        {
        default:
            printf("IMU660RC_ACC_SAMPLE_DEFAULT set error.");
            return_state = 1;
            break;
        case IMU660RC_ACC_SAMPLE_SGN_2G:
            imu660rc_write_register(IMU660RC_CTRL8, 0x00);
            imu660rc_transition_factor[0] = 16393.44f;
            break;
        case IMU660RC_ACC_SAMPLE_SGN_4G:
            imu660rc_write_register(IMU660RC_CTRL8, 0x01);
            imu660rc_transition_factor[0] = 8196.72f;
            break;
        case IMU660RC_ACC_SAMPLE_SGN_8G:
            imu660rc_write_register(IMU660RC_CTRL8, 0x02);
            imu660rc_transition_factor[0] = 4098.36f;
            break;
        case IMU660RC_ACC_SAMPLE_SGN_16G:
            imu660rc_write_register(IMU660RC_CTRL8, 0x03);
            imu660rc_transition_factor[0] = 2049.18f;
            break;
        }
        if (1 == return_state)
        {
            break;
        }

        switch (IMU660RC_GYRO_SAMPLE_DEFAULT)
        {
        default:
            printf("IMU660RC_GYRO_SAMPLE_DEFAULT set error.");
            return_state = 1;
            break;
        case IMU660RC_GYRO_SAMPLE_SGN_125DPS:
            imu660rc_write_register(IMU660RC_CTRL6, 0x00);
            imu660rc_transition_factor[1] = 228.5714f;
            break;
        case IMU660RC_GYRO_SAMPLE_SGN_250DPS:
            imu660rc_write_register(IMU660RC_CTRL6, 0x01);
            imu660rc_transition_factor[1] = 114.2857f;
            break;
        case IMU660RC_GYRO_SAMPLE_SGN_500DPS:
            imu660rc_write_register(IMU660RC_CTRL6, 0x02);
            imu660rc_transition_factor[1] = 57.1428f;
            break;
        case IMU660RC_GYRO_SAMPLE_SGN_1000DPS:
            imu660rc_write_register(IMU660RC_CTRL6, 0x03);
            imu660rc_transition_factor[1] = 28.5714f;
            break;
        case IMU660RC_GYRO_SAMPLE_SGN_2000DPS:
            imu660rc_write_register(IMU660RC_CTRL6, 0x04);
            imu660rc_transition_factor[1] = 14.2857f;
            break;
        case IMU660RC_GYRO_SAMPLE_SGN_4000DPS:
            imu660rc_write_register(IMU660RC_CTRL6, 0x0C);
            imu660rc_transition_factor[1] = 7.14285f;
            break;
        }
        if (1 == return_state)
        {
            break;
        }

        imu660rc_write_register(IMU660RC_CTRL1, 0x15);
        imu660rc_write_register(IMU660RC_CTRL2, 0x18);
        imu660rc_write_register(IMU660RC_CTRL7, 0x01);
        imu660rc_write_register(IMU660RC_CTRL9, 0x08);

        if (IMU660RC_QUARTERNION_DISABLE != quarternion_rate)
        {
            imu660rc_write_register(IMU660RC_INT2_CTRL, 0x80);
            imu660rc_write_register(IMU660RC_CTRL4, 0x08);
            imu660rc_write_register(IMU660RC_EMB_FUNC_CFG, 0x30);

            imu660rc_write_register(IMU660RC_CTRL1, (uint8)(0x10 | (quarternion_rate + 3)));
            imu660rc_write_register(IMU660RC_CTRL2, (uint8)(0x10 | (quarternion_rate + 3)));

            imu660rc_set_mem_bank(IMU660RC_EMBED_MEM_BANK);
            imu660rc_write_register(IMU660RC_SFLP_ODR, (uint8)(0x43 | (quarternion_rate << 3)));
            imu660rc_write_register(IMU660RC_EMB_FUNC_EN_A, 0x02);
            imu660rc_write_register(IMU660RC_PAGE_RW, 0x00);
            imu660rc_set_mem_bank(IMU660RC_MAIN_MEM_BANK);

            switch (IMU660RC_INT2_PIN)
            {
            case INT0_P32:
                int0_irq_handler = imu660rc_callback;
                exti_init(IMU660RC_INT2_PIN, EXTI_TRIGGER_BOTH);
                interrupt_set_priority(INT0_IRQn, 3);
                break;
            case INT1_P33:
                int1_irq_handler = imu660rc_callback;
                exti_init(IMU660RC_INT2_PIN, EXTI_TRIGGER_BOTH);
                interrupt_set_priority(INT1_IRQn, 3);
                break;
            default:
                break;
            }
        }
    } while (0);

    return return_state;
}

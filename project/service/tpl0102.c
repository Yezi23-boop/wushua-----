#include "zf_common_headfile.h"
#include "tpl0102.h"

#define TPL0102_WIP_TIMEOUT 10000u
#define TPL0102_P3_IIC_MASK 0x30u

static const uint8 tpl0102_addr[TPL0102_DEVICE_COUNT] = {
    TPL0102_ADDR_54,
    TPL0102_ADDR_56};
static uint8 tpl0102_is_online[TPL0102_DEVICE_COUNT] = {0, 0};
static uint8 tpl0102_wra[TPL0102_DEVICE_COUNT] = {
    TPL0102_DEFAULT_CODE, TPL0102_DEFAULT_CODE};
static uint8 tpl0102_wrb[TPL0102_DEVICE_COUNT] = {
    TPL0102_DEFAULT_CODE, TPL0102_DEFAULT_CODE};

static void tpl0102_iic_delay(void);
static void tpl0102_bus_begin(uint8 *p3m0, uint8 *p3m1, uint8 *ea);
static void tpl0102_bus_end(uint8 p3m0, uint8 p3m1, uint8 ea);
static void tpl0102_iic_start(void);
static void tpl0102_iic_stop(void);
static uint8 tpl0102_iic_write_byte(uint8 value);
static uint8 tpl0102_iic_read_byte(void);
static uint8 tpl0102_read_register(uint8 device, uint8 reg, uint8 *value);
static uint8 tpl0102_write_register(uint8 device, uint8 reg, uint8 value);
static uint8 tpl0102_write_ab(uint8 device, uint8 a, uint8 b);
static uint8 tpl0102_write_volatile(uint8 device, uint8 reg, uint8 value);

/** @brief 提供 TPL0102 软件 I2C 所需的短延时。 */
static void tpl0102_iic_delay(void)
{
    volatile uint8 count;

    count = 8;
    while (count--)
    {
    }
}

/** @brief 保存 P3 模式，暂停中断并配置 P3.4/P3.5 开漏 I2C。 */
static void tpl0102_bus_begin(uint8 *p3m0, uint8 *p3m1, uint8 *ea)
{
    *p3m0 = P3M0;
    *p3m1 = P3M1;
    *ea = EA;
    EA = 0;
    P3M0 |= TPL0102_P3_IIC_MASK;
    P3M1 |= TPL0102_P3_IIC_MASK;
    P34 = 1;
    P35 = 1;
    tpl0102_iic_delay();
}

/** @brief 恢复软件 I2C 前的 P3 模式和中断使能状态。 */
static void tpl0102_bus_end(uint8 p3m0, uint8 p3m1, uint8 ea)
{
    P3M0 = p3m0;
    P3M1 = p3m1;
    EA = ea;
}

/** @brief 产生 I2C START 或重复 START 条件。 */
static void tpl0102_iic_start(void)
{
    P35 = 1;
    P34 = 1;
    tpl0102_iic_delay();
    P35 = 0;
    tpl0102_iic_delay();
    P34 = 0;
}

/** @brief 产生 I2C STOP 条件并释放总线。 */
static void tpl0102_iic_stop(void)
{
    P35 = 0;
    P34 = 0;
    tpl0102_iic_delay();
    P34 = 1;
    tpl0102_iic_delay();
    P35 = 1;
    tpl0102_iic_delay();
}

/** @brief 发送一个字节并返回从机 ACK 状态。 */
static uint8 tpl0102_iic_write_byte(uint8 value)
{
    uint8 mask;
    uint8 ack;

    for (mask = 0x80u; mask != 0; mask >>= 1)
    {
        P35 = (value & mask) ? 1 : 0;
        tpl0102_iic_delay();
        P34 = 1;
        tpl0102_iic_delay();
        P34 = 0;
    }
    P35 = 1;
    tpl0102_iic_delay();
    P34 = 1;
    tpl0102_iic_delay();
    ack = (P35 == 0);
    P34 = 0;
    return ack;
}

/** @brief 读取一个字节并发送 NACK。 */
static uint8 tpl0102_iic_read_byte(void)
{
    uint8 i;
    uint8 value;

    value = 0;
    P35 = 1;
    for (i = 0; i < 8; i++)
    {
        P34 = 1;
        tpl0102_iic_delay();
        value = (uint8)((value << 1) | P35);
        P34 = 0;
        tpl0102_iic_delay();
    }
    P35 = 1;
    P34 = 1;
    tpl0102_iic_delay();
    P34 = 0;
    return value;
}

/** @brief 读取一颗 TPL0102 的 8 位寄存器。 */
static uint8 tpl0102_read_register(uint8 device, uint8 reg, uint8 *value)
{
    uint8 p3m0;
    uint8 p3m1;
    uint8 ea;
    uint8 success;

    if (device >= TPL0102_DEVICE_COUNT || value == 0)
    {
        return 0;
    }
    success = 0;
    tpl0102_bus_begin(&p3m0, &p3m1, &ea);
    tpl0102_iic_start();
    if (tpl0102_iic_write_byte((uint8)(tpl0102_addr[device] << 1)) &&
        tpl0102_iic_write_byte(reg))
    {
        tpl0102_iic_start();
        if (tpl0102_iic_write_byte((uint8)((tpl0102_addr[device] << 1) | 1u)))
        {
            *value = tpl0102_iic_read_byte();
            success = 1;
        }
    }
    tpl0102_iic_stop();
    tpl0102_bus_end(p3m0, p3m1, ea);
    return success;
}

/** @brief 写一颗 TPL0102 的 8 位寄存器。 */
static uint8 tpl0102_write_register(uint8 device, uint8 reg, uint8 value)
{
    uint8 p3m0;
    uint8 p3m1;
    uint8 ea;
    uint8 success;

    if (device >= TPL0102_DEVICE_COUNT)
    {
        return 0;
    }
    tpl0102_bus_begin(&p3m0, &p3m1, &ea);
    tpl0102_iic_start();
    success = tpl0102_iic_write_byte((uint8)(tpl0102_addr[device] << 1)) &&
              tpl0102_iic_write_byte(reg) && tpl0102_iic_write_byte(value);
    tpl0102_iic_stop();
    tpl0102_bus_end(p3m0, p3m1, ea);
    return success;
}

/** @brief 从 WRA 开始连续写入 A、B 两个寄存器。 */
static uint8 tpl0102_write_ab(uint8 device, uint8 a, uint8 b)
{
    uint8 p3m0;
    uint8 p3m1;
    uint8 ea;
    uint8 success;

    tpl0102_bus_begin(&p3m0, &p3m1, &ea);
    tpl0102_iic_start();
    success = tpl0102_iic_write_byte((uint8)(tpl0102_addr[device] << 1)) &&
              tpl0102_iic_write_byte(TPL0102_REG_WRA) &&
              tpl0102_iic_write_byte(a) && tpl0102_iic_write_byte(b);
    tpl0102_iic_stop();
    tpl0102_bus_end(p3m0, p3m1, ea);
    return success;
}

/** @brief 切到易失模式后写入一个 WR 寄存器。 */
static uint8 tpl0102_write_volatile(uint8 device, uint8 reg, uint8 value)
{
    if (!tpl0102_is_online[device] ||
        !tpl0102_write_register(device, TPL0102_REG_ACR, TPL0102_ACR_VOLATILE_ENABLE))
    {
        return 0;
    }
    return tpl0102_write_register(device, reg, value);
}

/** @brief 初始化一颗 TPL0102 并读取当前 WR 寄存器。 */
uint8 tpl0102_init(uint8 device)
{
    uint8 acr;

    if (device >= TPL0102_DEVICE_COUNT ||
        !tpl0102_read_register(device, TPL0102_REG_ACR, &acr))
    {
        return 0;
    }
    if (!tpl0102_read_register(device, TPL0102_REG_WRA, &tpl0102_wra[device]) ||
        !tpl0102_read_register(device, TPL0102_REG_WRB, &tpl0102_wrb[device]))
    {
        return 0;
    }
    tpl0102_is_online[device] = 1;
    return 1;
}

/** @brief 查询 TPL0102 在线状态。 */
uint8 tpl0102_online(uint8 device)
{
    return (device < TPL0102_DEVICE_COUNT) ? tpl0102_is_online[device] : 0;
}

/** @brief 获取缓存的 A 通道 WR 值。 */
uint8 tpl0102_get_a(uint8 device)
{
    return (device < TPL0102_DEVICE_COUNT) ? tpl0102_wra[device] : TPL0102_DEFAULT_CODE;
}

/** @brief 获取缓存的 B 通道 WR 值。 */
uint8 tpl0102_get_b(uint8 device)
{
    return (device < TPL0102_DEVICE_COUNT) ? tpl0102_wrb[device] : TPL0102_DEFAULT_CODE;
}

/**
 * @brief 从 TPL0102 易失 WR 寄存器刷新 A/B 缓存。
 * @param device 器件索引。
 * @return 1 表示两个通道均读取成功，0 表示 I2C 读取失败或索引非法。
 * @note 仅用于菜单显示，禁止在 2ms 控制链路调用。
 */
uint8 tpl0102_refresh(uint8 device)
{
    uint8 a;
    uint8 b;

    if (device >= TPL0102_DEVICE_COUNT ||
        !tpl0102_read_register(device, TPL0102_REG_WRA, &a) ||
        !tpl0102_read_register(device, TPL0102_REG_WRB, &b))
    {
        if (device < TPL0102_DEVICE_COUNT)
        {
            tpl0102_is_online[device] = 0;
        }
        return 0;
    }

    tpl0102_wra[device] = a;
    tpl0102_wrb[device] = b;
    tpl0102_is_online[device] = 1;
    return 1;
}

/** @brief 立即写入 A 通道易失 WR 寄存器。 */
uint8 tpl0102_set_a(uint8 device, uint8 value)
{
    if (device >= TPL0102_DEVICE_COUNT ||
        !tpl0102_write_volatile(device, TPL0102_REG_WRA, value))
    {
        return 0;
    }
    tpl0102_wra[device] = value;
    return 1;
}

/** @brief 立即写入 B 通道易失 WR 寄存器。 */
uint8 tpl0102_set_b(uint8 device, uint8 value)
{
    if (device >= TPL0102_DEVICE_COUNT ||
        !tpl0102_write_volatile(device, TPL0102_REG_WRB, value))
    {
        return 0;
    }
    tpl0102_wrb[device] = value;
    return 1;
}

/** @brief 将当前 A/B 缓存值写入 TPL0102 内部 EEPROM。 */
uint8 tpl0102_save(uint8 device)
{
    uint8 acr;
    uint16 timeout;
    uint8 success;

    if (device >= TPL0102_DEVICE_COUNT || !tpl0102_is_online[device] ||
        !tpl0102_read_register(device, TPL0102_REG_ACR, &acr))
    {
        return 0;
    }
    acr = (uint8)((acr & (uint8)~0x80u) | TPL0102_ACR_NONVOLATILE_ENABLE);
    if (!tpl0102_write_register(device, TPL0102_REG_ACR, acr) ||
        !tpl0102_write_ab(device, tpl0102_wra[device], tpl0102_wrb[device]))
    {
        tpl0102_write_register(device, TPL0102_REG_ACR, TPL0102_ACR_VOLATILE_ENABLE);
        return 0;
    }
    success = 0;
    for (timeout = 0; timeout < TPL0102_WIP_TIMEOUT; timeout++)
    {
        if (!tpl0102_read_register(device, TPL0102_REG_ACR, &acr))
        {
            break;
        }
        if ((acr & TPL0102_ACR_WIP_MASK) == 0)
        {
            success = 1;
            break;
        }
    }
    tpl0102_write_register(device, TPL0102_REG_ACR, TPL0102_ACR_VOLATILE_ENABLE);
    return success;
}

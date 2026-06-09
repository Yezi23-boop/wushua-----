#include "zf_common_headfile.h"

#define TPL0102_IIC_HALF_PERIOD_US 300u
#define TPL0102_DEBUG_BEGIN_RETRY 3u
#define TPL0102_DEBUG_RETRY_DELAY_MS 2u
#define TPL0102_WIP_POLL_LIMIT 30u
#define TPL0102_WIP_POLL_DELAY_MS 1u
#define TPL0102_WRITE_ONLY_DEBUG 1

typedef enum
{
    TPL0102_DEVICE_U3 = 0,
    TPL0102_DEVICE_U6,
    TPL0102_DEVICE_COUNT
} TPL0102_Device;

typedef struct
{
    uint8 device; /**< 目标芯片索引，集中屏蔽 U3/U6 与逻辑通道的硬件映射。 */
    uint8 reg;    /**< WRA/WRB 寄存器地址；VOL=0 时同地址访问 IVRA/IVRB。 */
} TPL0102_ChannelMap;

static const uint8 tpl0102_device_addr[TPL0102_DEVICE_COUNT] = {
    TPL0102_U3_ADDR,
    TPL0102_U6_ADDR};

/* 与 ADC.c 的 RAW 显示顺序对齐：P1.0=WB1、P1.1=WA1、P0.1=WB2、P0.0=WA2。 */
static const TPL0102_ChannelMap tpl0102_channel_map[TPL0102_CH_COUNT] = {
    {TPL0102_DEVICE_U6, TPL0102_REG_WRB},
    {TPL0102_DEVICE_U6, TPL0102_REG_WRA},
    {TPL0102_DEVICE_U3, TPL0102_REG_WRB},
    {TPL0102_DEVICE_U3, TPL0102_REG_WRA}};

static uint8 tpl0102_cached_code[TPL0102_CH_COUNT] = {
    TPL0102_DEFAULT_CODE,
    TPL0102_DEFAULT_CODE,
    TPL0102_DEFAULT_CODE,
    TPL0102_DEFAULT_CODE};
static uint8 tpl0102_debug_active = 0;
static uint8 tpl0102_last_error = TPL0102_ERROR_NONE;
static uint8 tpl0102_last_target_code = TPL0102_DEFAULT_CODE;
static uint8 tpl0102_last_readback_code = TPL0102_DEFAULT_CODE;
static uint8 tpl0102_last_acr = 0xFFu;
static uint8 tpl0102_debug_u3_acr = 0xFFu;
static uint8 tpl0102_debug_u6_acr = 0xFFu;
static uint8 tpl0102_debug_bus_idle = 0xFFu;
static uint8 tpl0102_debug_addr_mask = 0;
static uint8 tpl0102_debug_sda_test = 0xFFu;
static uint8 tpl0102_debug_ack_sample = 0xFFu;
static uint8 tpl0102_debug_scan_ack_sample = 0xFFu;
static uint8 tpl0102_saved_tr0 = 0;
static uint8 tpl0102_saved_tr1 = 0;
static uint8 tpl0102_saved_et0 = 0;
static uint8 tpl0102_saved_et1 = 0;

static void tpl0102_iic_delay(void);
static void tpl0102_iic_transaction_begin(void);
static void tpl0102_iic_transaction_end(void);
static void tpl0102_scl_high(void);
static void tpl0102_scl_low(void);
static void tpl0102_sda_low(void);
static void tpl0102_sda_release(void);
static uint8 tpl0102_sda_read(void);
static void tpl0102_iic_init_pins(void);
static void tpl0102_iic_start(void);
static void tpl0102_iic_stop(void);
static uint8 tpl0102_iic_write_byte(uint8 value);
#if !TPL0102_WRITE_ONLY_DEBUG
static uint8 tpl0102_iic_read_byte(uint8 nack);
#endif
static uint8 tpl0102_sample_bus_idle(void);
static uint8 tpl0102_sample_sda_drive_test(void);
static uint8 tpl0102_scan_addr_mask(void);
static uint8 tpl0102_write_register_addr(uint8 addr, uint8 reg, uint8 value);
#if !TPL0102_WRITE_ONLY_DEBUG
static uint8 tpl0102_read_register_addr(uint8 addr, uint8 reg, uint8 *value);
#endif
static uint8 tpl0102_get_addr(uint8 device, uint8 *addr);
static uint8 tpl0102_write_register(uint8 device, uint8 reg, uint8 value);
#if !TPL0102_WRITE_ONLY_DEBUG
static uint8 tpl0102_read_register(uint8 device, uint8 reg, uint8 *value);
#endif
static uint8 tpl0102_write_acr(uint8 device, uint8 acr);
#if !TPL0102_WRITE_ONLY_DEBUG
static uint8 tpl0102_wait_wip_clear(uint8 device);
static uint8 tpl0102_load_cache_from_device(void);
#endif
static uint8 tpl0102_debug_begin_once(void);

/**
 * @brief TPL0102 私有软件 I2C 半周期延时。
 *
 * 菜单调试不在控制链路中运行；这里故意放慢到约 300us 半周期，
 * 给 P35/SDA 外部上拉留出足够恢复时间，避免 ACK 采样被低电平残留误导。
 */
static void tpl0102_iic_delay(void)
{
    system_delay_us(TPL0102_IIC_HALF_PERIOD_US);
}

/**
 * @brief 暂停 T0/T1 定时器，避免 P34/P35 复用脚在软件 I2C 期间被外设干扰。
 *
 * P34/P35 同时是 T0/T1 管脚；该函数只在菜单调试 I2C 事务内短暂调用，
 * 结束后会恢复原来的中断允许和运行状态。
 */
static void tpl0102_iic_transaction_begin(void)
{
    tpl0102_saved_tr0 = TR0;
    tpl0102_saved_tr1 = TR1;
    tpl0102_saved_et0 = ET0;
    tpl0102_saved_et1 = ET1;

    ET0 = 0;
    ET1 = 0;
    TR0 = 0;
    TR1 = 0;
}

/**
 * @brief 恢复 T0/T1 定时器状态。
 */
static void tpl0102_iic_transaction_end(void)
{
    TR0 = tpl0102_saved_tr0;
    TR1 = tpl0102_saved_tr1;
    ET0 = tpl0102_saved_et0;
    ET1 = tpl0102_saved_et1;
}

/** @brief 拉高 P34/SCL。 */
static void tpl0102_scl_high(void)
{
    gpio_high(IO_P34);
}

/** @brief 拉低 P34/SCL。 */
static void tpl0102_scl_low(void)
{
    gpio_low(IO_P34);
}

/**
 * @brief 拉低 P35/SDA。
 *
 * P35 使用 8051 准双向口，写 0 时主动拉低 SDA。
 */
static void tpl0102_sda_low(void)
{
    gpio_low(IO_P35);
}

/**
 * @brief 释放 P35/SDA。
 *
 * 准双向口写 1 后进入可被外部器件拉低的释放状态；TPL0102 的 ACK/读数据
 * 仍可覆盖 SDA。当前板上 P35 用这种模式比开漏/高阻反复切换更稳定。
 */
static void tpl0102_sda_release(void)
{
    gpio_high(IO_P35);
}

/** @brief 读取 P35/SDA 当前实际电平。 */
static uint8 tpl0102_sda_read(void)
{
    return gpio_get_level(IO_P35);
}

/**
 * @brief 初始化 TPL0102 私有 I2C 引脚。
 *
 * P34/SCL 使用推挽输出，避免 SCL 上拉不足导致时钟线空闲读低；
 * P35/SDA 使用准双向口，写 1 释放、写 0 拉低，适配 8051 端口特性。
 */
static void tpl0102_iic_init_pins(void)
{
    gpio_init(IO_P34, GPO, 1, GPO_PUSH_PULL);
    gpio_init(IO_P35, GPIO, 1, GPIO_NO_PULL);
    tpl0102_sda_release();
    tpl0102_scl_high();
    tpl0102_iic_delay();
}

/**
 * @brief 产生 I2C START 条件。
 */
static void tpl0102_iic_start(void)
{
    tpl0102_sda_release();
    tpl0102_scl_high();
    tpl0102_iic_delay();
    tpl0102_sda_low();
    tpl0102_iic_delay();
    tpl0102_scl_low();
    tpl0102_iic_delay();
}

/**
 * @brief 产生 I2C STOP 条件并释放总线。
 */
static void tpl0102_iic_stop(void)
{
    tpl0102_sda_low();
    tpl0102_iic_delay();
    tpl0102_scl_high();
    tpl0102_iic_delay();
    tpl0102_sda_release();
    tpl0102_iic_delay();
}

/**
 * @brief 写 1 字节并读取从机 ACK。
 *
 * @param[in] value 需要发送的 8 位数据，MSB first。
 * @return 1 表示 ACK；0 表示 NACK。
 */
static uint8 tpl0102_iic_write_byte(uint8 value)
{
    uint8 mask;
    uint8 ack;

    mask = 0x80u;
    while (mask != 0)
    {
        if ((value & mask) != 0)
        {
            tpl0102_sda_release();
        }
        else
        {
            tpl0102_sda_low();
        }
        tpl0102_iic_delay();
        tpl0102_scl_high();
        tpl0102_iic_delay();
        tpl0102_scl_low();
        tpl0102_iic_delay();
        mask >>= 1;
    }

    tpl0102_sda_release();
    tpl0102_iic_delay();
    tpl0102_debug_ack_sample = 0;
    if (tpl0102_sda_read())
    {
        tpl0102_debug_ack_sample |= 1u;
    }
    tpl0102_scl_high();
    tpl0102_iic_delay();
    if (tpl0102_sda_read())
    {
        tpl0102_debug_ack_sample |= 2u;
    }
    ack = (tpl0102_sda_read() == 0);
    tpl0102_scl_low();
    if (tpl0102_sda_read())
    {
        tpl0102_debug_ack_sample |= 4u;
    }
    tpl0102_iic_delay();

    return ack;
}

/**
 * @brief 读取 1 字节并发送 ACK/NACK。
 *
 * @param[in] nack 1 表示最后一字节发送 NACK，0 表示继续读发送 ACK。
 * @return 读取到的数据。
 */
#if !TPL0102_WRITE_ONLY_DEBUG
static uint8 tpl0102_iic_read_byte(uint8 nack)
{
    uint8 i;
    uint8 value;

    value = 0;
    tpl0102_sda_release();
    for (i = 0; i < 8; i++)
    {
        value <<= 1;
        tpl0102_scl_high();
        tpl0102_iic_delay();
        if (tpl0102_sda_read())
        {
            value |= 1u;
        }
        tpl0102_scl_low();
        tpl0102_iic_delay();
    }

    if (nack)
    {
        tpl0102_sda_release();
    }
    else
    {
        tpl0102_sda_low();
    }
    tpl0102_iic_delay();
    tpl0102_scl_high();
    tpl0102_iic_delay();
    tpl0102_scl_low();
    tpl0102_sda_release();
    tpl0102_iic_delay();

    return value;
}
#endif

/**
 * @brief 采样 P34/P35 当前空闲电平。
 *
 * @return bit1=SCL(P34)，bit0=SDA(P35)，正常空闲为 3。
 */
static uint8 tpl0102_sample_bus_idle(void)
{
    return (uint8)((gpio_get_level(IO_P34) << 1) | gpio_get_level(IO_P35));
}

/**
 * @brief 测试 SDA 在调试入口各阶段的实际电平。
 *
 * @return bit0=初始释放，bit1=二次释放，bit2=SCL 低时强拉低后，
 *         bit3=SCL 低时再次释放；正常应为 11。
 */
static uint8 tpl0102_sample_sda_drive_test(void)
{
    uint8 value;

    value = 0;
    if (tpl0102_sda_read())
    {
        value |= 1u;
    }

    tpl0102_sda_release();
    tpl0102_iic_delay();
    if (tpl0102_sda_read())
    {
        value |= 2u;
    }

    // SCL 高时拉低 SDA 会形成 START；自测阶段只测端口，不触发从机状态机。
    tpl0102_scl_low();
    tpl0102_iic_delay();
    tpl0102_sda_low();
    tpl0102_iic_delay();
    if (tpl0102_sda_read())
    {
        value |= 4u;
    }

    tpl0102_sda_release();
    tpl0102_iic_delay();
    if (tpl0102_sda_read())
    {
        value |= 8u;
    }
    tpl0102_scl_high();
    tpl0102_iic_delay();

    return value;
}

/**
 * @brief 扫描 TPL0102 可能的 0x50..0x57 地址 ACK。
 *
 * @return bit0..bit7 对应地址 0x50..0x57；正常 U6=0x54、U3=0x56 时为 80。
 */
static uint8 tpl0102_scan_addr_mask(void)
{
    uint8 i;
    uint8 mask;
    uint8 addr;

    mask = 0;
    for (i = 0; i < 8; i++)
    {
        addr = (uint8)(0x50u + i);
        tpl0102_iic_transaction_begin();
        tpl0102_iic_start();
        if (tpl0102_iic_write_byte((uint8)(addr << 1)))
        {
            mask |= (uint8)(1u << i);
        }
        if (i == 0)
        {
            tpl0102_debug_scan_ack_sample = tpl0102_debug_ack_sample;
        }
        tpl0102_iic_stop();
        tpl0102_iic_transaction_end();
        system_delay_us(20);
    }

    return mask;
}

/**
 * @brief 按 7 位地址写 TPL0102 单寄存器。
 *
 * @param[in] addr 7 位 I2C 地址。
 * @param[in] reg 寄存器地址。
 * @param[in] value 寄存器数据。
 * @return 1 表示地址、寄存器、数据均 ACK；0 表示任一阶段 NACK。
 */
static uint8 tpl0102_write_register_addr(uint8 addr, uint8 reg, uint8 value)
{
    uint8 ok;

    ok = 1;
    tpl0102_iic_transaction_begin();
    tpl0102_iic_start();
#if TPL0102_WRITE_ONLY_DEBUG
    // 临时硬件验证模式：SDA 读回异常时不使用 ACK 判定，只观察写入后模拟量是否变化。
    tpl0102_iic_write_byte((uint8)(addr << 1));
    tpl0102_iic_write_byte(reg);
    tpl0102_iic_write_byte(value);
#else
    if (!tpl0102_iic_write_byte((uint8)(addr << 1)))
    {
        ok = 0;
    }
    else if (!tpl0102_iic_write_byte(reg))
    {
        ok = 0;
    }
    else if (!tpl0102_iic_write_byte(value))
    {
        ok = 0;
    }
#endif
    tpl0102_iic_stop();
    tpl0102_iic_transaction_end();

    return ok;
}

/**
 * @brief 按 7 位地址读取 TPL0102 单寄存器。
 *
 * @param[in] addr 7 位 I2C 地址。
 * @param[in] reg 寄存器地址。
 * @param[out] value 读回数据。
 * @return 1 表示事务完整 ACK；0 表示地址或寄存器阶段 NACK。
 */
#if !TPL0102_WRITE_ONLY_DEBUG
static uint8 tpl0102_read_register_addr(uint8 addr, uint8 reg, uint8 *value)
{
    uint8 ok;

    if (value == 0)
    {
        return 0;
    }

    ok = 1;
    tpl0102_iic_transaction_begin();
    tpl0102_iic_start();
    if (!tpl0102_iic_write_byte((uint8)(addr << 1)))
    {
        ok = 0;
    }
    else if (!tpl0102_iic_write_byte(reg))
    {
        ok = 0;
    }
    else
    {
        tpl0102_iic_start();
        if (!tpl0102_iic_write_byte((uint8)((addr << 1) | 0x01u)))
        {
            ok = 0;
        }
        else
        {
            *value = tpl0102_iic_read_byte(1);
        }
    }
    tpl0102_iic_stop();
    tpl0102_iic_transaction_end();

    return ok;
}
#endif

/**
 * @brief 根据内部设备索引取得 7 位地址。
 *
 * @param[in] device TPL0102_DEVICE_U3 或 TPL0102_DEVICE_U6。
 * @param[out] addr 返回 7 位 I2C 地址。
 * @return 1 表示索引有效；0 表示索引越界。
 */
static uint8 tpl0102_get_addr(uint8 device, uint8 *addr)
{
    if (device >= TPL0102_DEVICE_COUNT || addr == 0)
    {
        return 0;
    }

    *addr = tpl0102_device_addr[device];
    return 1;
}

/**
 * @brief 写 TPL0102 8 位寄存器。
 *
 * @param[in] device 目标 TPL0102 设备索引。
 * @param[in] reg 寄存器地址，0x00/0x01/0x10。
 * @param[in] value 待写入数据。
 * @return 1 表示写入成功；0 表示 I2C NACK。
 */
static uint8 tpl0102_write_register(uint8 device, uint8 reg, uint8 value)
{
    uint8 addr;

    if (!tpl0102_get_addr(device, &addr))
    {
        return 0;
    }

    return tpl0102_write_register_addr(addr, reg, value);
}

/**
 * @brief 读取 TPL0102 8 位寄存器。
 *
 * @param[in] device 目标 TPL0102 设备索引。
 * @param[in] reg 寄存器地址，0x00/0x01/0x10。
 * @param[out] value 读取到的数据。
 * @return 1 表示读取成功；0 表示 I2C NACK。
 */
#if !TPL0102_WRITE_ONLY_DEBUG
static uint8 tpl0102_read_register(uint8 device, uint8 reg, uint8 *value)
{
    uint8 addr;

    if (!tpl0102_get_addr(device, &addr))
    {
        return 0;
    }

    return tpl0102_read_register_addr(addr, reg, value);
}
#endif

/**
 * @brief 写入 ACR 以切换 WR/IVR 访问模式。
 *
 * @param[in] device 目标 TPL0102 设备索引。
 * @param[in] acr ACR 写入值。
 * @return 1 表示写入成功；0 表示 I2C NACK。
 */
static uint8 tpl0102_write_acr(uint8 device, uint8 acr)
{
    return tpl0102_write_register(device, TPL0102_REG_ACR, acr);
}

/**
 * @brief 等待 TPL0102 内部 EEPROM 写周期结束。
 *
 * @param[in] device 目标 TPL0102 设备索引。
 * @return 1 表示 WIP 在超时前清零；0 表示读取失败或超时。
 */
#if !TPL0102_WRITE_ONLY_DEBUG
static uint8 tpl0102_wait_wip_clear(uint8 device)
{
    uint8 i;
    uint8 acr;

    for (i = 0; i < TPL0102_WIP_POLL_LIMIT; i++)
    {
        if (!tpl0102_read_register(device, TPL0102_REG_ACR, &acr))
        {
            return 0;
        }

        if ((acr & TPL0102_ACR_WIP_MASK) == 0)
        {
            return 1;
        }

        system_delay_ms(TPL0102_WIP_POLL_DELAY_MS);
    }

    return 0;
}
#endif

/**
 * @brief 从两颗 TPL0102 当前 WR 读取四路缓存。
 *
 * @return 1 表示四路读取成功；0 表示任一路读取失败。
 */
#if !TPL0102_WRITE_ONLY_DEBUG
static uint8 tpl0102_load_cache_from_device(void)
{
    uint8 i;
    uint8 value;
    const TPL0102_ChannelMap *map;

    for (i = 0; i < TPL0102_CH_COUNT; i++)
    {
        map = &tpl0102_channel_map[i];
        if (!tpl0102_read_register(map->device, map->reg, &value))
        {
            if (map->device == TPL0102_DEVICE_U3)
            {
                tpl0102_last_error = TPL0102_ERROR_U3_READ;
            }
            else
            {
                tpl0102_last_error = TPL0102_ERROR_U6_READ;
            }
            return 0;
        }
        tpl0102_cached_code[i] = value;
    }

    return 1;
}
#endif

/**
 * @brief 单次尝试打开 TPL0102 调试会话。
 *
 * @return 1 表示两颗器件均已进入 volatile WR 模式；0 表示本次 I2C 尝试失败。
 */
static uint8 tpl0102_debug_begin_once(void)
{
#if !TPL0102_WRITE_ONLY_DEBUG
    uint8 ok;
    uint8 acr;

    ok = 1;
#endif
    tpl0102_last_error = TPL0102_ERROR_NONE;
    tpl0102_debug_u3_acr = 0xFFu;
    tpl0102_debug_u6_acr = 0xFFu;

#if TPL0102_WRITE_ONLY_DEBUG
    // 只写验证版不依赖 ACK/读回；用于判断 TPL0102 是否能实际收到写入波形。
    tpl0102_write_acr(TPL0102_DEVICE_U3, TPL0102_ACR_VOLATILE_ENABLE);
    tpl0102_write_acr(TPL0102_DEVICE_U6, TPL0102_ACR_VOLATILE_ENABLE);
    tpl0102_debug_u3_acr = TPL0102_ACR_VOLATILE_ENABLE;
    tpl0102_debug_u6_acr = TPL0102_ACR_VOLATILE_ENABLE;
    tpl0102_debug_active = 1;
    tpl0102_last_error = TPL0102_ERROR_NONE;
    return 1;
#else
    if (!tpl0102_write_acr(TPL0102_DEVICE_U3, TPL0102_ACR_VOLATILE_ENABLE))
    {
        tpl0102_last_error = TPL0102_ERROR_U3_ACR;
        ok = 0;
    }
    else if (!tpl0102_write_acr(TPL0102_DEVICE_U6, TPL0102_ACR_VOLATILE_ENABLE))
    {
        tpl0102_last_error = TPL0102_ERROR_U6_ACR;
        ok = 0;
    }
    else if (!tpl0102_read_register(TPL0102_DEVICE_U3, TPL0102_REG_ACR, &acr))
    {
        tpl0102_last_error = TPL0102_ERROR_U3_READ;
        ok = 0;
    }
    else
    {
        tpl0102_debug_u3_acr = acr;
        if (!tpl0102_read_register(TPL0102_DEVICE_U6, TPL0102_REG_ACR, &acr))
        {
            tpl0102_last_error = TPL0102_ERROR_U6_READ;
            ok = 0;
        }
        else
        {
            tpl0102_debug_u6_acr = acr;
            if (tpl0102_debug_u3_acr != TPL0102_ACR_VOLATILE_ENABLE ||
                tpl0102_debug_u6_acr != TPL0102_ACR_VOLATILE_ENABLE)
            {
                tpl0102_last_error = TPL0102_ERROR_SET_MODE;
                ok = 0;
            }
            else if (!tpl0102_load_cache_from_device())
            {
                ok = 0;
            }
        }
    }

    if (ok)
    {
        tpl0102_last_error = TPL0102_ERROR_NONE;
    }

    tpl0102_debug_active = ok;
    return ok;
#endif
}

/**
 * @brief 打开 TPL0102 调试会话并读取当前抽头缓存。
 *
 * 该入口只允许菜单或串口调试路径调用。正常运行期依靠 TPL0102 内部 IVR 上电恢复，
 * 不在 2ms 控制链路里访问 I2C。
 *
 * @return 1 表示两颗器件均可访问且缓存刷新成功；0 表示 I2C 异常。
 */
uint8 tpl0102_debug_begin(void)
{
    uint8 i;

    tpl0102_iic_init_pins();
    tpl0102_debug_sda_test = tpl0102_sample_sda_drive_test();
    tpl0102_debug_bus_idle = tpl0102_sample_bus_idle();
    if (tpl0102_debug_bus_idle != 0x03u)
    {
        tpl0102_last_error = TPL0102_ERROR_SET_MODE;
        tpl0102_debug_u3_acr = 0xFFu;
        tpl0102_debug_u6_acr = 0xFFu;
        tpl0102_debug_active = 0;
        return 0;
    }

    tpl0102_debug_addr_mask = 0;
    for (i = 0; i < TPL0102_DEBUG_BEGIN_RETRY; i++)
    {
        if (tpl0102_debug_begin_once())
        {
            return 1;
        }

        tpl0102_iic_init_pins();
        system_delay_ms(TPL0102_DEBUG_RETRY_DELAY_MS);
    }

    tpl0102_iic_init_pins();
    tpl0102_debug_addr_mask = tpl0102_scan_addr_mask();
    tpl0102_debug_active = 0;
    return 0;
}

/**
 * @brief 关闭 TPL0102 调试会话的写保护状态。
 *
 * @return void
 */
void tpl0102_debug_end(void)
{
    tpl0102_debug_active = 0;
    tpl0102_iic_stop();
}

/**
 * @brief 写入单路 TPL0102 volatile WR 抽头码。
 *
 * @param[in] channel 逻辑通道。
 * @param[in] tap_code 抽头码，范围 0..255。
 * @return 1 表示写入成功；0 表示未打开调试、通道非法或 I2C NACK。
 */
uint8 tpl0102_set_channel(TPL0102_Channel channel, uint8 tap_code)
{
    const TPL0102_ChannelMap *map;
#if !TPL0102_WRITE_ONLY_DEBUG
    uint8 readback_code;
    uint8 acr;
#endif

    if (!tpl0102_debug_active || channel >= TPL0102_CH_COUNT)
    {
        tpl0102_last_error = TPL0102_ERROR_SET_WRITE;
        return 0;
    }

    map = &tpl0102_channel_map[channel];
    tpl0102_last_target_code = tap_code;
    tpl0102_last_acr = 0xFFu;
#if !TPL0102_WRITE_ONLY_DEBUG
    if (tpl0102_read_register(map->device, TPL0102_REG_ACR, &acr))
    {
        tpl0102_last_acr = acr;
    }

#else
    if (!tpl0102_write_register(map->device, map->reg, tap_code))
    {
        tpl0102_last_error = TPL0102_ERROR_SET_WRITE;
        return 0;
    }

    tpl0102_last_readback_code = tap_code;
    tpl0102_cached_code[channel] = tap_code;
    tpl0102_last_error = TPL0102_ERROR_NONE;
    return 1;
#endif
#if !TPL0102_WRITE_ONLY_DEBUG
    if (!tpl0102_write_register(map->device, map->reg, tap_code))
    {
        tpl0102_last_error = TPL0102_ERROR_SET_WRITE;
        return 0;
    }

    if (!tpl0102_read_register(map->device, map->reg, &readback_code))
    {
        tpl0102_last_error = TPL0102_ERROR_SET_READBACK;
        return 0;
    }

    // 调试页显示芯片实际读回值，用于区分 I2C 写入问题和模拟链路问题。
    tpl0102_last_readback_code = readback_code;
    tpl0102_cached_code[channel] = readback_code;
    if (readback_code != tap_code)
    {
        tpl0102_last_error = TPL0102_ERROR_SET_MISMATCH;
        return 1;
    }

    tpl0102_last_error = TPL0102_ERROR_NONE;
    return 1;
#endif
}

/**
 * @brief 批量写入四路 TPL0102 volatile WR 抽头码。
 *
 * @param[in] tap_codes 四路抽头码数组。
 * @return 1 表示全部写入成功；0 表示任一路失败。
 */
uint8 tpl0102_set_all(const uint8 *tap_codes)
{
    uint8 i;

    if (tap_codes == 0)
    {
        return 0;
    }

    for (i = 0; i < TPL0102_CH_COUNT; i++)
    {
        if (!tpl0102_set_channel((TPL0102_Channel)i, tap_codes[i]))
        {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief 保存单路抽头码到 TPL0102 内部 IVR。
 *
 * 保存动作会触发芯片内部 EEPROM 写周期，因此只允许用户确认保存时调用。
 * 失败后会关闭调试会话，避免后续菜单继续写入不确定状态的总线。
 *
 * @param[in] channel 逻辑通道。
 * @param[in] tap_code 需要保存的抽头码。
 * @return 1 表示保存完成；0 表示 NACK、WIP 超时或模式恢复失败。
 */
uint8 tpl0102_save_channel(TPL0102_Channel channel, uint8 tap_code)
{
#if TPL0102_WRITE_ONLY_DEBUG
    // 只写验证模式只用于观察 volatile WR 是否能改变模拟量，不触发 EEPROM 写周期。
    tpl0102_last_target_code = tap_code;
    if (channel >= TPL0102_CH_COUNT)
    {
        tpl0102_debug_active = 0;
    }
    tpl0102_last_error = TPL0102_ERROR_SAVE;
    return 0;
#else
    const TPL0102_ChannelMap *map;
    uint8 ok;

    if (!tpl0102_debug_active || channel >= TPL0102_CH_COUNT)
    {
        tpl0102_last_error = TPL0102_ERROR_SAVE;
        return 0;
    }

    map = &tpl0102_channel_map[channel];
    ok = 1;

    if (!tpl0102_write_acr(map->device, TPL0102_ACR_NONVOLATILE_ENABLE))
    {
        ok = 0;
    }
    else if (!tpl0102_write_register(map->device, map->reg, tap_code))
    {
        ok = 0;
    }
    else if (!tpl0102_wait_wip_clear(map->device))
    {
        ok = 0;
    }

    if (!tpl0102_write_acr(map->device, TPL0102_ACR_VOLATILE_ENABLE))
    {
        ok = 0;
    }

    if (ok)
    {
        tpl0102_last_error = TPL0102_ERROR_NONE;
        tpl0102_cached_code[channel] = tap_code;
    }
    else
    {
        tpl0102_last_error = TPL0102_ERROR_SAVE;
        tpl0102_debug_active = 0;
    }

    return ok;
#endif
}

/**
 * @brief 保存四路抽头码到 TPL0102 内部 IVR。
 *
 * @param[in] tap_codes 四路抽头码数组。
 * @return 1 表示全部保存成功；0 表示任一路失败。
 */
uint8 tpl0102_save_all(const uint8 *tap_codes)
{
    uint8 i;

    if (tap_codes == 0)
    {
        return 0;
    }

    for (i = 0; i < TPL0102_CH_COUNT; i++)
    {
        if (!tpl0102_save_channel((TPL0102_Channel)i, tap_codes[i]))
        {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief 读取本模块缓存的抽头码。
 *
 * @param[in] channel 逻辑通道。
 * @return 对应缓存码；通道非法时返回 `TPL0102_DEFAULT_CODE`。
 */
uint8 tpl0102_get_cached_code(TPL0102_Channel channel)
{
    if (channel >= TPL0102_CH_COUNT)
    {
        return TPL0102_DEFAULT_CODE;
    }

    return tpl0102_cached_code[channel];
}

/**
 * @brief 读取最近一次 TPL0102 调试错误码。
 *
 * @return 0 表示无错误；1..9 对应菜单页显示的 E1..E9。
 */
uint8 tpl0102_get_last_error(void)
{
    return tpl0102_last_error;
}

/**
 * @brief 读取最近一次调试写入的目标抽头码。
 *
 * @return 最近一次写入请求值。
 */
uint8 tpl0102_get_last_target_code(void)
{
    return tpl0102_last_target_code;
}

/**
 * @brief 读取最近一次调试写入后的寄存器读回值。
 *
 * @return 最近一次读回值。
 */
uint8 tpl0102_get_last_readback_code(void)
{
    return tpl0102_last_readback_code;
}

/**
 * @brief 读取最近一次调试写入前采集到的 ACR 值。
 *
 * @return ACR 值；0xFF 表示本次 ACR 读取失败。
 */
uint8 tpl0102_get_last_acr(void)
{
    return tpl0102_last_acr;
}

/**
 * @brief 读取 K3 打开调试后 U3 的 ACR 读回值。
 *
 * @return U3 ACR；0xFF 表示读取失败。
 */
uint8 tpl0102_get_debug_u3_acr(void)
{
    return tpl0102_debug_u3_acr;
}

/**
 * @brief 读取 K3 打开调试后 U6 的 ACR 读回值。
 *
 * @return U6 ACR；0xFF 表示读取失败。
 */
uint8 tpl0102_get_debug_u6_acr(void)
{
    return tpl0102_debug_u6_acr;
}

/**
 * @brief 读取 K3 打开调试前 I2C 总线空闲电平。
 *
 * @return bit1=SCL(P34)，bit0=SDA(P35)，正常应为 3。
 */
uint8 tpl0102_get_debug_bus_idle(void)
{
    return tpl0102_debug_bus_idle;
}

/**
 * @brief 读取 K3 打开调试时的 0x50..0x57 地址 ACK 掩码。
 *
 * @return U6=0x54、U3=0x56 正常为 80。
 */
uint8 tpl0102_get_debug_addr_mask(void)
{
    return tpl0102_debug_addr_mask;
}

/**
 * @brief 读取 K3 打开调试时的 SDA 拉低/释放测试结果。
 *
 * @return bit0=初始释放，bit1=二次释放，bit2=SCL 低时强拉低后，
 *         bit3=SCL 低时再次释放；正常应为 11。
 */
uint8 tpl0102_get_debug_sda_test(void)
{
    return tpl0102_debug_sda_test;
}

/**
 * @brief 读取地址扫描首个 ACK 位的 SDA 采样结果。
 *
 * @return bit0=释放 SDA 后，bit1=SCL 高电平采样，bit2=SCL 拉低后；1 表示 SDA 高。
 */
uint8 tpl0102_get_debug_scan_ack_sample(void)
{
    return tpl0102_debug_scan_ack_sample;
}

/**
 * @brief 查询 TPL0102 调试会话状态。
 *
 * @return 1 表示会话打开；0 表示未打开或已因错误关闭。
 */
uint8 tpl0102_is_debug_active(void)
{
    return tpl0102_debug_active;
}

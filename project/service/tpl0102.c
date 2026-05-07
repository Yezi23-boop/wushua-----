#include "zf_common_headfile.h"

#define TPL0102_SOFT_IIC_DELAY 2u
#define TPL0102_WIP_POLL_LIMIT 30u
#define TPL0102_WIP_POLL_DELAY_MS 1u

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

extern void soft_iic_start(soft_iic_info_struct *soft_iic_obj);
extern void soft_iic_stop(soft_iic_info_struct *soft_iic_obj);
extern uint8 soft_iic_send_data(soft_iic_info_struct *soft_iic_obj, const uint8 dat);
extern uint8 soft_iic_read_data(soft_iic_info_struct *soft_iic_obj, uint8 ack);

static soft_iic_info_struct tpl0102_u3_iic;
static soft_iic_info_struct tpl0102_u6_iic;

static soft_iic_info_struct *tpl0102_iic[TPL0102_DEVICE_COUNT] = {
    &tpl0102_u3_iic,
    &tpl0102_u6_iic};

/* 当前 PCB 网络名显示 U6 对应 1 号、U3 对应 2 号。若实车方向相反，只改此表。 */
static const TPL0102_ChannelMap tpl0102_channel_map[TPL0102_CH_COUNT] = {
    {TPL0102_DEVICE_U6, TPL0102_REG_WRA},
    {TPL0102_DEVICE_U6, TPL0102_REG_WRB},
    {TPL0102_DEVICE_U3, TPL0102_REG_WRA},
    {TPL0102_DEVICE_U3, TPL0102_REG_WRB}};

static uint8 tpl0102_cached_code[TPL0102_CH_COUNT] = {
    TPL0102_DEFAULT_CODE,
    TPL0102_DEFAULT_CODE,
    TPL0102_DEFAULT_CODE,
    TPL0102_DEFAULT_CODE};
static uint8 tpl0102_debug_active = 0;

static uint8 tpl0102_get_iic(uint8 device, soft_iic_info_struct **iic);
static uint8 tpl0102_write_register(uint8 device, uint8 reg, uint8 value);
static uint8 tpl0102_read_register(uint8 device, uint8 reg, uint8 *value);
static uint8 tpl0102_write_acr(uint8 device, uint8 acr);
static uint8 tpl0102_wait_wip_clear(uint8 device);
static uint8 tpl0102_load_cache_from_device(void);

/**
 * @brief 根据设备索引取得软件 I2C 对象。
 *
 * @param[in] device TPL0102_DEVICE_U3 或 TPL0102_DEVICE_U6。
 * @param[out] iic 返回对应软件 I2C 对象。
 * @return 1 表示索引有效；0 表示索引越界。
 */
static uint8 tpl0102_get_iic(uint8 device, soft_iic_info_struct **iic)
{
    if (device >= TPL0102_DEVICE_COUNT)
    {
        return 0;
    }

    *iic = tpl0102_iic[device];
    return 1;
}

/**
 * @brief 写 TPL0102 8 位寄存器并检查每个字节 ACK。
 *
 * @param[in] device 目标 TPL0102 设备索引。
 * @param[in] reg 寄存器地址，0x00/0x01/0x10。
 * @param[in] value 待写入数据。
 * @return 1 表示地址、寄存器、数据均 ACK；0 表示任一阶段 NACK。
 */
static uint8 tpl0102_write_register(uint8 device, uint8 reg, uint8 value)
{
    soft_iic_info_struct *iic;
    uint8 ok;

    if (!tpl0102_get_iic(device, &iic))
    {
        return 0;
    }

    ok = 1;
    soft_iic_start(iic);
    if (!soft_iic_send_data(iic, (uint8)(iic->addr << 1)))
    {
        ok = 0;
    }
    else if (!soft_iic_send_data(iic, reg))
    {
        ok = 0;
    }
    else if (!soft_iic_send_data(iic, value))
    {
        ok = 0;
    }
    soft_iic_stop(iic);

    return ok;
}

/**
 * @brief 读取 TPL0102 8 位寄存器。
 *
 * @param[in] device 目标 TPL0102 设备索引。
 * @param[in] reg 寄存器地址，0x00/0x01/0x10。
 * @param[out] value 读取到的数据。
 * @return 1 表示读事务完整 ACK；0 表示地址或寄存器阶段 NACK。
 */
static uint8 tpl0102_read_register(uint8 device, uint8 reg, uint8 *value)
{
    soft_iic_info_struct *iic;
    uint8 ok;

    if (!tpl0102_get_iic(device, &iic) || value == 0)
    {
        return 0;
    }

    ok = 1;
    soft_iic_start(iic);
    if (!soft_iic_send_data(iic, (uint8)(iic->addr << 1)))
    {
        ok = 0;
    }
    else if (!soft_iic_send_data(iic, reg))
    {
        ok = 0;
    }
    else
    {
        soft_iic_start(iic);
        if (!soft_iic_send_data(iic, (uint8)((iic->addr << 1) | 0x01u)))
        {
            ok = 0;
        }
        else
        {
            *value = soft_iic_read_data(iic, 1);
        }
    }
    soft_iic_stop(iic);

    return ok;
}

/**
 * @brief 写入 ACR 以切换 volatile/IVR 访问模式。
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

/**
 * @brief 从两颗 TPL0102 当前 WR 读取四路缓存。
 *
 * @return 1 表示四路读取成功；0 表示任一路读取失败。
 */
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
            return 0;
        }
        tpl0102_cached_code[i] = value;
    }

    return 1;
}

/**
 * @brief 打开 TPL0102 调试会话并读取当前抽头缓存。
 *
 * 该入口会临时占用 P3.4/P3.5 软件 I2C，因此只允许菜单或串口
 * 调试路径调用，正常运行期依靠 TPL0102 内部 IVR 上电恢复。
 *
 * @return 1 表示两颗器件均可访问且缓存刷新成功；0 表示 I2C 异常。
 */
uint8 tpl0102_debug_begin(void)
{
    uint8 ok;

    soft_iic_init(&tpl0102_u3_iic, TPL0102_U3_ADDR, TPL0102_SOFT_IIC_DELAY, IO_P34, IO_P35);
    soft_iic_init(&tpl0102_u6_iic, TPL0102_U6_ADDR, TPL0102_SOFT_IIC_DELAY, IO_P34, IO_P35);

    ok = 1;
    if (!tpl0102_write_acr(TPL0102_DEVICE_U3, TPL0102_ACR_VOLATILE_ENABLE))
    {
        ok = 0;
    }
    if (!tpl0102_write_acr(TPL0102_DEVICE_U6, TPL0102_ACR_VOLATILE_ENABLE))
    {
        ok = 0;
    }
    if (ok && !tpl0102_load_cache_from_device())
    {
        ok = 0;
    }

    tpl0102_debug_active = ok;
    return ok;
}

/**
 * @brief 关闭 TPL0102 调试会话的写保护状态。
 *
 * @return void
 */
void tpl0102_debug_end(void)
{
    tpl0102_debug_active = 0;
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

    if (!tpl0102_debug_active || channel >= TPL0102_CH_COUNT)
    {
        return 0;
    }

    map = &tpl0102_channel_map[channel];
    if (!tpl0102_write_register(map->device, map->reg, tap_code))
    {
        return 0;
    }

    tpl0102_cached_code[channel] = tap_code;
    return 1;
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
    const TPL0102_ChannelMap *map;
    uint8 ok;

    if (!tpl0102_debug_active || channel >= TPL0102_CH_COUNT)
    {
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
        tpl0102_cached_code[channel] = tap_code;
    }
    else
    {
        tpl0102_debug_active = 0;
    }

    return ok;
}

/**
 * @brief 保存四路抽头码到 TPL0102 内部 IVR。
 *
 * @param[in] tap_codes 四路抽头码数组。
 * @return 1 表示全部保存成功；0 表示任一路保存失败。
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
 * @brief 查询 TPL0102 调试会话状态。
 *
 * @return 1 表示会话打开；0 表示未打开或已因错误关闭。
 */
uint8 tpl0102_is_debug_active(void)
{
    return tpl0102_debug_active;
}

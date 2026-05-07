#ifndef __TPL0102_H__
#define __TPL0102_H__

#include "zf_common_headfile.h"

#define TPL0102_U3_ADDR 0x56u
#define TPL0102_U6_ADDR 0x54u

#define TPL0102_REG_WRA 0x00u
#define TPL0102_REG_WRB 0x01u
#define TPL0102_REG_ACR 0x10u
#define TPL0102_ACR_VOLATILE_ENABLE 0xC0u
#define TPL0102_ACR_NONVOLATILE_ENABLE 0x40u
#define TPL0102_ACR_WIP_MASK 0x20u
#define TPL0102_DEFAULT_CODE 0x80u

typedef enum
{
    TPL0102_CH_LEFT_H = 0,
    TPL0102_CH_LEFT_V,
    TPL0102_CH_RIGHT_H,
    TPL0102_CH_RIGHT_V,
    TPL0102_CH_COUNT
} TPL0102_Channel;

/**
 * @brief 打开 TPL0102 调试会话。
 *
 * 该函数只允许在菜单或串口调试路径调用，用于临时初始化 P3.4/P3.5
 * 软件 I2C，并把两颗 TPL0102 切到 volatile WR 访问模式。
 *
 * @return 1 表示两颗器件进入调试模式成功；0 表示 I2C NACK 或读取缓存失败。
 *
 * @note 不要在 `hardware_init()`、中断或 5ms 控制链路中调用。
 */
uint8 tpl0102_debug_begin(void);

/**
 * @brief 关闭 TPL0102 调试会话。
 *
 * 该函数只清除模块内部可写状态，不主动改动 TPL0102 抽头值。
 * TPL0102 仍保持最后一次写入的 WR 或已保存的 IVR 恢复值。
 *
 * @return void
 */
void tpl0102_debug_end(void);

/**
 * @brief 写入单路 volatile 抽头码。
 *
 * @param[in] channel 逻辑增益通道，必须小于 `TPL0102_CH_COUNT`。
 * @param[in] tap_code 8 位抽头码，0 表示最低端，255 表示最高端。
 * @return 1 表示写入成功并已更新缓存；0 表示未进入调试模式、通道非法或 I2C NACK。
 *
 * @note 只写 TPL0102 的 WR，不消耗芯片内部 EEPROM 寿命。
 */
uint8 tpl0102_set_channel(TPL0102_Channel channel, uint8 tap_code);

/**
 * @brief 批量写入四路 volatile 抽头码。
 *
 * @param[in] tap_codes 四路抽头码数组，顺序与 `TPL0102_Channel` 一致。
 * @return 1 表示全部写入成功；0 表示任一路写入失败。
 */
uint8 tpl0102_set_all(const uint8 *tap_codes);

/**
 * @brief 保存单路抽头码到 TPL0102 内部非易失 IVR。
 *
 * @param[in] channel 逻辑增益通道，必须小于 `TPL0102_CH_COUNT`。
 * @param[in] tap_code 需要保存的 8 位抽头码。
 * @return 1 表示保存完成且 WIP 已清零；0 表示 NACK、超时或恢复 volatile 模式失败。
 *
 * @note 该操作触发 TPL0102 内部 EEPROM 写周期，只能由用户确认保存时调用。
 */
uint8 tpl0102_save_channel(TPL0102_Channel channel, uint8 tap_code);

/**
 * @brief 保存四路抽头码到 TPL0102 内部非易失 IVR。
 *
 * @param[in] tap_codes 四路抽头码数组，顺序与 `TPL0102_Channel` 一致。
 * @return 1 表示全部保存成功；0 表示任一路保存失败。
 */
uint8 tpl0102_save_all(const uint8 *tap_codes);

/**
 * @brief 读取当前缓存的抽头码。
 *
 * @param[in] channel 逻辑增益通道，必须小于 `TPL0102_CH_COUNT`。
 * @return 当前缓存抽头码；通道非法时返回 `TPL0102_DEFAULT_CODE`。
 */
uint8 tpl0102_get_cached_code(TPL0102_Channel channel);

/**
 * @brief 查询当前是否处于 TPL0102 调试会话。
 *
 * @return 1 表示调试会话已打开；0 表示未打开或上次打开失败。
 */
uint8 tpl0102_is_debug_active(void);

#endif /* __TPL0102_H__ */

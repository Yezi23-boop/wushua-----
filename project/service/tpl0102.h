#ifndef __TPL0102_H__
#define __TPL0102_H__

#include "zf_common_typedef.h"

#define TPL0102_DEVICE_54 0u
#define TPL0102_DEVICE_56 1u
#define TPL0102_DEVICE_COUNT 2u

#define TPL0102_ADDR_54 0x54u
#define TPL0102_ADDR_56 0x56u

#define TPL0102_REG_WRA 0x00u
#define TPL0102_REG_WRB 0x01u
#define TPL0102_REG_ACR 0x10u
#define TPL0102_ACR_VOLATILE_ENABLE 0xC0u
#define TPL0102_ACR_NONVOLATILE_ENABLE 0x40u
#define TPL0102_ACR_WIP_MASK 0x20u
#define TPL0102_DEFAULT_CODE 0x80u

/**
 * @brief 初始化一颗 TPL0102 并读取当前 WR 寄存器。
 * @param device 器件索引，`TPL0102_DEVICE_54` 或 `TPL0102_DEVICE_56`。
 * @return 1 表示器件在线，0 表示未应答。
 */
uint8 tpl0102_init(uint8 device);

/**
 * @brief 查询 TPL0102 初始化时检测到的在线状态。
 * @param device 器件索引。
 * @return 1 表示在线，0 表示离线。
 */
uint8 tpl0102_online(uint8 device);

/**
 * @brief 获取缓存的 A 通道 WR 值。
 * @param device 器件索引。
 * @return 0 至 255 的抽头码。
 */
uint8 tpl0102_get_a(uint8 device);

/**
 * @brief 获取缓存的 B 通道 WR 值。
 * @param device 器件索引。
 * @return 0 至 255 的抽头码。
 */
uint8 tpl0102_get_b(uint8 device);

/**
 * @brief 从 TPL0102 易失 WR 寄存器刷新 A/B 缓存。
 * @param device 器件索引。
 * @return 1 表示两个通道均读取成功，0 表示 I2C 读取失败或索引非法。
 * @note 仅用于菜单显示，禁止在 2ms 控制链路调用。
 */
uint8 tpl0102_refresh(uint8 device);

/**
 * @brief 写入 A 通道易失 WR 寄存器。
 * @param device 器件索引。
 * @param value 0 至 255 的抽头码。
 * @return 1 表示已发起写入并更新缓存，0 表示器件离线或索引非法。
 */
uint8 tpl0102_set_a(uint8 device, uint8 value);

/**
 * @brief 写入 B 通道易失 WR 寄存器。
 * @param device 器件索引。
 * @param value 0 至 255 的抽头码。
 * @return 1 表示已发起写入并更新缓存，0 表示器件离线或索引非法。
 */
uint8 tpl0102_set_b(uint8 device, uint8 value);

/**
 * @brief 将当前 A/B 缓存值写入 TPL0102 内部 EEPROM。
 * @param device 器件索引。
 * @return 1 表示 WIP 已清零并恢复易失模式，0 表示器件离线、读取失败或写超时。
 * @note 仅由菜单确认保存调用，不调用车辆配置 EEPROM。
 */
uint8 tpl0102_save(uint8 device);

#endif

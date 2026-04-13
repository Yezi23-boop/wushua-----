#ifndef __FUYA_H__
#define __FUYA_H__

#include "zf_common_typedef.h"

/**
 * @brief 表面状态枚举
 */
typedef enum
{
    FUYA_SURFACE_GROUND = 0, /**< 平地 */
    FUYA_SURFACE_WALL = 1    /**< 墙面 */
} fuya_surface_state_enum;

/**
 * @brief 负压系统全局变量
 */
extern volatile int fuya_date;             /**< 当前平滑后的输出脉宽，范围 1000~2000 */
extern volatile int fuya_target_pwm;       /**< 当前目标脉宽，范围 1000~2000 */
extern volatile uint8 fuya_target_percent; /**< 当前目标百分比，范围 0~100 */
extern volatile uint8 fuya_surface_state;  /**< 当前识别到的表面状态 */

/**
 * @brief 负压吸附系统初始化
 */
void fuya_Init(void);

/**
 * @brief 直接输出负压占空比
 */
void fuya_motor_output(int pwm);

/**
 * @brief 按百分比直接设置负压输出
 * @param percent 上层百分比，范围 0~100
 */
void fuya_set_percent(uint8 percent);

/**
 * @brief 强制停止负压输出
 * @details 将目标和实际输出都拉回到 ESC 最小脉宽 1000
 */
void fuya_force_stop(void);

/**
 * @brief 两态负压更新逻辑
 * @details 基于 IMU 重力向量识别平地/墙面，并平滑切换输出
 */
void fuya_update_simple(void);

#endif /* __FUYA_H__ */

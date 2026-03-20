#ifndef __FUYA_H__
#define __FUYA_H__

#include "zf_common_typedef.h"

/**
 * @brief 负压系统全局变量
 */
extern volatile int fuya_date;          /**< 最终输出的平滑占空比 */
extern volatile float fuya_date_factor; /**< 目标占空比（未滤波） */
extern volatile uint8 phase;            /**< 当前运动阶段 (0-4) */

/**
 * @brief 负压吸附系统初始化
 */
void fuya_Init(void);

/**
 * @brief 直接输出负压占空比
 */
void fuya_motor_output(int pwm);

/**
 * @brief 简单版负压占空比更新逻辑
 * @details 基于 IMU 姿态解算结果，动态调整风扇吸力，实现墙面、天花板的稳定吸附
 */
void fuya_update_simple(void);

#endif /* __FUYA_H__ */

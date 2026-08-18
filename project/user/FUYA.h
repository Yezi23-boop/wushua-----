#ifndef __FUYA_H__
#define __FUYA_H__

#include "zf_common_typedef.h"

/**
 * @brief 负压系统全局变量
 */
extern volatile int fuya_output_pwm;           /**< 当前固定输出脉宽，范围 500~1000 */
extern volatile uint8 fuya_output_percent;     /**< 当前目标百分比，范围 0~100 */

/**
 * @brief 负压吸附系统初始化
 */
void fuya_init(void);

/**
 * @brief 直接输出负压占空比
 */
void fuya_set_pwm(int pwm);

/**
 * @brief 按百分比直接设置负压输出
 * @param percent 上层百分比，函数内部会限制到 0~100，避免 8 位提前截断。
 */
void fuya_set_percent(float percent);

/**
 * @brief 强制停止负压输出
 * @details 将目标和实际输出都拉回到 ESC 最小脉宽 500
 */
void fuya_stop(void);

#endif /* __FUYA_H__ */

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
extern volatile int fuya_date;                 /**< 当前平滑后的输出脉宽，范围 500~1000 */
extern volatile int fuya_target_pwm;           /**< 当前目标脉宽，范围 500~1000 */
extern volatile uint8 fuya_target_percent;     /**< 当前目标百分比，范围 0~100 */
extern volatile uint8 fuya_surface_state;      /**< 当前识别到的表面状态 */
extern volatile uint8 fuya_cylinder_peak_flag; /**< 圆筒最高点通过标志：1-已过顶待落地，0-普通状态 */
extern volatile float fuya_last_vzc;           /**< 最近一次 roll 角差，单位：度，范围 -180~180。 */

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
 * @details 将目标和实际输出都拉回到 ESC 最小脉宽 500
 */
void fuya_force_stop(void);

/**
 * @brief 负压状态识别与输出平滑更新
 *
 * @details
 * 基于 IMU 的 roll 角差识别平地/墙面工况。通过软斜坡的方式输出目标占空比，
 * 避免直接突变导致外接电池瞬态压降或控制链路干扰。
 *
 * @note 无阻塞，允许在 10ms 中断或主循环中调用。
 */
void fuya_update_simple(void);

/**
 * @brief 应用圆筒过顶专用角度参数。
 * @details 首次进入时备份 app.angle 参数，并切换为圆筒过顶专用权重。
 */
void fuya_apply_cylinder_peak_angle(void);

/**
 * @brief 恢复圆筒过顶前的角度参数。
 */
void fuya_restore_cylinder_peak_angle(void);

/**
 * @brief 10ms 圆筒最高点检测
 * @details
 * 仅在运行态工作。检测到圆筒最高点后置标志位，并临时覆盖角度参数；
 * 回到平地后自动恢复原参数并清除标志位。
 * @param start_state 当前运行状态：2 为允许运行，其余状态视为停用
 */
void fuya_update_cylinder_peak_10ms(int8 start_state);

#endif /* __FUYA_H__ */

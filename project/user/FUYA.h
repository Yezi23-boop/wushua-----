#ifndef __FUYA_H__
#define __FUYA_H__

#include "zf_common_typedef.h"

/**
 * @brief 负压系统全局变量
 */
extern volatile int fuya_date;                 /**< 当前固定输出脉宽，范围 500~1000 */
extern volatile int fuya_target_pwm;           /**< 当前目标脉宽，范围 500~1000 */
extern volatile uint8 fuya_target_percent;     /**< 当前目标百分比，范围 0~100 */
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
 * 当前保留该接口只是兼容旧调用点。函数不改变负压输出，
 * 固定值输出由 `run_time_2()` 中的预启动状态显式调用 `fuya_set_percent()` 完成。
 *
 * @note 无阻塞，允许在初始化、10ms 中断或主循环中调用。
 */
void fuya_update_simple(void);

/**
 * @brief 记录圆筒过顶窗口。
 * @details 当前不再切换 angle 参数，仅保留标志供元素状态机观察和复位。
 */
void fuya_apply_cylinder_peak_angle(void);

/**
 * @brief 清理圆筒过顶窗口状态。
 */
void fuya_restore_cylinder_peak_angle(void);

/**
 * @brief 10ms 圆筒最高点检测
 * @details
 * 仅在运行态工作。检测到圆筒最高点后置标志位；
 * 回到平地后清除标志位。
 * @param start_state 当前运行状态：2 为允许运行，其余状态视为停用
 */
void fuya_update_cylinder_peak_10ms(int8 start_state);

#endif /* __FUYA_H__ */

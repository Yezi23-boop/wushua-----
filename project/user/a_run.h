#ifndef __A_RUN_H__
#define __A_RUN_H__

#include "zf_common_typedef.h"

/**
 * @brief 运行状态全局标志
 */
extern volatile int flat_statr; /**< 运行阶段标志：0-停止，1-准备，2及以上-运行中 */
extern volatile int flat_fly;   /**< 飞坡触发标志：1-正在飞坡/特殊慢速处理，0-正常 */

/**
 * @brief 核心控制任务 1 (建议 5ms 周期)
 * @details 包含：传感器读取、PID 偏差更新、电机 PWM 输出
 */
void run_time_1(void);

/**
 * @brief 核心控制任务 2 (建议 10ms 周期)
 * @details 包含：IMU 姿态解算、电压监测、丢线保护、标定更新
 */
void run_time_2(void);

/**
 * @brief 高级控制任务 3 (备用/测试)
 * @details 采用纯追踪 (Pure Pursuit) 算法与陀螺仪融合控制
 */
void run_time_3(void);

/* --- 测试接口 --- */
void run_test_speed(void);
void run_test_angle(void);

#endif /* __A_RUN_H__ */

#ifndef __A_RUN_H__
#define __A_RUN_H__

#include "zf_common_typedef.h"

/**
 * @brief 运行状态标志
 */
extern volatile int flat_statr; /**< 运行状态镜像：0-停止，1-预启动，2-运行中，3-外部强制启动请求 */
extern volatile int flat_fly;   /**< 飞坡状态标志：1-飞坡控制生效，0-普通巡线状态 */

extern volatile uint8 test_trial_active;
extern volatile uint8 test_trial_armed;
extern volatile uint16 test_trial_elapsed_ms;
extern volatile uint16 test_trial_limit_ms;
extern volatile uint16 test_trial_cooldown_elapsed_ms;
extern volatile uint16 test_trial_cooldown_ms;
extern volatile int16 test_fuya_pwm;
extern volatile int32 test_left_pwm_output;
extern volatile int32 test_right_pwm_output;

/**
 * @brief 5ms 主控制任务
 * @details 完成采样、姿态/转向/速度环计算，并在允许运行时输出 PWM
 */
void run_time_1(void);

/**
 * @brief 10ms 状态管理任务
 * @details 处理 IMU 解算、赛道状态检测、启停状态机和软件定时器
 */
void run_time_2(void);

/**
 * @brief 纯追踪实验任务
 * @details 使用 Pure Pursuit 生成目标轮速，再由速度环完成闭环输出
 */
void run_time_3(void);

/* --- 测试与保护接口 --- */
void run_test_speed(void);
void run_test_angle(void);
void run_test_motor(int speed_l, int speed_r);
void a_run_apply_iap_guard(void);
void ground_load_test_arm(void);
void ground_load_test_fire(void);
void ground_load_test_stop(void);

#endif /* __A_RUN_H__ */

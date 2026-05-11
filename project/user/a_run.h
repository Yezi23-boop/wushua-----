#ifndef __A_RUN_H__
#define __A_RUN_H__

#include "zf_common_typedef.h"

/**
 * @brief 运行模式大阶段的控制状态标志位集合
 */
extern volatile int flat_fly;       /**< 飞坡阶段状态：0-普通巡线，1-保持，2-恢复，3-冷却 */
extern volatile float left_target;  /**< 当前左轮目标速度（用于菜单/调试显示） */
extern volatile float right_target; /**< 当前右轮目标速度（用于菜单/调试显示） */
/**
 * @brief 系统主控制任务 (挂载于 TM0，通常为 5ms)
 * @details 完成采样、转向差速/速度环计算，并在允许运行时输出 PWM。由汇编中断包装后调用，严禁阻塞。
 */
void run_time_1(void);

/**
 * @brief 低频(10ms)状态环调度任务
 *
 * @details
 * 跟随 TM1 中断或者主调度延时被挂载，完成按键机、异常环境识别与软定时器轮询。
 * 承担着高层状态向低层策略投递的职责。
 *
 * @note 同样在时基中断内执行，禁止任何形式阻塞操作
 */
void run_time_2(void);

/**
 * @brief 差速实验任务
 * @details 使用电感偏差直接生成目标轮速，再由速度环完成闭环输出
 */
void run_time_3(void);

/* --- 测试与保护接口 --- */
void run_test_diff(void);
void a_run_apply_iap_guard(void);

#endif /* __A_RUN_H__ */

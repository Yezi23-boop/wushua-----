#ifndef __ISR_H_
#define __ISR_H_

/**
 * @file isr.h
 * @brief 中断任务编译期开关
 * @details
 * 通过宏开关决定 TM0/TM1 中断中挂载的业务函数，便于比赛现场快速裁剪任务链。
 * 注意：高频中断路径应保持轻量，避免在此处开启重型调试逻辑。
 */
/* --- 功能模块开关控制 --- */
#define MAIN_ENABLE_VOFA 0       /**< 是否使能 VOFA+ 串口交互 (可能耗费主循环时间) */
#define MAIN_ENABLE_MENU 0       /* 是否使能 IPS 屏幕菜单交互系统 */
#define MAIN_ENABLE_SPEED_TEST 1 /* 是否使能串口打印速度环测试数据 */

#define MAIN_ENABLE_ISR_PWM 1             /* 是否使能 IPS 屏幕菜单交互系统 */
#define MAIN_ENABLE_ISR_TEST_DIFF_FUNC 0  /* 1: 在 TM0 中断中运行 test_diff_func() */
#define MAIN_ENABLE_ISR_TEST_SPEED_FUNC 0 /* 1: 在 TM0 中断中运行 test_speed_func() */
#define MAIN_ENABLE_ISR_RUN_TIME_1 0      /* 1: 在 TM0 中断中运行主控制环 run_time_1() */
#define MAIN_ENABLE_ISR_RUN_TIME_2 0      /* 1: 在 TM1 中断中运行状态环 run_time_2() */

/**
 * @brief 中断服务函数声明文件
 * @details 本文件由逐飞科技提供，用于存放 AI8051U 的所有硬件中断回调
 */

#endif /* __ISR_H_ */

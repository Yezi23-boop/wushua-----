/**
 * @file test.c
 * @brief 直立辅助功能测试与功能性验证包
 * @details
 * 封装各种孤立于主发版竞速代码的联调方法和驱动实验组件：
 * 主要包括直接设定给定轮速、使用硬编码电感阈值执行基础巡线策略。
 * 本模块设计为离线诊断排障用，在实竞中不要将其宏使能。
 */
#include "zf_common_headfile.h"
#include "test.h"
static int8 time_test = 0;
/* 测试用的目标设定值 */
float test_speed_value = 0;
float test_diff_value = 0;

/**
 * @brief 直接差速测试
 * @details 模拟固定差速输出逻辑，通过修改 test_diff_value 观察电机响应
 */
void test_diff_func(void)
{
    float diff_output;

    time_test++;
    a_run_apply_iap_guard();
    fuya_set_percent(30); /* 运行态全力负压，其他状态关闭负压 */
    diff_output = test_diff_value;

    /* 1. 刷新四元数中断已缓存的陀螺仪 Z 轴反馈量 */
    imu_update_gyro_z_from_imu660rc();
    /* 2. 获取编码器反馈速度 */
    Encoder_get(&PID.left_speed, &PID.right_speed);

    /* 3. 将直接差速目标叠加到速度环 */
    /* 注意：左右轮目标速度方向相反以实现原地或行进间转弯 */
    pid_speed_update(&PID.left_speed, -diff_output, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, +diff_output, PID.right_speed.speed);

    /* 4. 执行电机输出 */
    motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
}

/**
 * @brief 基础速度环追踪测试
 */
void test_speed_func(void)
{
    a_run_apply_iap_guard();
    /* 1. 获取编码器实时速度反馈 */
    Encoder_get(&PID.left_speed, &PID.right_speed);

    /* 2. 更新左右轮速度环 PID */
    pid_speed_update(&PID.left_speed, test_speed_value, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, test_speed_value, PID.right_speed.speed);
//    pid_speed_update(&PID.left_speed, test_speed_value, PID.left_speed.speed);
//    pid_speed_update(&PID.right_speed, test_speed_value, PID.right_speed.speed);
    /* 3. 直接输出 PID 计算得到的占空比 */
    motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
}

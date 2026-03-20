#include "zf_common_headfile.h"
#include "test.h"

/* 测试用的目标设定值 */
float test_speed_value = 0;
float test_angle_value = 0;

/**
 * @brief 角度/姿态追踪测试
 * @details 模拟转向控制逻辑，通过修改 test_angle_value 观察电机响应
 */
void test_angle_func(void)
{
    /* 1. 准备传感器数据（采样并初步处理） */
    Prepare_Data();

    /* 2. 获取编码器反馈速度 */
    Encoder_get(&PID.left_speed, &PID.right_speed);

    /* 3. 直接使用校准后的 gyro_z 参与反馈控制 */
    pid_angle_update(&PID.angle, test_angle_value, gyro_z);

    /* 4. 将角度控制器的输出作为差速调节量，叠加到速度环 */
    /* 注意：左右轮目标速度方向相反以实现原地或行进间转弯 */
    pid_speed_update(&PID.left_speed, -PID.angle.output, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, +PID.angle.output, PID.right_speed.speed);

    /* 5. 执行电机输出 */
    motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
}

/**
 * @brief 基础速度环追踪测试
 */
void test_speed_func(void)
{
    /* 1. 获取编码器实时速度反馈 */
    Encoder_get(&PID.left_speed, &PID.right_speed);

    /* 2. 更新左右轮速度环 PID */
    pid_speed_update(&PID.left_speed, test_speed_value, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, test_speed_value, PID.right_speed.speed);

    /* 3. 直接输出 PID 计算得到的占空比 */
    motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
}

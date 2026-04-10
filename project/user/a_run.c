#include "zf_common_headfile.h"
#include "a_run.h"
#include "a_run_mode.h"
#include "../service/soft_timer.h"

/* --- 运行状态变量 --- */
volatile int flat_statr = 0; /* 运行状态镜像：0-停止，1-预启动，2-运行中，3-外部强制启动请求 */
volatile int flat_fly = 0;   /* 飞坡状态标志位 */

/* --- 周期任务内部变量 --- */
static int steer_div = 0;    /* 2ms 主环分频：用于每 4ms 更新一次转向环 */
static int speed_active = 0; /* 当前参与速度环计算的目标速度 */

/**
 * @brief 2ms 主控制任务
 * @details 按“采样 -> 解算 -> PID -> 输出”的顺序完成一轮核心控制
 */
void run_time_1(void)
{
    float left_target;
    float right_target;
    int32 left_pwm;
    int32 right_pwm;

    /* 1. 执行 IAP 保护，避免复位脚异常时进入错误状态 */
    a_run_apply_iap_guard();

    /* 2. 采集编码器数据 */
    Encoder_get(&PID.left_speed, &PID.right_speed); /* 读取左右轮编码器速度 */

    /* 3. 转向环按 4ms 更新一次，角速度环和速度环继续保持 2ms */
    steer_div++;
    if (steer_div >= 2)
    {
        read_AD();                         /* 采集四路电感 ADC */
        pid_steer_update(&PID.steer, Err); /* 根据赛道偏差更新转向环 */
        steer_div = 0;
    }

    /* 4. 根据飞坡/赛道状态修正当前目标速度 */
    a_run_mode_update_fly_speed(&speed_active);

    /* 5. 串联角度环与速度环 */
    /* 转向反馈环使用校准后的 gyro_z */
    pid_angle_update(&PID.angle, PID.steer.output, gyro_z);
    run_mode_update_angle_output(&PID.angle.output); /* 环岛阶段可覆盖角度环输出 */
    /* 左右轮速度环目标 = 基础速度 ± 姿态补偿 */
    left_target = (float)speed_active - PID.angle.output;
    right_target = (float)speed_active + PID.angle.output;
    pid_speed_update(&PID.left_speed, left_target, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, right_target, PID.right_speed.speed);

    /* 6. 仅在运行态时允许电机输出 */
    if (a_run_mode_get_start_state() == 2)
    {
        left_pwm = motor_apply_speed_deadzone_comp(
            (int32)PID.left_speed.output,
            left_target,
            PID.left_speed.speed,
            MAIN_LEFT_DEADZONE_PWM);
        right_pwm = motor_apply_speed_deadzone_comp(
            (int32)PID.right_speed.output,
            right_target,
            PID.right_speed.speed,
            MAIN_RIGHT_DEADZONE_PWM);
        motor_output(left_pwm, right_pwm);
    }
}

/**
 * @brief 10ms 状态管理任务
 * @details 完成赛道检测、启停状态更新和软件定时器维护
 */
void run_time_2(void)
{
    /* 1. 更新电感动态最大值，用于归一化与标定 */
    scan_track_max_value();

    /* 2. 执行各类保护检测 */
    lost_lines();    /* 丢线保护 */
    dianya_jiance(); /* 电池电压检测 */

    /* 3. 更新启停状态与负压控制 */
    a_run_mode_update_start_state(); /* 按键/外部命令状态机，每 10ms 刷新一次 */
    // 同步对外状态镜像
    flat_statr = a_run_mode_get_start_state();
    a_run_mode_update_fuya_state(); /* 根据当前状态决定是否启用负压 */

    /* 4. 更新软件定时器 */
    soft_timer_update_10ms();
}

/**
 * @brief 纯追踪实验任务
 * @details 使用 Pure Pursuit 生成左右轮目标速度，并通过速度环完成闭环输出
 */
void run_time_3(void)
{
    float left_target = 0.0f;
    float right_target = 0.0f;
    int32 left_pwm;
    int32 right_pwm;

    a_run_apply_iap_guard();
    read_AD();
    Encoder_get(&PID.left_speed, &PID.right_speed);

    /* 更新转向环 PD 输出 */
    pid_steer_update(&PID.steer, Err);

    /* 由纯追踪算法生成左右轮目标速度 */
    {
        float norm_err = Err / 100.0f; /* 将偏差缩放到算法所需量级 */
        /* 该实验路径同样使用校准后的 gyro_z */
        Pure_Pursuit_Gyro_Control(app.speed.speed_run, norm_err, gyro_z, &left_target, &right_target);
    }

    /* 将目标轮速送入左右速度环 */
    pid_speed_update(&PID.left_speed, left_target, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, right_target, PID.right_speed.speed);

    if (a_run_mode_get_start_state() == 2)
    {
        left_pwm = motor_apply_speed_deadzone_comp(
            (int32)PID.left_speed.output,
            left_target,
            PID.left_speed.speed,
            MAIN_LEFT_DEADZONE_PWM);
        right_pwm = motor_apply_speed_deadzone_comp(
            (int32)PID.right_speed.output,
            right_target,
            PID.right_speed.speed,
            MAIN_RIGHT_DEADZONE_PWM);
        motor_output(left_pwm, right_pwm);
    }
}

void run_test_angle(void)
{
    a_run_apply_iap_guard();
    test_angle_func();
    fuya_update_simple();
}

void run_test_motor(int speed_l, int speed_r)
{
    a_run_apply_iap_guard();
    /* 1. 读取编码器速度反馈 */
    Encoder_get(&PID.left_speed, &PID.right_speed);
    motor_output(speed_l, speed_r);
}

/**
 * @brief IAP 保护处理
 * @details 当 P32 被拉低时，写入 STC 约定值，确保 ISP/IAP 控制状态正确切换
 */
void a_run_apply_iap_guard(void)
{
    if (!P32)
    {
        IAP_CONTR = 0x60; /* STC 约定值：允许切换到 ISP/IAP 控制模式 */
    }
}

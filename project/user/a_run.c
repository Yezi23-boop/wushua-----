/**
 * @file a_run.c
 * @brief 主控制任务调度与执行入口
 * @details
 * 本文件承接定时中断任务链，将“采样-解算-控制输出”组织为固定周期流程：
 * - run_time_1(): 高频控制环，负责电感误差环/双轮速度环并级控制与电机输出
 * - run_time_2(): 低频状态环，负责保护检测、状态机与软定时器
 * - run_time_3(): 差速实验链路，用于算法验证
 *
 * 该模块位于实时主链路，注释强调调用时序和数据依赖，便于赛道现场快速排障。
 */
#include "zf_common_headfile.h"

/* --- 运行状态变量 --- */
volatile float left_target = 0.0f;  /* 当前左轮目标速度（用于菜单/调试显示） */
volatile float right_target = 0.0f; /* 当前右轮目标速度（用于菜单/调试显示） */

/* --- 周期任务内部变量 --- */
static int steer_div_10 = 0;        /* 2ms 主环分频：每 3 拍约 6ms 更新一次转向环 */
static float speed_active = 0.0f;   /* 当前参与速度环计算的目标速度，保留 speed_run 的小数调参精度。 */
static uint8 ring_angle_active = 0; /* 圆环角速度环活动标志，用于进入时抑制微分突跳。 */
/**
 * @brief 主控制核心任务 (运行于 TM0 2ms 中断)
 * @details 串行执行传感器采集 -> 姿态获取 -> 双轮速度闭环 -> 误差差速叠加 -> 电机执行链路。
 * 必须始终保证函数总体耗时远小于 2ms 的中断周期，且严禁加入任何可能阻塞的任务（如 printf、延迟函数），
 * 任何超时都会导致电机脱管、失控。
 */
void run_time_1(void)
{
    float steer_pwm;
    float ring_angle_target;
    float gyro_feedback;
    float left_pwm;
    float right_pwm;
    int8 start_state;
    CylinderState cylinder_state;
    steer_div_10++;
    a_run_apply_iap_guard();
    start_state = a_run_mode_get_start_state();
    read_AD();                                      /* 1) 传感器采样：获取归一化位置信息及赛道丢失警告。由于是在中断中调用，禁止内嵌耗时过长的排序运算 */
    Encoder_get(&PID.left_speed, &PID.right_speed); /* 读取左右轮编码器速度 */
    imu_update_gyro_z_from_imu660rc();
    if (steer_div_10 >= 3)
    {
        /* 圆桶窗口确认后才切专用方向环，避免元素序列轮到圆桶时起步提前降增益。 */
        cylinder_state = a_run_cylinder_get_state();
        if (cylinder_state == CYLINDER_STATE_DECEL)
        {
            PID.steer.Kp = app.cylinder.kp_Err;
            PID.steer.Kd = app.cylinder.kd_Err;
        }
        else
        {
            PID.steer.Kp = app.speed.kp_Err;
            PID.steer.Kd = app.speed.kd_Err;
        }
        /* 误差环根据电感偏差生成最终 PWM 差速修正量。 */
        pid_steer_update(&PID.steer, Err, 0.0f);
        steer_div_10 = 0;
    }
    speed_active = app.speed.speed_run;
    steer_pwm = PID.steer.output;
    ring_angle_target = 0.0f;
    /*
     * 元素仲裁跟随 2ms 采样链路，并放在误差环之后执行。
     * 普通路段保留误差环差速，圆环固定阶段只覆盖本拍目标角速度。
     */
    a_run_track_element_update_gate(&speed_active, &ring_angle_target);
    if (ring_angle_target != 0.0f)
    {
        gyro_feedback = gyro_z * app.angle.gyro_feedback_scale;
        if (ring_angle_active == 0)
        {
            /* 首拍预置微分历史，只保留比例响应，避免圆环接管时 D 项突跳。 */
            PID.angle.prev_error = ring_angle_target - gyro_feedback;
            ring_angle_active = 1;
        }
        pid_angle_update(&PID.angle, ring_angle_target, gyro_feedback);
        steer_pwm = PID.angle.output;
    }
    else
    {
        ring_angle_active = 0;
        PID.angle.error = 0.0f;
        PID.angle.prev_error = 0.0f;
        PID.angle.output = 0.0f;
    }
    if (seesaw_zero_brake_active != 0)
    {
        /*
         * 跷跷板停止等待前零速闭环刹车。
         * 此阶段使用 signed 编码器速度，前滑给反向力矩，倒滑则自动收回到正向。
         */
        left_target = 0.0f;
        right_target = 0.0f;
        pid_speed_update(&PID.left_speed, left_target, speed_l_signed);
        pid_speed_update(&PID.right_speed, right_target, speed_r_signed);
        if (start_state == 2)
        {
            motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
        }
        else
        {
            motor_output(0, 0);
        }
        return;
    }
    left_target = speed_active;
    right_target = speed_active;

    /* 左右速度环各自维持基础速度，允许其与最终叠加的转向差速互相作用。 */
    pid_speed_update(&PID.left_speed, left_target, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, right_target, PID.right_speed.speed);

    /* 误差环直接叠加到最终 PWM：正输出左轮减、右轮加。 */
    left_pwm = PID.left_speed.output - steer_pwm;
    right_pwm = PID.right_speed.output + steer_pwm;

    /* 7. 仅在运行态时允许电机输出 */
    if (start_state == 2)
    {
        motor_output((int32)left_pwm, (int32)right_pwm);
    }
    else
    {
        motor_output(0, 0);
    }
}

/**
 * @brief 10ms 状态管理任务 (运行于 TM1 中断)
 * @details 完成赛道检测、大周期系统状态机更新、异常保护与软件定时器。
 * 此时序对实时性要求稍低，但仍需避免长延时阻塞操作影响主控制环中断。
 */
void run_time_2(void)
{
    int8 start_state;

    /* 1. 更新电感动态最大值，用于归一化与标定 */
    scan_track_max_value();
    a_run_mode_update_start_state(); /* 按键状态机，每 10ms 刷新一次 */
    start_state = a_run_mode_get_start_state();
    /* 2. 执行各类保护检测 */
    lost_lines();    /* 丢线保护 */
    dianya_jiance(); /* 电池电压检测 */
    motor_stall_check_10ms();
    /* 3. 更新启停状态与负压控制 */
    if (start_state == 1)
    {
        fuya_set_percent(app.start.fuya_xili); /* 预启动和运行态都周期刷新固定负压。 */
    }
    /* 4. 更新软件定时器 */
    soft_timer_update_10ms();
}

/**
 * @brief 差速实验任务
 * @details 使用电感偏差直接生成左右轮目标速度，并通过速度环完成闭环输出
 */
void run_time_3(void)
{
    float diff_output;

    /* 试验链路同样遵循主链路顺序，便于与正式控制策略对比 */
    a_run_apply_iap_guard();
    imu_update_gyro_z_from_imu660rc();
    read_AD();
    Encoder_get(&PID.left_speed, &PID.right_speed);

    /* 更新转向差速输出 */
    pid_steer_update(&PID.steer, Err, gyro_z);
    diff_output = PID.steer.output;
    left_target = app.speed.speed_run - diff_output;
    right_target = app.speed.speed_run + diff_output;

    /* 将目标轮速送入左右速度环 */
    pid_speed_update(&PID.left_speed, left_target, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, right_target, PID.right_speed.speed);

    if (a_run_mode_get_start_state() == 2)
    {
        motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
    }
    else
    {
        motor_output(0, 0);
    }
}

void run_test_diff(void)
{
    /* 仅用于实验调试：不参与常规竞速主链路 */
    a_run_apply_iap_guard();
    test_diff_func();
}

/**
 * @brief IAP 下载保护处理
 * @details 监听 P32 引脚状态，拉低时将触发复位到系统 ISP 监控区，必须极低耗时。
 */
void a_run_apply_iap_guard(void)
{
    if (!P32)
    {
        IAP_CONTR = 0x60; /* STC 约定值：允许切换到 ISP/IAP 控制模式 */
    }
}

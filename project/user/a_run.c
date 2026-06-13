/**
 * @file a_run.c
 * @brief 主控制任务调度与执行入口
 * @details
 * 本文件承接定时中断任务链，将“采样-解算-控制输出”组织为固定周期流程：
 * - run_time_1(): 高频控制环，负责电感/方向差速/双速度闭环与电机输出
 * - run_time_2(): 低频状态环，负责保护检测、状态机与软定时器
 * - run_time_3(): 差速实验链路，用于算法验证
 *
 * 该模块位于实时主链路，注释强调调用时序和数据依赖，便于赛道现场快速排障。
 */
#include "zf_common_headfile.h"

/* --- 运行状态变量 --- */
volatile int flat_fly = 0;          /* 飞坡阶段状态：0-普通巡线，1-保持，2-恢复，3-冷却 */
volatile float left_target = 0.0f;  /* 当前左轮目标速度（用于菜单/调试显示） */
volatile float right_target = 0.0f; /* 当前右轮目标速度（用于菜单/调试显示） */

/* --- 周期任务内部变量 --- */
static int steer_div_10 = 0;        /* 2ms 主环分频：每 3 拍约 6ms 更新一次转向环 */
static float speed_active = 0.0f;   /* 当前参与速度环计算的目标速度，保留 speed_run 的小数调参精度。 */

/**
 * @brief 主控制核心任务 (运行于 TM0 2ms 中断)
 * @details 串行执行传感器采集 -> 姿态获取 -> 方向差速 -> 双速度闭环 -> 电机执行链路。
 * 必须始终保证函数总体耗时远小于 2ms 的中断周期，且严禁加入任何可能阻塞的任务（如 printf、延迟函数），
 * 任何超时都会导致电机脱管、失控。
 */
void run_time_1(void)
{
    float steer_output;
    float left_pwm;
    float right_pwm;
    int8 start_state;
    steer_div_10++;
    a_run_apply_iap_guard();
    start_state = a_run_mode_get_start_state();
    read_AD();                                      /* 1) 传感器采样：获取归一化位置信息及赛道丢失警告。由于是在中断中调用，禁止内嵌耗时过长的排序运算 */
    Encoder_get(&PID.left_speed, &PID.right_speed); /* 读取左右轮编码器速度 */
    imu_update_gyro_z_from_imu660rc(); /* 圆环仍使用 gyro_z 积分判定阶段，主控不再做角速度内环。 */
    if (steer_div_10 >= 3)
    {
        /* 方向环直接生成最终差速 PWM 修正量，左轮减、右轮加为正方向。 */
        pid_steer_update(&PID.steer, Err, 0.0f);
        steer_div_10 = 0;
    }
    speed_active = app.speed.speed_run;
    /*
     * 元素仲裁跟随 2ms 采样链路，并放在转向外环之后执行。
     * 原因：跷跷板可能覆盖速度，圆环可能覆盖方向差速，必须压住普通循迹目标。
     */
    a_run_track_element_update_gate(&speed_active, &PID.steer.output);
    left_target = speed_active;
    right_target = speed_active;

    /* 左右速度环保持同一基础速度目标，方向环只在最终 PWM 层叠加差速。 */
    pid_speed_update(&PID.left_speed, left_target, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, right_target, PID.right_speed.speed);
    steer_output = PID.steer.output;
    /*
     * 最终 PWM 前向限幅：方向差速不得大于当前基础推力。
     * 这样内侧轮最低降到 0，不反转，避免高速循迹时反拖、打滑和速度环互相打架。
     */
    if (steer_output > PID.left_speed.output)
    {
        steer_output = PID.left_speed.output;
    }
    else if (steer_output < -PID.right_speed.output)
    {
        steer_output = -PID.right_speed.output;
    }
    left_pwm = PID.left_speed.output - steer_output;
    right_pwm = PID.right_speed.output + steer_output;

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

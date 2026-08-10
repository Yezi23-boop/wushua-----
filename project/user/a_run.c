/**
 * @file a_run.c
 * @brief 主控制任务调度与执行入口
 * @details
 * 本文件承接定时中断任务链，将“采样-解算-控制输出”组织为固定周期流程：
 * - run_time_1(): 高频控制环，负责电感/转向差速/速度闭环与电机输出
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
static int steer_div_10 = 0;      /* 2ms 主环分频：每 3 拍约 6ms 更新一次转向环 */
static float speed_active = 0.0f; /* 当前参与速度环计算的目标速度，保留 speed_run 的小数调参精度。 */
/**
 * @brief 主控制核心任务 (运行于 TM0 2ms 中断)
 * @details 串行执行传感器采集 -> 姿态获取 -> 转向偏差融合 -> 速度设定 -> 电机执行链路。
 * 必须始终保证函数总体耗时远小于 2ms 的中断周期，且严禁加入任何可能阻塞的任务（如 printf、延迟函数），
 * 任何超时都会导致电机脱管、失控。
 */
void run_time_1(void)
{
    int8 start_state;
    CylinderState cylinder_state;
    float diff_inner_gain;
    float diff_outer_gain;
    steer_div_10++;
    a_run_apply_iap_guard();
    start_state = a_run_mode_get_start_state();
    read_AD();                                      /* 1) 传感器采样：获取归一化位置信息及赛道丢失警告。由于是在中断中调用，禁止内嵌耗时过长的排序运算 */
    Encoder_get(&PID.left_speed, &PID.right_speed); /* 读取左右轮编码器速度 */
    imu_update_gyro_z_from_imu660rc();
    if (steer_div_10 >= 3)
    {
        /* 每次方向环更新都先恢复全局参数，使菜单中的Kp2修改可以立即生效。 */
        PID.steer.Kp = app.speed.kp_Err;
        PID.steer.Kd = app.speed.kd_Err;
        PID.steer.Kp2 = app.speed.kp2_Err;
        cylinder_state = a_run_cylinder_get_state();
        if (cylinder_state == CYLINDER_STATE_DECEL)
        {
            /* 圆桶减速阶段保持最高优先级，沿用圆桶专用Kp/Kd和全局Kp2。 */
            PID.steer.Kp = app.cylinder.kp_Err;
            PID.steer.Kd = app.cylinder.kd_Err;
        }
        else
        {
            a_run_ring_apply_steer_params(&PID.steer.Kp,
                                          &PID.steer.Kd,
                                          &PID.steer.Kp2);
        }
        if (a_run_cross_get_state() == CROSS_STATE_TIMING)
        {
            /* 双十字确认后使用独立转向环参数，离开TIMING自动恢复全局值。 */
            PID.steer.Kp = app.cross.kp_Err;
            PID.steer.Kd = app.cross.kd_Err;
            PID.steer.Kp2 = app.cross.kp2_Err;
        }
        if (a_run_cross_single_get_state() == CROSS_SINGLE_STATE_TIMING)
        {
            /* 单十字确认后使用独立转向环参数，离开TIMING自动恢复全局值。 */
            PID.steer.Kp = app.cross_single.kp_Err;
            PID.steer.Kd = app.cross_single.kd_Err;
            PID.steer.Kp2 = app.cross_single.kp2_Err;
        }
        /* 方向外环根据电感偏差生成差速目标，后续再结合 gyro 阻尼输出最终差速。 */
        if (adc_strong_signal != 0)
        {
            int8 dir_vote;

            /*
             * 方向由最近三次有效解算Err符号多数表决：入口附近样本已带十字拖拽趋势，
             * 表决可防单拍噪声把修正方向打反（如++-取+的反即-）；
             * 幅度带符号可现场反向，票数为0视为居中不给修正量。
             */
            dir_vote = (int8)(adc_err_sign_hist[0] + adc_err_sign_hist[1] +
                              adc_err_sign_hist[2]);
            if (dir_vote > 0)
                PID.steer.output = -app.angle.strong_correct_angle;
            else if (dir_vote < 0)
                PID.steer.output = app.angle.strong_correct_angle;
            else
                PID.steer.output = 0.0f;
        }
        else
        {
            pid_steer_update(&PID.steer, Err, 0.0f);
        }
        steer_div_10 = 0;
    }
    speed_active = app.speed.speed_run;
    /*
     * 元素仲裁跟随 2ms 采样链路，并放在转向外环之后执行。
     * 原因：跷跷板和圆环都可能覆盖 PID.steer.output，必须压住普通循迹目标。
     */
    a_run_track_element_update_gate(&speed_active, &PID.steer.output);
    if (seesaw_zero_brake_active != 0)
    {
        /*
         * 跷跷板停止等待前零速闭环刹车。
         * 此阶段使用 signed 编码器速度，前滑给反向力矩，倒滑则自动收回到正向。
         */
        PID.angle.output = 0.0f;
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
    PID.angle.Kp = app.angle.kp_Angle;
    PID.angle.Kd = app.angle.kd_Angle;
    diff_inner_gain = app.speed.diff_inner_gain;
    diff_outer_gain = app.speed.diff_outer_gain;
    a_run_ring_apply_angle_diff_params(&PID.angle.Kp,
                                       &PID.angle.Kd,
                                       &diff_inner_gain,
                                       &diff_outer_gain);
    pid_angle_update(&PID.angle, PID.steer.output, gyro_z * app.angle.gyro_feedback_scale);
    if (app.speed.diff_enable != 0)
    {
        Pid_Differential(speed_active, PID.angle.output,
                         &left_target, &right_target,
                         app.angle.limiting_Angle,
                         diff_inner_gain, diff_outer_gain);
    }
    else
    {
        left_target = speed_active - PID.angle.output;
        right_target = speed_active + PID.angle.output;
    }

    /* 速度环保持高频更新，保证电机执行链路带宽 */
    pid_speed_update(&PID.left_speed, left_target, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, right_target, PID.right_speed.speed);

    /* 7. 仅在运行态时允许电机输出 */
    if (start_state == 2)
    {
        motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
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

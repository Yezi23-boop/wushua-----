/**
 * @file a_run.c
 * @brief 主控制任务调度与执行入口
 * @details
 * 本文件承接定时中断任务链，将“采样-解算-控制输出”组织为固定周期流程：
 * - run_time_1(): 高频控制环，负责电感/姿态/速度闭环与电机输出
 * - run_time_2(): 低频状态环，负责保护检测、状态机与软定时器
 * - run_time_3(): 纯追踪实验链路，用于算法验证
 *
 * 该模块位于实时主链路，注释强调调用时序和数据依赖，便于赛道现场快速排障。
 */
#include "zf_common_headfile.h"
#include "a_run.h"
#include "a_run_mode.h"
#include "../service/soft_timer.h"

/* --- 运行状态变量 --- */
volatile int flat_statr = 0;            /* 运行状态镜像：0-停止，1-预启动，2-运行中，3-外部强制启动请求 */
volatile int flat_fly = 0;              /* 飞坡状态标志位 */
volatile float run_left_target = 0.0f;  /* 当前左轮目标速度（用于菜单/调试显示） */
volatile float run_right_target = 0.0f; /* 当前右轮目标速度（用于菜单/调试显示） */

/* --- 周期任务内部变量 --- */
static int steer_div_4 = 0;  /* 2ms 主环分频：用于每 4ms 更新一次转向环 */
static int steer_div_8 = 0;  /* 2ms 主环分频：用于每 16ms 触发电感采样与转向更新 */
static int speed_active = 0; /* 当前参与速度环计算的目标速度 */

/**
 * @brief 2ms 主控制任务
 * @details 按“采样 -> 解算 -> PID -> 输出”的顺序完成一轮核心控制
 */
void run_time_1(void)
{
    float left_target = run_left_target;
    float right_target = run_right_target;
    int32 left_pwm;
    int32 right_pwm;
    /* 调试脚位翻转预留（默认关闭）：可用于示波器测量环路耗时 */
    /* P36 = 0; */
    /* 1. 执行 IAP 保护，避免复位脚异常时进入错误状态 */
    a_run_apply_iap_guard();

    /* 3. 采集编码器数据 */
    Encoder_get(&PID.left_speed, &PID.right_speed); /* 读取左右轮编码器速度 */

    /* 4. 转向环按 4ms 更新一次，角速度环和速度环继续保持 2ms */
    steer_div_4++;
    steer_div_8++;
    if (steer_div_8 >= 4)
    {
        fuya_set_percent((uint8)app.start.fuya_xili); /* 运行态全力负压，其他状态关闭负压 */
        /* 4.1 低频电感链路：读 ADC 并更新赛道偏差 */
        read_AD();                         /* 采集四路电感 ADC */
        pid_steer_update(&PID.steer, Err); /* 根据赛道偏差更新转向环 */
        steer_div_8 = 0;
    }
    if (steer_div_4 >= 2)
    {
        /* 5. 根据飞坡/赛道状态修正当前目标速度 */
        a_run_mode_update_fly_speed(&speed_active);
        /* 2. 将四元数中断已缓存的原始陀螺仪 Z 轴转换为控制环使用量 */
        imu_update_gyro_z_from_imu660rc();
        /* 6. 串联角度环与速度环 */
        /* 转向反馈环使用校准后的 gyro_z */
        pid_angle_update(&PID.angle, PID.steer.output, gyro_z);
        // run_mode_update_angle_output(&PID.angle.output); /* 环岛阶段可覆盖角度环输出 */
        /* 左右轮速度环目标 = 基础速度 ± 姿态补偿 */
        /* 输出符号约定：左轮减姿态量，右轮加姿态量，实现差速转向 */
        left_target = (float)speed_active - PID.angle.output;
        right_target = (float)speed_active + PID.angle.output;
        run_left_target = left_target;
        run_right_target = right_target;
        steer_div_4 = 0;
    }

    /* 速度环保持高频更新，保证电机执行链路带宽 */
    pid_speed_update(&PID.left_speed, left_target, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, right_target, PID.right_speed.speed);

    /* 7. 仅在运行态时允许电机输出 */
    if (a_run_mode_get_start_state() == 2)
    {
        left_pwm = (int32)PID.left_speed.output;
        right_pwm = (int32)PID.right_speed.output;
        motor_output(left_pwm, right_pwm);
    }
    /* P36 = 1; */
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
    /* 同步对外状态镜像，供显示与外部逻辑读取 */
    flat_statr = a_run_mode_get_start_state(); /* 同步当前启停状态到对外变量 */
                                               //   a_run_mode_update_fuya_state();    /* 根据当前状态决定是否启用负压 */
                                               //  fuya_set_percent(30);                 /* 运行态全力负压，其他状态关闭负压 */
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

    /* 试验链路同样遵循主链路顺序，便于与正式控制策略对比 */
    a_run_apply_iap_guard();
    imu_update_gyro_z_from_imu660rc();
    read_AD();
    Encoder_get(&PID.left_speed, &PID.right_speed);

    /* 更新转向环 PD 输出 */
    pid_steer_update(&PID.steer, Err);

    /* 由纯追踪算法生成左右轮目标速度 */
    {
        float norm_err = Err / 100.0f; /* 将偏差缩放到算法所需量级 */
        /* 该实验路径同样使用校准后的 gyro_z */
        Pure_Pursuit_Gyro_Control(app.speed.speed_run, norm_err, gyro_z, &left_target, &right_target);
        run_left_target = left_target;
        run_right_target = right_target;
    }

    /* 将目标轮速送入左右速度环 */
    pid_speed_update(&PID.left_speed, left_target, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, right_target, PID.right_speed.speed);

    if (a_run_mode_get_start_state() == 2)
    {
        left_pwm = (int32)PID.left_speed.output;
        right_pwm = (int32)PID.right_speed.output;
        motor_output(left_pwm, right_pwm);
    }
}

void run_test_angle(void)
{
    /* 仅用于实验调试：不参与常规竞速主链路 */
    a_run_apply_iap_guard();
    test_angle_func();
    fuya_update_simple();
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

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
#include "a_run.h"
#include "a_run_mode.h"
#include "../service/soft_timer.h"

/* --- 运行状态变量 --- */
volatile int flat_statr = 0;            /* 运行状态镜像：0-停止，1-预启动，2-运行中，3-外部强制启动请求 */
volatile int flat_fly = 0;              /* 飞坡状态标志位 */
volatile float left_target = 0.0f;      /* 当前左轮目标速度（用于菜单/调试显示） */
volatile float right_target = 0.0f;     /* 当前右轮目标速度（用于菜单/调试显示） */

/* --- 周期任务内部变量 --- */
static int steer_div_10 = 0; /* 2ms 主环分频：用于每 4ms 更新一次转向环 */
static int speed_active = 0; /* 当前参与速度环计算的目标速度 */

/**
 * @brief 2ms 主控制任务
 * @details 按“采样 -> 解算 -> PID -> 输出”的顺序完成一轮核心控制
 */
void run_time_1(void)
{
    float diff_output;
    steer_div_10++;
    /* P36 = 0; */
    a_run_apply_iap_guard();
    read_AD();                                      /* 采集四路电感 ADC */
    Encoder_get(&PID.left_speed, &PID.right_speed); /* 读取左右轮编码器速度 */
    if (steer_div_10 > 2)
    {
        pid_steer_update(&PID.steer, Err, gyro_z); /* 根据赛道偏差和 gyro 阻尼更新转向环 */
    }
    speed_active = (int)app.speed.speed_run;
    imu_update_gyro_z_from_imu660rc();
    gyro_integrals();
    diff_output = PID.steer.output;
    left_target = (float)speed_active - diff_output;
    right_target = (float)speed_active + diff_output;

    /* 速度环保持高频更新，保证电机执行链路带宽 */
    pid_speed_update(&PID.left_speed, left_target, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, right_target, PID.right_speed.speed);

    /* 7. 仅在运行态时允许电机输出 */
    if (a_run_mode_get_start_state() == 2)
    {
        motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
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
    circle_check_r();

    /* 2. 执行各类保护检测 */
    lost_lines();    /* 丢线保护 */
    dianya_jiance(); /* 电池电压检测 */
    /* 3. 更新启停状态与负压控制 */
    a_run_mode_update_start_state(); /* 按键/外部命令状态机，每 10ms 刷新一次 */
    /* 同步对外状态镜像，供显示与外部逻辑读取 */
    flat_statr = a_run_mode_get_start_state(); /* 同步当前启停状态到对外变量 */
                                               //   a_run_mode_update_fuya_state();    /* 根据当前状态决定是否启用负压 */
	if(a_run_mode_get_start_state()==1)
	{
      fuya_set_percent(30);                 /* 运行态全力负压，其他状态关闭负压 */
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
}

void run_test_diff(void)
{
    /* 仅用于实验调试：不参与常规竞速主链路 */
    a_run_apply_iap_guard();
    test_diff_func();
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

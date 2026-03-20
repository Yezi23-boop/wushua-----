#include "zf_common_headfile.h"
#include "a_run.h"
#include "a_run_mode.h"
#include "../service/soft_timer.h"

/* --- 全局状态变量 --- */
volatile int flat_statr = 0; /* 运行状态机：0-待机，1-已准备，2-正在运行 */
volatile int flat_fly = 0;   /* 飞坡/特殊元素标志位 */

/* --- 内部私有变量 --- */
static int time_1 = 0;       /* 分频计数器，用于在 5ms 任务中分出 10ms 逻辑 */
static int speed_active = 0; /* 当前期望执行的物理速度 */

/* 内部私有函数声明 */
// 已在 a_run.h 中声明为全局函数
// static void a_run_apply_iap_guard(void);

/**
 * @brief 5ms 周期核心任务
 * @details 负责最高实时性的控制链路：采样 -> 滤波 -> PID -> 输出
 */
void run_time_1(void)
{
    /* 1. 安全保护检查（如按键触发强制复位下载） */
    a_run_apply_iap_guard();

    /* 2. 传感器数据获取 */
    read_AD();                                      /* 读取并处理电感 ADC */
    Prepare_Data();                                 /* 读取 IMU 原始数据并预处理 */
    Encoder_get(&PID.left_speed, &PID.right_speed); /* 获取左右编码器速度 */

    /* 3. 分段执行转向 PID (此处 10ms 更新一次转向环) */
    time_1++;
    if (time_1 >= 2)
    {
        pid_steer_update(&PID.steer, Err); /* 更新基于电感偏差的转向环 */
        time_1 = 0;
    }

    /* 4. 特殊元素速度/方向策略更新 (如飞坡慢速处理) */
    a_run_mode_update_fly_speed(&speed_active);

    /* 5. 串级 PID 控制 */
    /* Steering feedback loop uses calibrated gyro_z */
    pid_angle_update(&PID.angle, PID.steer.output, gyro_z);
    run_mode_update_angle_output(&PID.angle.output); /* 根据环岛状态机可能覆盖转向输出 */
    /* 速度环：基础速度叠加/删减角度环的差速调节量 */
    pid_speed_update(&PID.left_speed, (float)speed_active - PID.angle.output, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, (float)speed_active + PID.angle.output, PID.right_speed.speed);

    /* 6. 执行电机物理输出 */
    if (a_run_mode_get_start_state() == 2)
    {
        motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
    }
}

/**
 * @brief 10ms 周期管理任务
 * @details 负责姿态解算、保护逻辑及低频状态更新
 */
void run_time_2(void)
{
    /* 1. 自动标定更新（更新电感最大最小值记录） */
    scan_track_max_value();

    /* 2. 系统保护逻辑 */
    lost_lines();    /* 丢线停车保护 */
    dianya_jiance(); /* 电池欠压保护 */

    /* 3. 姿态解算更新（Mahony 算法） */
    IMUupdate(&Gyr_filt, &Acc_filt, &Att_Angle);

    /* 4. 运行模式管理 */
    a_run_mode_update_start_state(); /* 按键启动逻辑，10ms定时推进状态机 */
    // 更新全局运行标志
    flat_statr = a_run_mode_get_start_state();
    a_run_mode_update_fuya_state(); /* 负压吸附状态更新 */

    /* 5. 软件定时器后台更新 */
    soft_timer_update_10ms();
}

/**
 * @brief 高级算法任务 (Pure Pursuit + Gyro Loop)
 * @details 备用方案：采用纯追踪几何模型替代传统 PD 转向
 */
void run_time_3(void)
{
    float left_target = 0.0f;
    float right_target = 0.0f;

    a_run_apply_iap_guard();
    read_AD();
    Prepare_Data();
    Encoder_get(&PID.left_speed, &PID.right_speed);

    /* 执行基础转向 PD 以获得稳定趋势 */
    pid_steer_update(&PID.steer, Err);

    /* 执行纯追踪融合算法 */
    {
        float norm_err = Err / 100.0f; /* 归一化偏差 */
        /* Keep using calibrated gyro_z in this experimental path */
        Pure_Pursuit_Gyro_Control(app.speed.speed_run, norm_err, gyro_z, &left_target, &right_target);
    }

    /* 更新底层速度环并输出 */
    pid_speed_update(&PID.left_speed, left_target, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, right_target, PID.right_speed.speed);

    if (a_run_mode_get_start_state() == 2)
    {
        motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
    }
}
/* --- 顶层测试包装函数 --- */

void run_test_speed(void)
{
    a_run_apply_iap_guard();
    test_speed_func();
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
    /* 1. 准备姿态数据（不依赖此数据可删） */
    Prepare_Data();
    /* 2. 获取左右编码器速度 */
    Encoder_get(&PID.left_speed, &PID.right_speed);
    motor_output(speed_l, speed_r);
}
/**
 * @brief 硬件复位守卫
 * @details 检测 P32 引脚（通常连接物理按键），若按下则强制进入 IAP 下载模式
 */
void a_run_apply_iap_guard(void)
{
    if (!P32)
    {
        IAP_CONTR = 0x60; /* STC 强制复位到 ISP 监控区指令 */
    }
}

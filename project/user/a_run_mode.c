#include "zf_common_headfile.h"
#include "a_run_mode.h"

/* --- 内部私有变量 --- */
static int flat_statr_date = 0; /* 启动按键计时消抖 */
static int count_fly_1 = 0;     /* 飞坡触发检测计数 */
static int count_fly_2 = 0;     /* 飞坡持续时间计数 */

/**
 * @brief 启动逻辑维护
 * @details 检测 P35 引脚状态，长按或延时后切换 flat_statr 状态
 */
void a_run_mode_update_start_state(void)
{
    flat_statr_date++;

    /*
     * 若 P35 按键按下 (0) 且持续了一段时间
     * flat_statr 用于控制电机使能 (>=2 为运行)
     */
    if (P35 == 0 && flat_statr_date > 50)
    {
        flat_statr++;
        flat_statr_date = 0;
    }
}

/**
 * @brief 负压执行条件检查
 * @details 只有在 flat_statr 激活且配置使能时，才调用底层负压更新逻辑
 */
void a_run_mode_update_fuya_state(void)
{
    if (flat_statr >= 1 && g_app_config.start.start_flag == 1)
    {
        fuya_update_simple();
    }
}

/**
 * @brief 飞坡/特殊慢速区域处理逻辑
 * @details
 * 1. 检测四路电感是否符合“全丢”或“微弱”特征判定飞坡
 * 2. 触发后锁定一个慢速值，并给定固定的转向输出
 * 3. 经过一段计时后恢复正常循迹
 */
void a_run_mode_update_fly_speed(int *speed)
{
    // /* A. 飞坡进入判定：电感值均小于特定阈值，且飞坡模式开关开启 */
    // if (g_app_config.fly.fly_ramp_enable == 1 && ad1 < 40 && ad2 < 15 && ad3 < 15 && ad4 < 40 && flat_fly == 0)
    // {
    //     count_fly_1++;
    //     if (count_fly_1 >= g_app_config.fly.count_fly_time_1)
    //     {
    //         count_fly_1 = 0;
    //         flat_fly = 1; /* 触发飞坡模式 */
    //     }
    // }

    /* B. 飞坡模式下的策略执行 */
    if (flat_fly == 1)
    {
        /* 使用配置中的慢速值和预设角度 */
        *speed = g_app_config.fly.count_fly_speed;
        PID.steer.output = (float)g_app_config.fly.count_fly_angle;

        count_fly_2++;
        /* 计时结束，退出飞坡模式 */
        if (count_fly_2 >= g_app_config.fly.count_fly_time_2)
        {
            count_fly_2 = 0;
            flat_fly = 0;
        }
    }
    else
    {
        /* C. 正常循迹速度 */
        *speed = (int)g_app_config.speed.speed_run;
    }
}

/**
 * @file a_run_mode.c
 * @brief 运行模式统一调配层
 * @details
 * 本模块保留启停和负压状态更新，并统一转发飞坡、环岛和圆桶接口。
 * 外部调用点继续只依赖 a_run_mode.h，便于现场调试时从一个入口追踪运行阶段。
 */
#include "zf_common_headfile.h"

/* --- 启停状态机参数 --- */
#define START_DEBOUNCE_TIME 5 /* 启动按键消抖确认次数（10ms 调用周期下约 500ms） */

enum StartState
{
    START_STATE_0 = 0, // 停止/待机
    START_STATE_1 = 1, // 预启动
    START_STATE_2 = 2  // 允许运行
};

static enum StartState current_start_state = START_STATE_0; /**< 当前启动状态，由 `a_run_mode_update_start_state` 写入，外部只读。 */
static int press_debounce_cnt = 0;                          /**< 按下消抖计数，仅在 `a_run_mode_update_start_state`（10ms 上下文）中递增。 */
static int8 key_released = 1;                               /**< 按键释放锁存：1-已释放等待下一次按下，0-仍在按下期间，防止重复触发状态切换。 */

/**
 * @brief 启动状态机更新
 * @details 10ms 调用一次，检测 P36 启动按键或外部命令，在停止、预启动、运行之间切换
 */
void a_run_mode_update_start_state(void)
{
    /* 外部命令可直接请求进入运行态，flat_statr == 3 为一次性触发 */
    if (flat_statr == 3)
    {
        current_start_state = START_STATE_2;
        flat_statr = 2; // 同步状态镜像为运行中
        return;
    }
    else if (flat_statr == 0 && current_start_state != START_STATE_0)
    {
        // 外部命令要求停止时，直接回到停止态
        current_start_state = START_STATE_0;
        return;
    }

    /* 检测按键按下（低电平有效） */
    if (P43 == 0)
    {
        if (key_released == 1) // 仅在本次按下的首次稳定阶段计数
        {
            /* 仅在按键保持按下期间递增，形成时间窗消抖 */
            press_debounce_cnt++;
            if (press_debounce_cnt >= START_DEBOUNCE_TIME)
            {
                /* 消抖通过后切换到下一个有效状态 */
                if (current_start_state == START_STATE_0 || current_start_state == START_STATE_2)
                {
                    current_start_state = START_STATE_1;
                }
                else if (current_start_state == START_STATE_1)
                {
                    current_start_state = START_STATE_2;
                }

                /* 原因：不重复触发按下的去抖。防止用户在物理抖动瞬间或持续按下时系统在连续多个状态间极速切换。 */
                key_released = 0;
                press_debounce_cnt = 0;
            }
        }
    }
    /* 按键释放（高电平） */
    else
    {
        press_debounce_cnt = 0;
        key_released = 1; // 解除锁存，等待下一次按下
    }
}

/**
 * @brief 读取当前启动状态
 * @return int8 当前状态值：0-停止，1-预启动，2-运行中
 */
int8 a_run_mode_get_start_state(void)
{
    return (int8)current_start_state;
}

/**
 * @brief 负压状态更新
 * @details 启动状态有效且配置允许时才更新负压，避免待机时误动作
 */
void a_run_mode_update_fuya_state(void)
{
    if (a_run_mode_get_start_state() >= 1 && app.start.start_flag == 1)
    {
        fuya_update_simple();
    }
    else
    {
        fuya_force_stop();
    }
}

/**
 * @brief 飞坡速度修正
 * @details 保持旧外部入口不变，实际状态机由 a_run_fly 模块维护。
 * @param speed 输出的目标速度指针
 */
void a_run_mode_update_fly_speed(int *speed)
{
    a_run_fly_update_speed(speed);
}

/**
 * @brief 更新赛道元素仲裁状态机
 * @details 保持旧外部入口不变，实际环岛/圆桶状态机由 a_run_track_element 模块维护。
 */
void a_run_mode_update_track_element_gate(void)
{
    a_run_track_element_update_gate();
}

/**
 * @brief 根据环岛状态更新角速度目标
 * @param angle_target 指向目标角速度的指针
 */
void run_mode_update_angle_target(float *angle_target)
{
    a_run_track_element_update_angle_target(angle_target);
}

/**
 * @brief 读取当前环岛状态机阶段，用于菜单调参显示。
 * @return int8 阶段编号：0-no_ring，1-ring，2-pre_ring，3-in_ring，4-pre_out_ring，5-out_ring。
 */
int8 a_run_mode_get_ring_state(void)
{
    return a_run_track_element_get_ring_state();
}

/**
 * @brief 读取当前期望赛道元素。
 * @return int8 0-无，1-左圆环，2-右圆环，3-圆桶。
 */
int8 a_run_mode_get_expected_element(void)
{
    return a_run_track_element_get_expected_element();
}

/**
 * @brief 读取当前圆筒状态机阶段。
 * @return int8 0-空闲，1-等顶部，2-等回平，3-稳定延迟。
 */
int8 a_run_mode_get_cylinder_state(void)
{
    return a_run_track_element_get_cylinder_state();
}

/**
 * @brief 读取圆桶判断使用的重力向量 vz 滤波值。
 * @return float 滤波后的 vz，来自四元数解算。
 */
float a_run_mode_get_cylinder_vz(void)
{
    return a_run_track_element_get_cylinder_vz();
}

/**
 * @brief 更新环岛里程累计与偏航角增量。
 * @details 保持旧外部入口不变，实际累计逻辑由 a_run_track_element 模块维护。
 */
void gyro_integrals(void)
{
    a_run_track_element_update_integrals();
}

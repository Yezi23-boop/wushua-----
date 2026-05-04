/**
 * @file a_run_fly.c
 * @brief 飞坡状态机与速度覆盖逻辑
 * @details
 * 本模块只处理飞坡入口识别、离线保持和落地恢复。对外仍由 a_run_mode.c
 * 统一调配，避免主控制链路直接依赖飞坡内部状态。
 */
#include "zf_common_headfile.h"

/* --- 飞坡状态内部变量 --- */
static int fly_detect_count = 0; /* 飞坡入口连续弱磁确认计数 */
static int fly_state_count = 0;  /* 飞坡保持、恢复和冷却阶段的 5ms 计数 */

/* --- 飞坡状态机参数（run_time_1 以 5ms 调用） --- */
#define FLY_AD_SIDE_LOST_TH 14u    /* 横向电感低于该值时认为主线信号正在消失 */
#define FLY_AD_CENTER_LOST_TH 5u   /* 竖向电感阈值更低，避免普通弱弯误触发飞坡 */
#define FLY_HOLD_ANGLE 0           /* 飞坡离线阶段固定目标角速度，0 表示直行锁角 */
#define FLY_RAMP_BLOCK_ACC_Z 0.95f /* 低于该 acc_z 时认为车身已明显离开平面姿态，单位：g。 */
#define FLY_RECOVER_COUNT 2        /* 落地恢复窗口，单位 5ms，默认约 20ms */
#define FLY_COOLDOWN_COUNT 20      /* 退出冷却窗口，单位 5ms，默认约 100ms */

/**
 * @brief 判断车身是否已经明显离开平面姿态。
 *
 * 平地丢线时四路电感也可能同时很低，因此飞坡入口不能只依赖电感。
 * acc_z 低于阈值时，认为车身已经进入坡面或飞坡姿态。
 *
 * @return int8 1-姿态满足飞坡触发条件，0-仍近似平面。
 */
static int8 fly_is_acc_z_ramp_pose(void)
{
    float acc_z;

    if (imu660rc_transition_factor[0] <= 0.001f)
    {
        return 0;
    }

    acc_z = imu660rc_acc_transition(imu660rc_acc_z);
    if (acc_z < FLY_RAMP_BLOCK_ACC_Z)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 判断当前电感是否满足飞坡入口弱磁特征。
 *
 * 飞坡入口通常表现为四路归一化电感同时快速跌低，并伴随重力 Z 分量下降。
 * 姿态条件用于过滤平地丢线，避免把普通弱磁或赛道断线误判为飞坡。
 *
 * @return int8 1-满足飞坡入口特征，0-不满足。
 *
 * @note 由 5ms 主控制环调用，只做常量比较，避免增加实时链路负担。
 */
static int8 fly_is_ramp_lost_signal(void)
{
    if (ad1 < FLY_AD_SIDE_LOST_TH &&
        ad2 < FLY_AD_CENTER_LOST_TH &&
        ad3 < FLY_AD_CENTER_LOST_TH &&
        ad4 < FLY_AD_SIDE_LOST_TH &&
        fly_is_acc_z_ramp_pose())
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 判断飞坡落地恢复是否已回到中线附近。
 *
 * 中线在控制链路中对应 Err 为 0，同时用 ad1/ad2 差值约束电感平衡。
 * 原因是落地后单看 Err 可能受瞬态计算影响，双条件可以减少偏线误退出。
 *
 * @return int8 1-已接近中线，0-仍需继续低速回正。
 */
static int8 fly_is_center_line(void)
{
    if (func_abs((int)ad1 - (int)ad2) < 10 &&
        Err > -1.0f && Err < 1.0f)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 复位飞坡状态机内部计数并回到普通巡线。
 *
 * 关闭飞坡开关或现场调试强制退出时，需要同时清掉阶段计数，避免重新开启后
 * 沿用上一次弱磁窗口中的残留计数而误入飞坡。
 */
static void fly_reset_state(void)
{
    fly_detect_count = 0;
    fly_state_count = 0;
    flat_fly = FLY_STATE_IDLE;
}

/**
 * @brief 飞坡速度修正。
 *
 * 根据四路电感特征推进飞坡状态机，并在高风险阶段覆盖速度和转向输出。
 * 该函数由 a_run_mode 调配层在 5ms 主控制链路中转发调用。
 *
 * @param speed 输出的目标速度指针。
 */
void a_run_fly_update_speed(int *speed)
{
    if (app.fly.fly_ramp_enable != 1)
    {
        fly_reset_state();
        return;
    }

    switch (flat_fly)
    {
    case FLY_STATE_IDLE:
        if (fly_is_ramp_lost_signal())
        {
            fly_detect_count++;
            if (fly_detect_count >= app.fly.count_fly_time_1)
            {
                fly_detect_count = 0;
                fly_state_count = 0;
                flat_fly = FLY_STATE_HOLD;
            }
        }
        else
        {
            fly_detect_count = 0;
        }

        if (flat_fly != FLY_STATE_HOLD)
        {
            break;
        }
        /* 触发成立的同一控制周期立即锁角，避免飞坡入口多放行一个 5ms 周期。 */

    case FLY_STATE_HOLD:
        /* 离地/弱磁期间冻结外环目标，避免 Err 瞬态失真把车头拉偏。 */
        *speed = app.fly.count_fly_speed;
        PID.steer.output = (float)FLY_HOLD_ANGLE;

        fly_state_count++;
        if (fly_state_count >= app.fly.count_fly_time_2)
        {
            fly_state_count = 0;
            flat_fly = FLY_STATE_RECOVER;
        }
        break;

    case FLY_STATE_RECOVER:
        /*
         * 下地后第一件事是用飞坡低速回到中线。这里不再锁角，
         * 让电感外环按 Err 回正，直到有效线信号下 Err 接近 0。
         */
        *speed = app.fly.count_fly_speed;
        if (fly_is_center_line())
        {
            fly_state_count++;
            if (fly_state_count >= FLY_RECOVER_COUNT)
            {
                fly_state_count = 0;
                flat_fly = FLY_STATE_COOLDOWN;
            }
        }
        else
        {
            fly_state_count = 0;
        }
        break;

    case FLY_STATE_COOLDOWN:
        /* 冷却期只禁止重复触发，不覆盖控制输出，给普通巡线一个稳定接管窗口。 */
        fly_state_count++;
        if (fly_state_count >= FLY_COOLDOWN_COUNT)
        {
            fly_reset_state();
        }
        break;

    default:
        fly_reset_state();
        break;
    }
}

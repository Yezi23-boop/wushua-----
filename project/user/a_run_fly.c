/**
 * @file a_run_fly.c
 * @brief 飞坡/跷跷板状态机与速度覆盖逻辑
 * @details
 * 本模块只处理跷跷板入口识别、离线保持和落地恢复。入口检测窗口由赛道元素
 * 仲裁控制，5ms 主控制链路直接传入是否允许触发入口。
 */
#include "zf_common_headfile.h"

/* --- 飞坡/跷跷板状态内部变量 --- */
static int fly_detect_count = 0;   /* 入口弱磁连续确认计数，单位为 5ms 周期。 */
static int fly_state_count = 0;    /* 保持、恢复和冷却阶段共用计数，单位为 5ms 周期。 */
static uint8 fly_finish_event = 0; /* 跷跷板恢复完成事件，由元素仲裁在 5ms 链路中单次消费。 */
static uint16 fly_recover_count = 0; /* 进入 RECOVER 后的总时长计数，单位为 5ms 周期。 */
static uint8 fly_recover_speed_step_count = 0; /* RECOVER 阶梯提速节拍计数，单位为 5ms 周期。 */
static int fly_recover_speed = 0; /* RECOVER 当前阶梯速度，最大不超过 app.fly.count_fly_speed。 */
volatile uint8 fly_lost_line_blocked = 0; /* 飞坡高风险窗口屏蔽丢线；RECOVER 超过 1s 后恢复保护。 */

/* --- 飞坡/跷跷板状态机参数（run_time_1 以 5ms 调用） --- */
#define FLY_AD_SIDE_LOST_TH 14u  /* 横向电感低于该值时认为主线信号正在消失 */
#define FLY_AD_CENTER_LOST_TH 5u /* 竖向电感阈值更低，避免普通弱弯误触发飞坡 */
#define FLY_LANDING_SIDE_TH 15u  /* HOLD 结束后横向电感任一路回升到该值，才允许进入落地恢复。 */
#define FLY_LANDING_CENTER_TH 10u /* HOLD 结束后竖向电感任一路回升到该值，辅助确认车已接近地面电磁线。 */
#define FLY_HOLD_ANGLE 0         /* 离线保持阶段固定目标角速度，0 表示直行锁角。 */
#define FLY_RECOVER_LINE_STABLE_COUNT 3u /* RECOVER 中线连续稳定确认次数，5ms * 3 = 15ms。 */
#define FLY_COOLDOWN_COUNT 10    /* 退出冷却窗口，单位 5ms，默认约 100ms */
#define FLY_RECOVER_STEER_LIMIT_COUNT 40u /* RECOVER 前 5ms * 40 = 200ms 限制转向，避免刚贴地时大差速打滑。 */
#define FLY_RECOVER_STEER_LIMIT 8.0f /* RECOVER 前段小角速度找线，避免刚贴地时大差速打滑。 */
#define FLY_RECOVER_SPEED_START 10 /* RECOVER 阶梯起步速度，避免从 0 过慢也避免直接满速打滑。 */
#define FLY_RECOVER_SPEED_STEP 3 /* RECOVER 阶梯提速单步增量。 */
#define FLY_RECOVER_SPEED_STEP_COUNT 20u /* 每 5ms * 10 = 50ms 提升一次速度。 */
#define FLY_RECOVER_LOST_LINE_ENABLE_COUNT 200u /* RECOVER 超过 5ms * 200 = 1000ms 仍未完成时恢复丢线保护。 */

/**
 * @brief 判断当前电感是否满足飞坡入口弱磁特征。
 *
 * 飞坡入口通常表现为四路归一化电感同时快速跌低；当前版本不再依赖 acc_z，
 * 入口是否开放完全由赛道元素仲裁的 `allow_entry` 控制。
 *
 * @return int8 1-满足飞坡入口特征，0-不满足。
 *
 * @note 由 5ms 主控制环调用，只做常量比较，避免增加实时链路负担。
 */
static int8 fly_is_ramp_lost_signal(void)
{
    return (ad1 < FLY_AD_SIDE_LOST_TH &&
            ad2 < FLY_AD_CENTER_LOST_TH &&
            ad3 < FLY_AD_CENTER_LOST_TH &&
            ad4 < FLY_AD_SIDE_LOST_TH)
               ? 1
               : 0;
}

/**
 * @brief 判断 HOLD 结束后电感是否已回升到可落地恢复状态。
 *
 * HOLD 阶段只靠固定时间退出会在车还悬空时提前进入 RECOVER；这里要求至少一路
 * 横向或竖向电感回升，说明车已重新接近电磁线，再允许开始吸稳和找线。
 *
 * @return int8 1-电感已有回升，可进入 RECOVER；0-仍处于弱磁/离线阶段。
 */
static int8 fly_is_landing_signal(void)
{
    return (ad1 > FLY_LANDING_SIDE_TH ||
            ad4 > FLY_LANDING_SIDE_TH ||
            ad2 > FLY_LANDING_CENTER_TH ||
            ad3 > FLY_LANDING_CENTER_TH)
               ? 1
               : 0;
}

/**
 * @brief 判断飞坡落地恢复是否已回到中线附近。
 *
 * 中线在控制链路中对应 Err 为 0，同时用横向主电感 ad1/ad4 差值约束电感平衡。
 * 原因是落地后单看 Err 可能受瞬态计算影响，双条件可以减少偏线误退出。
 *
 * @return int8 1-已接近中线，0-仍需继续低速回正。
 */
static int8 fly_is_center_line(void)
{
    return (func_abs((int)ad1 - (int)ad4) < 10 && ad1 > 20 && ad4 > 20 &&
            Err > -2.0f && Err < 2.0f)
               ? 1
               : 0;
}

/**
 * @brief 取出并清除飞坡/跷跷板完成事件。
 *
 * 完成事件只允许元素仲裁消费一次，避免墙面流程被同一次恢复确认重复触发。
 *
 * @return uint8 1-存在待消费完成事件，0-无事件。
 */
uint8 a_run_fly_take_finish_event(void)
{
    uint8 event;

    event = fly_finish_event;
    fly_finish_event = 0;

    return event;
}

/**
 * @brief 复位飞坡状态机内部计数并回到普通巡线。
 *
 * 关闭飞坡开关、元素仲裁复位或重新进入跷跷板阶段时，需要同时清掉阶段计数和
 * 完成事件，避免沿用上一轮弱磁窗口中的残留状态。
 */
void a_run_fly_reset(void)
{
    fly_detect_count = 0;
    fly_state_count = 0;
    fly_recover_count = 0;
    fly_recover_speed_step_count = 0;
    fly_recover_speed = 0;
    fly_finish_event = 0;
    fly_lost_line_blocked = 0;
    flat_fly = FLY_STATE_IDLE;
}

/**
 * @brief 飞坡/跷跷板速度修正。
 *
 * 根据四路电感特征推进飞坡状态机，并在高风险阶段覆盖速度和转向输出。入口检测
 * 只在元素仲裁允许时开放；一旦进入保持/恢复阶段，即使仲裁下一拍切换，也会继续
 * 完成当前保护流程，避免半途释放控制权。
 *
 * @param speed 输出的目标速度指针。
 * @param allow_entry 1-当前期望元素为跷跷板，允许空闲态检测入口；0-禁止新入口。
 */
void a_run_fly_update_speed(int *speed, uint8 allow_entry)
{
    if (app.fly.fly_ramp_enable != 1)
    {
        a_run_fly_reset();
        return;
    }

    switch (flat_fly)
    {
    case FLY_STATE_IDLE:
        fly_lost_line_blocked = 0;
        if (allow_entry != 0 && fly_is_ramp_lost_signal())
        {
            fly_detect_count++;
            if (fly_detect_count >= app.fly.count_fly_time_1)
            {
                fly_detect_count = 0;
                fly_state_count = 0;
                fly_recover_count = 0;
                fly_recover_speed_step_count = 0;
                fly_recover_speed = 0;
                fly_lost_line_blocked = 1;
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
            if (fly_is_landing_signal())
            {
                fly_state_count = 0;
                fly_recover_count = 0;
                fly_recover_speed_step_count = 0;
                fly_recover_speed = 0;
                fly_lost_line_blocked = 1;
                flat_fly = FLY_STATE_RECOVER;
            }
            else
            {
                fly_state_count = app.fly.count_fly_time_2;
            }
        }
        break;

    case FLY_STATE_RECOVER:
        /*
         * 电感回升后从 10 开始阶梯提速找线，并在前段限制转向。
         * 这样避免刚贴地时速度和差速同时过大，导致车身滑动后 Err 失真。
         */
        if (fly_recover_count < FLY_RECOVER_LOST_LINE_ENABLE_COUNT)
        {
            fly_recover_count++;
            fly_lost_line_blocked = 1;
        }
        else
        {
            fly_lost_line_blocked = 0;
        }

        if (fly_recover_count <= FLY_RECOVER_STEER_LIMIT_COUNT)
        {
            if (PID.steer.output > FLY_RECOVER_STEER_LIMIT)
            {
                PID.steer.output = FLY_RECOVER_STEER_LIMIT;
            }
            else if (PID.steer.output < -FLY_RECOVER_STEER_LIMIT)
            {
                PID.steer.output = -FLY_RECOVER_STEER_LIMIT;
            }
        }

        if (app.fly.count_fly_speed <= 0)
        {
            fly_recover_speed = 0;
        }
        else if (app.fly.count_fly_speed <= FLY_RECOVER_SPEED_START)
        {
            fly_recover_speed = app.fly.count_fly_speed;
        }
        else
        {
            if (fly_recover_speed < FLY_RECOVER_SPEED_START)
            {
                fly_recover_speed = FLY_RECOVER_SPEED_START;
            }

            fly_recover_speed_step_count++;
            if (fly_recover_speed_step_count >= FLY_RECOVER_SPEED_STEP_COUNT)
            {
                fly_recover_speed_step_count = 0;
                if (fly_recover_speed < app.fly.count_fly_speed)
                {
                    fly_recover_speed += FLY_RECOVER_SPEED_STEP;
                    if (fly_recover_speed > app.fly.count_fly_speed)
                    {
                        fly_recover_speed = app.fly.count_fly_speed;
                    }
                }
            }
        }
        *speed = fly_recover_speed;

        if (fly_is_center_line())
        {
            fly_state_count++;
            if (fly_state_count >= FLY_RECOVER_LINE_STABLE_COUNT)
            {
//                stop = 1;
                fly_state_count = 0;
                fly_recover_count = 0;
                fly_recover_speed_step_count = 0;
                fly_recover_speed = 0;
                fly_lost_line_blocked = 1;
                fly_finish_event = 1;
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
        fly_lost_line_blocked = 1;
        fly_state_count++;
        if (fly_state_count >= FLY_COOLDOWN_COUNT)
        {
            a_run_fly_reset();
        }
        break;

    default:
        a_run_fly_reset();
        break;
    }
}

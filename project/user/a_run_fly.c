/**
 * @file a_run_fly.c
 * @brief 飞坡/跷跷板状态机与速度覆盖逻辑
 * @details
 * 本模块处理跷跷板入口识别、离线保持、落地恢复和完成后的速度斜坡释放。
 * 入口检测窗口由赛道元素仲裁控制，释放阶段独立于当前元素持续运行。
 */
#include "zf_common_headfile.h"

/* --- 飞坡/跷跷板状态内部变量 --- */
static int fly_detect_count = 0;          /* 入口弱磁连续确认计数，单位为 2ms 周期。 */
static int fly_state_count = 0;           /* IDLE 阶段检测计数，单位为 2ms 周期。 */
static uint8 fly_finish_event = 0;        /* 跷跷板恢复完成事件，由元素仲裁在 2ms 链路中单次消费。 */
static float fly_release_speed = 0.0f;    /* COOLDOWN 速度斜坡当前输出值，保留 app.speed.speed_run 的小数精度。 */
volatile uint8 fly_lost_line_blocked = 0; /* 飞坡高风险窗口屏蔽丢线。 */
volatile int32 fly_pwm_output_limit = 0;  /* COOLDOWN 期间限制最终 PWM 占空比，0 表示不额外限制。 */

/* --- 跷跷板停止等待状态内部变量 --- */
static SeesawState seesaw_state = SEESAW_IDLE; /**< 停止等待状态机阶段 */
static int seesaw_detect_count = 0;            /**< 停止等待入口窗口内有效命中计数，单位为 2ms 周期。 */
static int seesaw_entry_window_count = 0;      /**< 停止等待入口趋势确认窗口计数，单位为 2ms 周期。 */
static int seesaw_brake_count = 0;             /**< 零速闭环刹车计数，单位为 2ms 周期。 */
static int seesaw_wait_count = 0;              /**< 等待倾斜计数，单位为 2ms 周期 */
static float seesaw_creep_distance = 0.0f;     /**< 零速刹车后前挪里程积分，单位沿用速度积分标尺 cm。 */
static uint16 seesaw_last_ad1 = 0;             /**< 停止等待入口上一拍 ad1，用于确认横向电感持续递减。 */
static uint16 seesaw_last_ad4 = 0;             /**< 停止等待入口上一拍 ad4，用于确认横向电感持续递减。 */
static uint8 seesaw_last_ad_valid = 0;         /**< 上一拍 ad1/ad4 是否可用于递减比较。 */
volatile uint8 seesaw_zero_brake_active = 0;   /**< 零速闭环刹车窗口，主控链路用 signed 速度反馈压到 0。 */
volatile uint8 seesaw_centering_active = 0;    /**< 跷跷板前挪/恢复期临时居中权重开关。 */

/* --- 入口/落地电感阈值 --- */
/* --- 飞坡模式专用阈值 --- */
#define FLY_DETECT_SIDE_TH 25u    /* 飞坡入口横向电感阈值 */
#define FLY_DETECT_CENTER_TH 10u  /* 飞坡入口竖向电感阈值 */
#define FLY_LAND_SIDE_TH 20u      /* 飞跃落地横向电感回升阈值 */
#define FLY_LAND_CENTER_TH 10u    /* 飞跃落地竖向电感回升阈值 */

/* --- 停止等待模式专用阈值 --- */
#define SEESAW_DETECT_SIDE_TH 25u   /* 停止等待入口横向电感阈值 */
#define SEESAW_DETECT_CENTER_TH 10u /* 停止等待入口竖向电感阈值 */
#define SEESAW_LAND_SIDE_TH 20u     /* CHECK 阶段横向电感恢复阈值 */
#define SEESAW_LAND_CENTER_TH 10u   /* CHECK 阶段竖向电感恢复阈值 */
#define SEESAW_ENTRY_WINDOW_COUNT 10 /* 停止等待入口确认窗口，10 * 2ms = 20ms。 */
#define SEESAW_RECOVER_SPEED 10      /* 停止等待 COOLDOWN 固定恢复速度，改为编译期常量。 */

/* --- 速度与 PWM 限制 --- */
#define FLY_PWM_LIMIT_RECOVER_LATE 4000 /* COOLDOWN 阶段 PWM 上限，给循迹留出纠偏能力。 */
#define SEESAW_BRAKE_COUNT 10           /* 10 * 2ms = 20ms，用零速闭环先抵消上板惯性。 */

/**
 * @brief 取出并清除飞坡/跷跷板完成事件。
 *
 * 完成事件只允许元素仲裁消费一次，避免后续元素被同一次恢复确认重复触发。
 *
 * @return uint8 1-存在待消费完成事件，0-无事件。
 */
uint8 a_run_fly_take_finish_event(void)
{
    uint8 event = fly_finish_event;

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
    fly_release_speed = 0.0f;
    fly_finish_event = 0;
    fly_lost_line_blocked = 0;
    fly_pwm_output_limit = 0;
    seesaw_centering_active = 0;
    flat_fly = FLY_STATE_IDLE;
}

/**
 * @brief 复位跷跷板停止等待状态机内部计数并回到普通巡线。
 *
 * 元素仲裁关闭或重新进入跷跷板阶段前调用，清掉计数和阶段。
 */
void a_run_seesaw_reset(void)
{
    seesaw_state = SEESAW_IDLE;
    seesaw_detect_count = 0;
    seesaw_entry_window_count = 0;
    seesaw_brake_count = 0;
    seesaw_wait_count = 0;
    seesaw_creep_distance = 0.0f;
    seesaw_last_ad1 = 0;
    seesaw_last_ad4 = 0;
    seesaw_last_ad_valid = 0;
    seesaw_zero_brake_active = 0;
    seesaw_centering_active = 0;
    fly_lost_line_blocked = 0;
    fly_finish_event = 0;
    fly_pwm_output_limit = 0;
}

/**
 * @brief 跷跷板停止等待模式速度状态机。
 *
 * 检测到跷跷板后停车等待 1 秒，利用重力让跷跷板倾斜，
 * 电感信号恢复后出发，阶梯增速恢复到巡线速度。
 *
 * @param speed 输出目标速度指针。
 * @param allow_entry 1-当前期望元素为跷跷板，允许从空闲态检测入口；0-禁止新入口。
 */
void a_run_seesaw_update_speed(float *speed, uint8 allow_entry)
{
    int seesaw_wait_limit;
    int seesaw_hit_limit;
    float creep_target;
    float creep_delta;
    uint8 weak_line;
    uint8 entry_hit;

    seesaw_wait_limit = app.fly.seesaw_wait_count;
    seesaw_hit_limit = app.fly.seesaw_detect_count;
    creep_target = app.fly.seesaw_creep_cm;

    switch (seesaw_state)
    {
    case SEESAW_IDLE:
        /*
         * 普通赛道也可能出现单拍弱磁，跷跷板入口要求在 20ms 窗口内多次出现
         * 四路弱磁且 ad1/ad4 同时递减，确认整车正在离开电磁线后再刹车。
         */
        weak_line = (uint8)(allow_entry != 0 &&
                            ad1 <= SEESAW_DETECT_SIDE_TH &&
                            ad2 <= SEESAW_DETECT_CENTER_TH &&
                            ad3 <= SEESAW_DETECT_CENTER_TH &&
                            ad4 <= SEESAW_DETECT_SIDE_TH);
        entry_hit = 0;
        if (weak_line != 0)
        {
            if (seesaw_last_ad_valid != 0 &&
                ad1 < seesaw_last_ad1 &&
                ad4 < seesaw_last_ad4)
            {
                entry_hit = 1;
            }

            seesaw_last_ad1 = ad1;
            seesaw_last_ad4 = ad4;
            seesaw_last_ad_valid = 1;

            if (entry_hit != 0)
            {
                if (seesaw_entry_window_count == 0)
                {
                    seesaw_entry_window_count = 1;
                    seesaw_detect_count = 1;
                }
                else
                {
                    seesaw_entry_window_count++;
                    seesaw_detect_count++;
                }

                if (seesaw_detect_count >= seesaw_hit_limit)
                {
                    seesaw_detect_count = 0;
                    seesaw_entry_window_count = 0;
                    seesaw_last_ad_valid = 0;
                    seesaw_brake_count = 0;
                    pid_speed_reset(&PID.left_speed);
                    pid_speed_reset(&PID.right_speed);
                    *speed = 0.0f;
                    fly_lost_line_blocked = 1;
                    stop = 0;
                    seesaw_zero_brake_active = 1;
                    seesaw_state = SEESAW_BRAKE;
                }
            }
            else if (seesaw_entry_window_count > 0)
            {
                seesaw_entry_window_count++;
            }

            if (seesaw_state == SEESAW_IDLE &&
                seesaw_entry_window_count >= SEESAW_ENTRY_WINDOW_COUNT)
            {
                seesaw_detect_count = 0;
                seesaw_entry_window_count = 0;
                seesaw_last_ad_valid = 0;
            }
        }
        else
        {
            seesaw_detect_count = 0;
            seesaw_entry_window_count = 0;
            seesaw_last_ad_valid = 0;
        }
        break;

    case SEESAW_STOP:
        /* 借用全局停车锁存，直接压住电机输出，恢复阶段再释放。 */
        *speed = 0.0f;
        fly_lost_line_blocked = 1;
        if (seesaw_zero_brake_active != 0)
        {
            pid_speed_reset(&PID.left_speed);
            pid_speed_reset(&PID.right_speed);
            seesaw_zero_brake_active = 0;
        }
        seesaw_centering_active = 0;
        stop = 1;
        seesaw_wait_count = 0;
        seesaw_state = SEESAW_WAIT;
        break;

    case SEESAW_BRAKE:
        /*
         * 上板后单靠 stop=1 会滑行，先用速度环把目标压到 0。
         * 这里临时放开 stop，并要求主控链路使用 signed 编码器反馈，避免倒滑也被当成前进速度。
         */
        *speed = 0.0f;
        fly_lost_line_blocked = 1;
        fly_pwm_output_limit = 0;
        stop = 0;
        seesaw_zero_brake_active = 1;
        seesaw_brake_count++;
        if (seesaw_brake_count >= SEESAW_BRAKE_COUNT)
        {
            seesaw_brake_count = 0;
            seesaw_creep_distance = 0.0f;
            if (creep_target > 0.0f)
            {
                seesaw_centering_active = 0;
                seesaw_state = SEESAW_CREEP;
            }
            else
            {
                seesaw_centering_active = 0;
                seesaw_state = SEESAW_STOP;
            }
        }
        break;

    case SEESAW_CREEP:
        /*
         * 零速刹车后低速向前循迹一小段，让车重更靠后压住跷跷板。
         * 里程积分沿用普通 abs 速度反馈和现场标定系数，保持与原前挪距离调参一致。
         */
        *speed = (float)app.fly.seesaw_speed;
        fly_lost_line_blocked = 1;
        if (seesaw_zero_brake_active != 0)
        {
            pid_speed_reset(&PID.left_speed);
            pid_speed_reset(&PID.right_speed);
            seesaw_zero_brake_active = 0;
        }
        stop = 0;
        creep_delta = (speed_l + speed_r) * 0.5f * 0.012f;
        if (creep_delta > 0.0f)
        {
            seesaw_creep_distance += creep_delta;
        }
        if (seesaw_creep_distance >= creep_target)
        {
            seesaw_creep_distance = 0.0f;
            seesaw_centering_active = 0;
            seesaw_state = SEESAW_STOP;
        }
        break;

    case SEESAW_WAIT:
        /* 等待时间由菜单配置，单位为 2ms 主控制周期。 */
        *speed = 0.0f;
        stop = 1;
        seesaw_wait_count++;
        if (seesaw_wait_count >= seesaw_wait_limit)
        {
            seesaw_state = SEESAW_CHECK;
        }
        break;

    case SEESAW_CHECK:
        /* 检查电感信号恢复 */
        *speed = 0.0f;
        stop = 1;
        if (ad1 > SEESAW_LAND_SIDE_TH ||
            ad4 > SEESAW_LAND_SIDE_TH ||
            ad2 > SEESAW_LAND_CENTER_TH ||
            ad3 > SEESAW_LAND_CENTER_TH)
        {
            seesaw_state = SEESAW_RECOVER;
        }
        break;

    case SEESAW_RECOVER:
        /* 阶梯增速恢复，复用 COOLDOWN 逻辑 */
        pid_speed_reset(&PID.left_speed);
        pid_speed_reset(&PID.right_speed);
        stop = 0;
        fly_release_speed = (float)SEESAW_RECOVER_SPEED;
        fly_finish_event = 1;
        flat_fly = FLY_STATE_COOLDOWN;
        seesaw_state = SEESAW_COOLDOWN;
        break;

    case SEESAW_COOLDOWN:
        /* 释放阶段由 a_run_fly_update_release_speed() 执行 */
        break;
    }
}

/**
 * @brief 更新跷跷板完成后的阶梯增速。
 *
 * 完成事件只表示跷跷板本体可以切到序列中的下一个元素，不代表速度保护结束。
 * 因此该函数独立于当前元素运行，只要处于 COOLDOWN 就继续按主控制环周期释放速度。
 *
 * @param speed 输出的目标速度指针，保留小数速度设定。
 */
void a_run_fly_update_release_speed(float *speed)
{
    float target_speed;

    if (flat_fly != FLY_STATE_COOLDOWN)
    {
        return;
    }

    seesaw_centering_active = 1;

    /*
     * 跷跷板落地回线后仍按斜坡释放速度。
     * 原因：负压、轮胎贴地和循迹误差都需要短暂恢复窗口，若完成事件后一拍
     * 回到巡线速度，后续无论接墙面、圆桶还是普通赛道都容易被惯性带偏。
     */
    target_speed = app.speed.speed_run;
    if (fly_release_speed >= target_speed)
    {
        *speed = target_speed;
        a_run_fly_reset();
        return;
    }

    *speed = fly_release_speed;
    if (app.fly.seesaw_mode == 0)
        fly_release_speed += app.fly.fly_release_step;
    else
        fly_release_speed += app.fly.seesaw_release_step;
    if (fly_release_speed > target_speed)
    {
        fly_release_speed = target_speed;
    }

    fly_lost_line_blocked = 1;
    fly_pwm_output_limit = FLY_PWM_LIMIT_RECOVER_LATE;
}

/**
 * @brief 飞坡/跷跷板本体速度修正。
 *
 * 根据四路电感特征推进飞坡状态机，并在高风险阶段覆盖速度和转向输出。入口检测
 * 只在元素仲裁允许时开放；进入 COOLDOWN 后由 a_run_fly_update_release_speed()
 * 继续完成阶梯增速，避免元素切换打断释放过程。
 *
 * @param speed 输出的目标速度指针，保留小数速度设定。
 * @param allow_entry 1-当前期望元素为跷跷板，允许空闲态检测入口；0-禁止新入口。
 */
void a_run_fly_update_speed(float *speed, uint8 allow_entry)
{
    switch (flat_fly)
    {
    case FLY_STATE_IDLE:
        if (allow_entry != 0 &&
            ad1 <= FLY_DETECT_SIDE_TH &&
            ad2 <= FLY_DETECT_CENTER_TH &&
            ad3 <= FLY_DETECT_CENTER_TH &&
            ad4 <= FLY_DETECT_SIDE_TH)
        {
            fly_detect_count++;
            if (fly_detect_count >= app.fly.fly_detect_count)
            {
                fly_detect_count = 0;
                fly_lost_line_blocked = 1;
                flat_fly = FLY_STATE_LOW;
            }
            else
            {
                break;
            }
        }
        else
        {
            fly_detect_count = 0;
            break;
        }
        /* 触发成立的同一控制周期立即降速，避免飞坡入口多放行一个主控制环周期。 */

    case FLY_STATE_LOW:
        /*
         * 跷跷板上低速通过，无 PWM 限制，转向保留循迹纠偏。
         * 四路电感全部 ≤ fly_airborne_th 时认为车已离地，切入 FLY。
         */
        *speed = (float)app.fly.fly_speed;
        fly_lost_line_blocked = 1;
        fly_pwm_output_limit = 0;
        if (ad1 <= (uint16)app.fly.fly_airborne_th &&
            ad2 <= (uint16)app.fly.fly_airborne_th &&
            ad3 <= (uint16)app.fly.fly_airborne_th &&
            ad4 <= (uint16)app.fly.fly_airborne_th)
        {
            flat_fly = FLY_STATE_FLY;
        }
        break;

    case FLY_STATE_FLY:
        /*
         * 空中飞跃：速度和转向清零，避免空中轮子转速过高导致落地打滑。
         * 电感回升后认为车已落地，直接进入 COOLDOWN 阶梯增速。
         */
        *speed = 0.0f;
        fly_lost_line_blocked = 1;
        fly_pwm_output_limit = 0;
        PID.steer.output = 0.0f;
        if (ad1 > FLY_LAND_SIDE_TH ||
            ad4 > FLY_LAND_SIDE_TH ||
            ad2 > FLY_LAND_CENTER_TH ||
            ad3 > FLY_LAND_CENTER_TH)
        {
            pid_speed_reset(&PID.left_speed);
            pid_speed_reset(&PID.right_speed);
            seesaw_centering_active = 1;
            fly_release_speed = 0.0f;
            fly_finish_event = 1;
            fly_pwm_output_limit = FLY_PWM_LIMIT_RECOVER_LATE;
            flat_fly = FLY_STATE_COOLDOWN;
        }
        break;

    case FLY_STATE_COOLDOWN:
        /* 释放阶段由 a_run_fly_update_release_speed() 执行 */
        break;
    }
}

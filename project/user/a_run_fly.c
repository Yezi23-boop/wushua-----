/**
 * @file a_run_fly.c
 * @brief 飞坡/跷跷板状态机与速度覆盖逻辑
 * @details
 * 本模块处理跷跷板入口识别、离线保持、落地恢复和完成后的速度斜坡释放。
 * 入口检测窗口由赛道元素仲裁控制，释放阶段独立于当前元素持续运行。
 */
#include "zf_common_headfile.h"

/* --- 飞坡/跷跷板状态内部变量 --- */
static FlyState fly_state = FLY_STATE_IDLE; /**< 飞坡模式状态机阶段。 */
static int fly_detect_count = 0;            /* 入口弱磁连续确认计数，单位为 2ms 周期。 */
static int fly_state_count = 0;             /* 飞坡入口趋势确认窗口计数，单位为 2ms 周期。 */
static uint8 fly_finish_event = 0;          /* 跷跷板恢复完成事件，由元素仲裁在 2ms 链路中单次消费。 */
static float fly_release_speed = 0.0f;      /* COOLDOWN 速度斜坡当前输出值，保留 app.speed.speed_run 的小数精度。 */
volatile uint8 fly_lost_line_blocked = 0;   /* 飞坡高风险窗口屏蔽丢线。 */
volatile int32 fly_pwm_output_limit = 0;    /* COOLDOWN 期间限制最终 PWM 占空比，0 表示不额外限制。 */
static uint8 fly_land_confirm_count = 0;    /* 飞坡落地回升连续确认计数，单位为 2ms 周期。 */
static uint16 fly_last_ad1 = 0;             /* 飞坡入口上一拍 ad1，用于确认横向电感持续递减。 */
static uint16 fly_last_ad4 = 0;             /* 飞坡入口上一拍 ad4，用于确认横向电感持续递减。 */
static uint16 fly_last_ad5 = 0;             /* 飞坡入口上一拍 ad5，用于确认中横电感持续递减。 */
static uint8 fly_last_ad_valid = 0;         /* 上一拍 ad1/ad4/ad5 是否可用于递减比较。 */

/* --- 跷跷板停止等待状态内部变量 --- */
static SeesawState seesaw_state = SEESAW_STATE_IDLE; /**< 停止等待状态机阶段 */
static int seesaw_detect_count = 0;                  /**< 停止等待入口窗口内有效命中计数，单位为 2ms 周期。 */
static int seesaw_entry_window_count = 0;            /**< 停止等待入口确认窗口计数，单位为 2ms 周期。 */
static int seesaw_brake_count = 0;                   /**< 零速闭环刹车计数，单位为 2ms 周期。 */
static int seesaw_wait_count = 0;                    /**< 等待倾斜计数，单位为 2ms 周期 */
static float seesaw_creep_distance = 0.0f;           /**< 零速刹车后前挪里程积分，单位沿用速度积分标尺 cm。 */
static uint16 seesaw_last_ad1 = 0;                   /**< 停止等待入口上一拍 ad1，用于确认横向电感持续递减。 */
static uint16 seesaw_last_ad4 = 0;                   /**< 停止等待入口上一拍 ad4，用于确认横向电感持续递减。 */
static uint16 seesaw_last_ad5 = 0;                   /**< 停止等待入口上一拍 ad5，用于确认中横电感持续递减。 */
static uint8 seesaw_last_ad_valid = 0;               /**< 上一拍 ad1/ad4/ad5 是否可用于递减比较。 */
volatile uint8 seesaw_zero_brake_active = 0;         /**< 零速闭环刹车窗口，主控链路用 signed 速度反馈压到 0。 */
volatile uint8 seesaw_centering_active = 0;          /**< 跷跷板前挪/恢复期临时居中权重开关。 */

/* --- 飞坡模式专用阈值 --- */
#define FLY_DETECT_SIDE_TH 20u    /* 飞坡入口横向电感阈值 */
#define FLY_DETECT_CENTER_TH 3u   /* 飞坡入口竖向电感阈值 */
#define FLY_LAND_SIDE_TH 20u      /* 飞坡落地横向电感回升阈值 */
#define FLY_ENTRY_WINDOW_COUNT 10 /* 飞坡入口确认窗口，10 * 2ms = 20ms。 */

/* --- 停止等待模式专用阈值 --- */
#define SEESAW_DETECT_SIDE_TH 20u    /* 停止等待入口横向电感阈值 */
#define SEESAW_DETECT_CENTER_TH 8u   /* 停止等待入口竖向电感阈值 */
#define SEESAW_LAND_SIDE_TH 25u      /* CHECK 阶段横向电感恢复阈值 */
#define SEESAW_LAND_CENTER_TH 10u    /* CHECK 阶段竖向电感恢复阈值 */
#define SEESAW_ENTRY_WINDOW_COUNT 20 /* 停止等待入口确认窗口，20 * 2ms = 40ms。 */
#define SEESAW_BRAKE_COUNT 10        /* 10 * 2ms = 20ms，用零速闭环先抵消上板惯性。 */

/* --- 飞坡/跷跷板恢复阶段共用限制 --- */
#define FLY_PWM_LIMIT_RECOVER_LATE 4000 /* COOLDOWN 阶段 PWM 上限，给循迹留出纠偏能力。 */

/**
 * @brief 更新飞坡/停止等待模式共用的入口判定窗口。
 *
 * 两种模式共用弱磁窗口；飞坡额外要求 ad1/ad4/ad5 递减，停止等待直接累计弱磁命中。
 *
 * @param allow_entry 当前元素仲裁是否允许开启入口检测。
 * @param side_th 横向电感弱磁阈值。
 * @param center_th 竖向电感弱磁阈值。
 * @param hit_limit 窗口内需要累计到的有效命中次数。
 * @param window_limit 入口统计窗口长度，单位为 2ms 周期。
 * @param require_decrease 1-要求 ad1/ad4/ad5 递减；0-弱磁拍直接计为命中。
 * @param detect_count 有效命中次数指针。
 * @param window_count 入口窗口计数指针。
 * @param last_ad1 上一拍 ad1 指针。
 * @param last_ad4 上一拍 ad4 指针。
 * @param last_ad_valid 上一拍 ad1/ad4 是否有效。
 * @return uint8 1-入口成立，0-入口未成立。
 */
static uint8 a_run_fly_update_entry_gate(uint8 allow_entry,
                                         uint16 side_th,
                                         uint16 center_th,
                                         int hit_limit,
                                         int window_limit,
                                         uint8 require_decrease,
                                         int *detect_count,
                                         int *window_count,
                                         uint16 *last_ad1,
                                         uint16 *last_ad4,
                                         uint16 *last_ad5,
                                         uint8 *last_ad_valid)
{
    uint8 weak_line;
    uint8 entry_hit;

    weak_line = (uint8)(allow_entry != 0 &&
                        ad1 <= side_th &&
                        ad2 <= center_th &&
                        ad3 <= center_th &&
                        ad4 <= side_th &&
                        ad5 < 8u);
    entry_hit = 0;
    if (weak_line != 0)
    {
        /*
         * 只在首次进入弱磁窗口时检查速度门槛，窗口期间不再检查。
         * 原因：上板后速度自然下降，连续检查会导致窗口断裂。
         */
        if (*window_count == 0 && *detect_count == 0)
        {
            /*
             * 0.9f 速度门槛：要求当前速度不低于目标速度的 90%，避免起步/低速段
             * 因电感信号天然偏弱而误判为飞坡入口。低速时弱磁是常态，不是飞坡。
             */
            if (!(speed_l > app.speed.speed_run * 0.6f &&
                  speed_r > app.speed.speed_run * 0.6f))
            {
                return 0;
            }
        }
        if (require_decrease == 0)
        {
            entry_hit = 1;
        }
        else if (*last_ad_valid != 0 &&
                 ad1 <= *last_ad1 &&
                 ad4 <= *last_ad4 &&
                 ad5 <= *last_ad5)
        {
            entry_hit = 1;
            *last_ad1 = ad1;
            *last_ad4 = ad4;
            *last_ad5 = ad5;
        }
        else if (*last_ad_valid == 0)
        {
            *last_ad1 = ad1;
            *last_ad4 = ad4;
            *last_ad5 = ad5;
            *last_ad_valid = 1;
        }

        if (entry_hit != 0)
        {
            (*window_count)++;
            (*detect_count)++;

            if (*detect_count >= hit_limit)
            {
                *detect_count = 0;
                *window_count = 0;
                *last_ad_valid = 0;
                return 1;
            }
        }
        else if (*window_count > 0)
        {
            (*window_count)++;
        }

        if (*window_count >= window_limit)
        {
            *detect_count = 0;
            *window_count = 0;
            *last_ad_valid = 0;
        }
    }
    else
    {
        *detect_count = 0;
        *window_count = 0;
        *last_ad_valid = 0;
    }

    return 0;
}

/**
 * @brief 读取飞坡模式状态。
 * @return FlyState 当前飞坡状态。
 */
FlyState a_run_fly_get_state(void)
{
    return fly_state;
}

/**
 * @brief 读取停止等待模式状态。
 * @return SeesawState 当前停止等待状态。
 */
SeesawState a_run_seesaw_get_state(void)
{
    return seesaw_state;
}

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
    fly_land_confirm_count = 0;
    seesaw_zero_brake_active = 0;
    seesaw_centering_active = 0;
    fly_last_ad1 = 0;
    fly_last_ad4 = 0;
    fly_last_ad5 = 0;
    fly_last_ad_valid = 0;
    fly_state = FLY_STATE_IDLE;
    seesaw_state = SEESAW_STATE_IDLE;
}

/**
 * @brief 复位跷跷板停止等待状态机内部计数并回到普通巡线。
 *
 * 元素仲裁关闭或重新进入跷跷板阶段前调用，清掉计数和阶段。
 */
void a_run_seesaw_reset(void)
{
    if (seesaw_state == SEESAW_STATE_HOLD_DELAY ||
        seesaw_state == SEESAW_STATE_WAIT_SIGNAL)
    {
        stop = 0;
    }
    fly_state = FLY_STATE_IDLE;
    seesaw_state = SEESAW_STATE_IDLE;
    seesaw_detect_count = 0;
    seesaw_entry_window_count = 0;
    seesaw_brake_count = 0;
    seesaw_wait_count = 0;
    seesaw_creep_distance = 0.0f;
    seesaw_last_ad1 = 0;
    seesaw_last_ad4 = 0;
    seesaw_last_ad5 = 0;
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
 * 检测到跷跷板后刹车、前挪并短暂保持，利用重力让跷跷板倾斜，
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

    seesaw_wait_limit = app.fly.seesaw_wait_count;
    seesaw_hit_limit = app.fly.seesaw_detect_count;
    creep_target = app.fly.seesaw_creep_cm;

    switch (seesaw_state)
    {
    case SEESAW_STATE_IDLE:
        /*
         * 普通赛道也可能出现单拍弱磁，跷跷板入口要求在 20ms 窗口内多次出现
         * 五路弱磁在窗口内累计达到设定次数后刹车，不要求各路逐拍递减。
         */
        if (a_run_fly_update_entry_gate(allow_entry,
                                        SEESAW_DETECT_SIDE_TH,
                                        SEESAW_DETECT_CENTER_TH,
                                        seesaw_hit_limit,
                                        SEESAW_ENTRY_WINDOW_COUNT,
                                        0,
                                        &seesaw_detect_count,
                                        &seesaw_entry_window_count,
                                        &seesaw_last_ad1,
                                        &seesaw_last_ad4,
                                        &seesaw_last_ad5,
                                        &seesaw_last_ad_valid) != 0)
        {
            seesaw_brake_count = 0;
            pid_speed_reset(&PID.left_speed);
            pid_speed_reset(&PID.right_speed);
            *speed = 0.0f;
            fly_lost_line_blocked = 1;
            seesaw_zero_brake_active = 1;
            seesaw_state = SEESAW_STATE_BRAKE;
        }
        break;

    case SEESAW_STATE_BRAKE:
        /*
         * 上板后先用速度环把目标压到 0，并使用 signed 编码器反馈，
         * 让前滑产生反向力矩、倒滑产生正向力矩。
         */
        *speed = 0.0f;
        fly_lost_line_blocked = 1;
        fly_pwm_output_limit = 0;
        seesaw_zero_brake_active = 1;
        seesaw_brake_count++;
        if (seesaw_brake_count >= SEESAW_BRAKE_COUNT)
        {
            seesaw_brake_count = 0;
            seesaw_creep_distance = 0.0f;
            seesaw_centering_active = 0;
            if (creep_target > 0.0f)
            {
                seesaw_state = SEESAW_STATE_CREEP;
            }
            else
            {
                seesaw_wait_count = 0;
                seesaw_zero_brake_active = 0;
                seesaw_state = SEESAW_STATE_HOLD_DELAY;
            }
        }
        break;

    case SEESAW_STATE_CREEP:
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
        /* 0.012f：里程积分系数，由采样周期(2ms)和轮径/编码器标定共同决定，将速度值转为每周期行驶距离(cm)。 */
        creep_delta = (speed_l + speed_r) * 0.5f * 0.012f;
        if (creep_delta > 0.0f)
        {
            seesaw_creep_distance += creep_delta;
        }
        if (seesaw_creep_distance >= creep_target)
        {
            //					stop=1;///
            seesaw_creep_distance = 0.0f;
            seesaw_wait_count = 0;
            seesaw_centering_active = 0;
            seesaw_state = SEESAW_STATE_HOLD_DELAY;
        }
        break;

    case SEESAW_STATE_HOLD_DELAY:
        /* 等待时间由菜单配置，单位为 2ms 主控制周期。 */
        *speed = 0.0f;
        seesaw_zero_brake_active = 0;
        if (seesaw_wait_count == 0)
        {
            stop = 1;
        }
        seesaw_wait_count++;
        if (seesaw_wait_count >= seesaw_wait_limit)
        {
            seesaw_state = SEESAW_STATE_WAIT_SIGNAL;
        }
        break;

    case SEESAW_STATE_WAIT_SIGNAL:
        /* 检查电感信号恢复 */
        *speed = 0.0f;
        if (ad1 > SEESAW_LAND_SIDE_TH ||
            ad4 > SEESAW_LAND_SIDE_TH ||
            ad2 > SEESAW_LAND_CENTER_TH ||
            ad3 > SEESAW_LAND_CENTER_TH)
        {
            pid_speed_reset(&PID.left_speed);
            pid_speed_reset(&PID.right_speed);
            stop = 0;
            fly_release_speed = (float)app.fly.seesaw_speed;
            fly_finish_event = 1;
            fly_state = FLY_STATE_COOLDOWN;
            seesaw_state = SEESAW_STATE_RELEASE;
        }
        break;

    case SEESAW_STATE_RELEASE:
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

    if (fly_state != FLY_STATE_COOLDOWN)
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
    switch (fly_state)
    {
    case FLY_STATE_IDLE:
        /*
         * 墙面后和普通赛道过渡段也可能出现短暂弱磁。
         * 飞坡入口要求在短窗口内多次出现四路弱磁，且 ad1/ad4 持续递减，
         * 确认整车正在真正离开电磁线后再切到 LOW，避免在跷跷板前提前误触发。
         */
        if (a_run_fly_update_entry_gate(allow_entry,
                                        FLY_DETECT_SIDE_TH,
                                        FLY_DETECT_CENTER_TH,
                                        app.fly.fly_detect_count,
                                        FLY_ENTRY_WINDOW_COUNT,
                                        1,
                                        &fly_detect_count,
                                        &fly_state_count,
                                        &fly_last_ad1,
                                        &fly_last_ad4,
                                        &fly_last_ad5,
                                        &fly_last_ad_valid) != 0)
        {
            fly_lost_line_blocked = 1;
            fly_state = FLY_STATE_LOW;
        }
        else
        {
            break;
        }
        /* 触发成立的同一控制周期立即降速，避免飞坡入口多放行一个主控制环周期。 */

    case FLY_STATE_LOW:
        /*
         * 跷跷板上低速通过，无 PWM 限制，转向保留循迹纠偏。
         * 不再尝试识别离地，只要后续出现连续 2 拍落地回升信号，就直接进入恢复段。
         */
        *speed = (float)app.fly.fly_speed;
        fly_lost_line_blocked = 1;
        fly_pwm_output_limit = 0;
        if (ad1 > FLY_LAND_SIDE_TH &&
            ad4 > FLY_LAND_SIDE_TH)
        {
            fly_land_confirm_count++;
            if (fly_land_confirm_count >= app.fly.fly_land_confirm_count)
            {
                fly_land_confirm_count = 0;
                pid_speed_reset(&PID.left_speed);
                pid_speed_reset(&PID.right_speed);
                seesaw_centering_active = 1;
                /*
                 * 飞坡落地仍有前向滑行速度，若 COOLDOWN 从 0 起步，
                 * 速度环会先给反向力矩去"刹停"，表现成小车短暂倒退。
                 * 因此这里直接从正的恢复速度起步，和停车模式保持同口径。
                 */
                fly_release_speed = (float)app.fly.fly_recover_speed;
                fly_finish_event = 1;
                fly_pwm_output_limit = FLY_PWM_LIMIT_RECOVER_LATE;
                fly_state = FLY_STATE_COOLDOWN;
            }
        }
        else
        {
            fly_land_confirm_count = 0;
        }
        break;

    case FLY_STATE_COOLDOWN:
        break;
    }
}

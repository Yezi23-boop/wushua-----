/**
 * @file a_run_fly.c
 * @brief 飞坡/跷跷板状态机与速度覆盖逻辑
 * @details
 * 本模块处理跷跷板入口识别、停止等待、落地恢复和完成后的阶梯增速释放。
 * 飞坡模式与停止等待模式共用入口判定和释放函数，由 app.fly.seesaw_mode 选择。
 * 释放期统一由 seesaw_state == SEESAW_STATE_RELEASE 标记，独立于当前元素持续运行。
 * 各共享标志（丢线屏蔽/PWM限幅/居中权重）只在状态迁移点置位，由复位统一清除。
 */
#include "zf_common_headfile.h"

/* 飞坡模式内部阶段，释放期统一由 SEESAW_STATE_RELEASE 标记，不再单独设阶段。 */
typedef enum
{
    FLY_STATE_IDLE = 0, /**< 普通巡线，允许在元素仲裁授权后检测入口。 */
    FLY_STATE_LOW = 1   /**< 飞坡上低速通过，等待落地回升信号。 */
} FlyState;

/* --- 飞坡/跷跷板共用内部变量 --- */
static FlyState fly_state = FLY_STATE_IDLE;          /**< 飞坡模式状态机阶段。 */
static SeesawState seesaw_state = SEESAW_STATE_IDLE; /**< 停止等待状态机阶段，兼作两种模式的释放期标记。 */
static uint8 seesaw_finish_event = 0;                /* 跷跷板恢复完成事件，由元素仲裁在 2ms 链路中单次消费。 */
static float seesaw_release_speed = 0.0f;            /* 释放阶梯增速当前输出值，保留 app.speed.speed_run 的小数精度。 */
static float seesaw_release_start_speed = 0.0f;      /* 进入 RELEASE 时的起步速度，用于 PWM 上限随速度插值。 */
volatile uint8 seesaw_lost_line_blocked = 0;         /* 飞坡/跷跷板高风险窗口屏蔽丢线。 */
volatile int32 seesaw_pwm_output_limit = 0;          /* 释放期间限制最终 PWM 占空比，0 表示不额外限制。 */

/* --- 飞坡模式内部变量 --- */
static uint8 fly_land_confirm_count = 0; /* 飞坡落地回升连续确认计数，单位为 2ms 周期。 */

/* --- 入口判定窗口共用变量，两种模式互斥运行故复用 --- */
static int entry_detect_count = 0; /* 入口连续弱磁命中计数，单位为 2ms 周期。 */

/* --- 停止等待模式内部变量 --- */
static int seesaw_brake_count = 0;           /**< 零速闭环刹车计数，单位为 2ms 周期。 */
static int seesaw_wait_count = 0;            /**< 等待倾斜计数，单位为 2ms 周期 */
static float seesaw_creep_distance = 0.0f;   /**< 零速刹车后前挪里程积分，单位沿用速度积分标尺 cm。 */
volatile uint8 seesaw_zero_brake_active = 0; /**< 零速闭环刹车窗口，主控链路用 signed 速度反馈压到 0。 */
volatile uint8 seesaw_centering_active = 0;  /**< 跷跷板前挪/恢复期临时居中权重开关。 */

/* --- 入口判定共用阈值（飞坡与停止等待两模式统一，取停止等待数值） --- */
#define SEESAW_ENTRY_SIDE_TH 20u  /* 入口横向电感 ad1/ad4 弱磁阈值 */
#define SEESAW_ENTRY_CENTER_TH 3u /* 入口竖向电感 ad2/ad3 弱磁阈值 */
#define SEESAW_ENTRY_AD5_TH 8u    /* 入口中横电感 ad5 弱磁阈值 */

/* --- 飞坡模式专用阈值 --- */
#define FLY_LAND_SIDE_TH 20u      /* 飞坡落地横向电感回升阈值 */
#define FLY_ENTRY_WINDOW_COUNT 10 /* 飞坡入口确认窗口，10 * 2ms = 20ms。 */

/* --- 停止等待模式专用阈值 --- */
#define SEESAW_LAND_SIDE_TH 25u      /* CHECK 阶段横向电感恢复阈值 */
#define SEESAW_LAND_CENTER_TH 10u    /* CHECK 阶段竖向电感恢复阈值 */
#define SEESAW_ENTRY_WINDOW_COUNT 20 /* 停止等待入口确认窗口，20 * 2ms = 40ms。 */
#define SEESAW_BRAKE_COUNT 10        /* 10 * 2ms = 20ms，用零速闭环先抵消上板惯性。 */

/* --- 释放阶段共用限制 --- */
#define FLY_PWM_LIMIT_RECOVER_START 6000             /* 释放起步 PWM 上限，给低速回线留纠偏余量。 */
#define FLY_PWM_LIMIT_RECOVER_END MOTOR_OUTPUT_PWM_LIMIT /* 释放爬满速度时的上限，回到全局限幅(9000)。 */

/**
 * @brief 更新飞坡/停止等待模式共用的入口判定窗口。
 *
 * 两种模式互斥运行，共用同一套弱磁命中计数：连续弱磁拍累计，
 * 达到设定次数后入口成立；任一拍非弱磁即清零重来。窗口上限只作
 * 兜底，防止菜单命中数配置过大时入口窗口无限拉长。
 *
 * @param allow_entry 当前元素仲裁是否允许开启入口检测。
 * @param side_th 横向电感弱磁阈值。
 * @param center_th 竖向电感弱磁阈值。
 * @param ad5_th 中横电感弱磁阈值。
 * @param hit_limit 入口成立所需的连续弱磁命中次数。
 * @param window_limit 入口统计窗口上限，单位为 2ms 周期。
 * @return uint8 1-入口成立，0-入口未成立。
 */
static uint8 a_run_fly_update_entry_gate(uint8 allow_entry,
                                         uint16 side_th,
                                         uint16 center_th,
                                         uint16 ad5_th,
                                         int hit_limit,
                                         int window_limit)
{
    uint8 weak_line;

    weak_line = (uint8)(allow_entry != 0 &&
                        ad1 <= side_th &&
                        ad2 <= center_th &&
                        ad3 <= center_th &&
                        ad4 <= side_th &&
                        ad5 < ad5_th);

    if (weak_line != 0)
    {
        /*
         * 只在首次进入弱磁窗口时检查速度门槛，窗口期间不再检查。
         * 原因：上板后速度自然下降，连续检查会导致窗口断裂。
         */
        if (entry_detect_count == 0)
        {
            /*
             * 0.6f 速度门槛：要求当前速度不低于目标速度的 60%，避免起步/低速段
             * 因电感信号天然偏弱而误判为入口。低速时弱磁是常态，不是上板。
             */
            if (!(speed_l > app.speed.speed_run * 0.6f &&
                  speed_r > app.speed.speed_run * 0.6f))
            {
                return 0;
            }
        }

        entry_detect_count++;
        if (entry_detect_count >= hit_limit)
        {
            entry_detect_count = 0;
            return 1;
        }
        if (entry_detect_count >= window_limit)
        {
            entry_detect_count = 0;
        }
    }
    else
    {
        entry_detect_count = 0;
    }

    return 0;
}

/**
 * @brief 读取停止等待状态机阶段。
 *
 * 返回 SEESAW_STATE_RELEASE 表示处于两种模式共用的释放期，
 * 元素仲裁据此决定是否跳过复位以保住阶梯增速。
 *
 * @return SeesawState 当前阶段。
 */
SeesawState a_run_seesaw_get_state(void)
{
    return seesaw_state;
}

/**
 * @brief 取出并清除跷跷板完成事件。
 *
 * 完成事件只允许元素仲裁消费一次，避免后续元素被同一次恢复确认重复触发。
 *
 * @return uint8 1-存在待消费完成事件，0-无事件。
 */
uint8 a_run_seesaw_take_finish_event(void)
{
    uint8 event = seesaw_finish_event;

    seesaw_finish_event = 0;

    return event;
}

/**
 * @brief 复位飞坡/跷跷板状态机内部计数并回到普通巡线。
 *
 * 两种模式互斥运行，共用同一个复位入口：清掉两套模式的计数、阶段、
 * 完成事件和所有共享标志。处于停车保持阶段时顺带解除 stop，
 * 避免复位后小车永久停在板上。
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
    fly_land_confirm_count = 0;
    entry_detect_count = 0;
    seesaw_brake_count = 0;
    seesaw_wait_count = 0;
    seesaw_creep_distance = 0.0f;
    seesaw_zero_brake_active = 0;
    seesaw_centering_active = 0;
    seesaw_lost_line_blocked = 0;
    seesaw_finish_event = 0;
    seesaw_pwm_output_limit = 0;
    seesaw_release_speed = 0.0f;
    seesaw_release_start_speed = 0.0f;
}

/**
 * @brief 跷跷板停止等待模式速度状态机。
 *
 * 检测到跷跷板后刹车、前挪并短暂保持，利用重力让跷跷板倾斜，
 * 电感信号恢复后置完成事件并进入 RELEASE 驻留态，阶梯增速由
 * a_run_seesaw_update_release_speed() 执行。
 *
 * @param speed 输出目标速度指针。
 * @param allow_entry 1-当前期望元素为跷跷板，允许从空闲态检测入口；0-禁止新入口。
 */
void a_run_seesaw_update_speed(float *speed, uint8 allow_entry)
{
    float creep_target;
    float creep_delta;

    creep_target = app.fly.seesaw_creep_cm;

    switch (seesaw_state)
    {
    case SEESAW_STATE_IDLE:
        /*
         * 普通赛道也可能出现单拍弱磁，跷跷板入口要求在 40ms 窗口内连续多次
         * 出现五路弱磁，累计达到设定次数后刹车。
         */
        if (a_run_fly_update_entry_gate(allow_entry,
                                        SEESAW_ENTRY_SIDE_TH,
                                        SEESAW_ENTRY_CENTER_TH,
                                        SEESAW_ENTRY_AD5_TH,
                                        app.fly.seesaw_detect_count,
                                        SEESAW_ENTRY_WINDOW_COUNT) != 0)
        {
            seesaw_brake_count = 0;
            pid_speed_reset(&PID.left_speed);
            pid_speed_reset(&PID.right_speed);
            *speed = 0.0f;
            seesaw_lost_line_blocked = 1;
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
        seesaw_zero_brake_active = 1;
        seesaw_brake_count++;
        if (seesaw_brake_count >= SEESAW_BRAKE_COUNT)
        {
            seesaw_brake_count = 0;
            seesaw_creep_distance = 0.0f;
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
            seesaw_creep_distance = 0.0f;
            seesaw_wait_count = 0;
            seesaw_state = SEESAW_STATE_HOLD_DELAY;
        }
        break;

    case SEESAW_STATE_HOLD_DELAY:
        /* 等待时间由菜单配置，单位为 2ms 主控制周期。 */
        *speed = 0.0f;
        if (seesaw_wait_count == 0)
        {
            stop = 1;
        }
        seesaw_wait_count++;
        if (seesaw_wait_count >= app.fly.seesaw_wait_count)
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
            seesaw_centering_active = 1;
            seesaw_release_speed = (float)app.fly.seesaw_speed;
            seesaw_release_start_speed = seesaw_release_speed;
            seesaw_finish_event = 1;
            seesaw_pwm_output_limit = FLY_PWM_LIMIT_RECOVER_START;
            seesaw_state = SEESAW_STATE_RELEASE;
        }
        break;

    case SEESAW_STATE_RELEASE:
        /* 释放期驻留态，阶梯增速由 a_run_seesaw_update_release_speed() 执行。 */
        break;
    }
}

/**
 * @brief 更新跷跷板完成后的阶梯增速。
 *
 * 完成事件只表示跷跷板本体可以切到序列中的下一个元素，不代表速度保护结束。
 * 因此该函数独立于当前元素运行，只要处于 RELEASE 就继续按主控制环周期释放速度。
 * 居中权重与 PWM 限幅在进入 RELEASE 的迁移点已置位，此处只推进速度。
 *
 * @param speed 输出的目标速度指针，保留小数速度设定。
 */
void a_run_seesaw_update_release_speed(float *speed)
{
    float target_speed;

    if (seesaw_state != SEESAW_STATE_RELEASE)
    {
        return;
    }

    /*
     * 落地回线后仍按斜坡释放速度。
     * 原因：负压、轮胎贴地和循迹误差都需要短暂恢复窗口，若完成事件后一拍
     * 回到巡线速度，后续无论接墙面、圆桶还是普通赛道都容易被惯性带偏。
     */
    target_speed = app.speed.speed_run;
    if (seesaw_release_speed >= target_speed)
    {
        *speed = target_speed;
        a_run_seesaw_reset();
        return;
    }

    *speed = seesaw_release_speed;
    if (app.fly.seesaw_mode == 0)
        seesaw_release_speed += app.fly.fly_release_step;
    else
        seesaw_release_speed += app.fly.seesaw_release_step;
    if (seesaw_release_speed > target_speed)
    {
        seesaw_release_speed = target_speed;
    }

    /* PWM 上限随释放速度线性爬升：起步 6000 留纠偏余量，爬到 speed_run 时回到全局限幅。 */
    if (seesaw_release_start_speed < target_speed)
    {
        float release_ratio = (seesaw_release_speed - seesaw_release_start_speed) /
                              (target_speed - seesaw_release_start_speed);
        if (release_ratio > 1.0f)
        {
            release_ratio = 1.0f;
        }
        seesaw_pwm_output_limit = (int32)(FLY_PWM_LIMIT_RECOVER_START +
            (FLY_PWM_LIMIT_RECOVER_END - FLY_PWM_LIMIT_RECOVER_START) * release_ratio);
    }
}

/**
 * @brief 飞坡模式本体速度状态机。
 *
 * 根据电感特征推进飞坡状态机，并在高风险阶段覆盖速度和转向输出。入口检测
 * 只在元素仲裁允许时开放；落地确认后进入 RELEASE，由
 * a_run_seesaw_update_release_speed() 继续完成阶梯增速，避免元素切换打断释放。
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
         * 飞坡入口要求短窗口内连续多次出现五路弱磁后切到 LOW；
         * 弱磁段按 fly_speed 低速通过，落地回升双路确认后进释放。
         */
        if (a_run_fly_update_entry_gate(allow_entry,
                                        SEESAW_ENTRY_SIDE_TH,
                                        SEESAW_ENTRY_CENTER_TH,
                                        SEESAW_ENTRY_AD5_TH,
                                        app.fly.fly_detect_count,
                                        FLY_ENTRY_WINDOW_COUNT) != 0)
        {
            seesaw_lost_line_blocked = 1;
            fly_state = FLY_STATE_LOW;
        }
        else
        {
            break;
        }
        /* 触发成立的同一控制周期立即降速，避免飞坡入口多放行一个主控制环周期。 */

    case FLY_STATE_LOW:
        /* 已落地进入释放期，速度由 a_run_seesaw_update_release_speed() 接管。 */
        if (seesaw_state == SEESAW_STATE_RELEASE)
        {
            break;
        }
        /*
         * 飞坡上低速通过，无 PWM 限制，转向保留循迹纠偏。
         * 不再尝试识别离地，只要后续出现连续落地回升信号，就直接进入恢复段。
         */
        *speed = (float)app.fly.fly_speed;
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
                 * 飞坡落地仍有前向滑行速度，若释放从 0 起步，
                 * 速度环会先给反向力矩去"刹停"，表现成小车短暂倒退。
                 * 因此这里直接从正的恢复速度起步，和停车模式保持同口径。
                 */
                seesaw_release_speed = (float)app.fly.fly_recover_speed;
                seesaw_release_start_speed = seesaw_release_speed;
                seesaw_finish_event = 1;
                seesaw_pwm_output_limit = FLY_PWM_LIMIT_RECOVER_START;
                seesaw_state = SEESAW_STATE_RELEASE;
            }
        }
        else
        {
            fly_land_confirm_count = 0;
        }
        break;
    }
}

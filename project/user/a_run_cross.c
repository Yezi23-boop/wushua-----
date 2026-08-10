/**
 * @file a_run_cross.c
 * @brief 双十字与单十字元素状态机。
 * @details
 * 两者入口电感判定完全相同（四路和 > 100），仅靠元素序列区分，
 * 共用同一套 WAIT_SIGNAL/TIMING 内核，各自维护独立实例和退出阈值；
 * 进入后只负责屏蔽后续元素识别，不改速度、不改转向，
 * 编码器积分到阈值后结束。
 */
#include "zf_common_headfile.h"
#include "a_run_cross.h"

#define CROSS_AD_SUM_THRESHOLD 100u   /* 十字类入口四路归一化电感和阈值 */
#define CROSS_SIGNAL_CONFIRM_COUNT 2u /* 电感和连续命中次数，2ms * 2 = 4ms */

/** @brief 十字类元素状态机实例，双十字与单十字各一份。 */
typedef struct
{
    CrossState state;    /**< 当前状态机阶段。 */
    uint16 signal_count; /**< 宽松电感条件连续命中次数。 */
    float encoder_sum;   /**< 编码器里程累计，单位沿用速度积分标尺 cm。 */
} CrossInstance;

static CrossInstance cross_inst = {CROSS_STATE_IDLE, 0, 0.0f};        /**< 双十字实例。 */
static CrossInstance cross_single_inst = {CROSS_STATE_IDLE, 0, 0.0f}; /**< 单十字实例。 */

/**
 * @brief 复位单个十字类状态机实例。
 * @param inst 目标实例指针。
 */
static void cross_instance_reset(CrossInstance *inst)
{
    inst->state = CROSS_STATE_IDLE;
    inst->signal_count = 0;
    inst->encoder_sum = 0.0f;
}

/**
 * @brief 十字类状态机共用内核。
 *
 * WAIT_SIGNAL 阶段检测四路电感和 > 100 连续命中后进入 TIMING；
 * TIMING 阶段累计编码器里程，达到 encoder_target 后完成。
 * 全程不覆盖 speed 和 angle_target。
 *
 * @param inst 目标实例指针。
 * @param encoder_target 本次退出里程阈值，双十字/单十字分别传入各自配置。
 * @return uint8 1-流程完成，0-仍在流程中。
 */
static uint8 cross_instance_update(CrossInstance *inst, float encoder_target)
{
    uint16 ad_sum;
    float creep_delta;

    switch (inst->state)
    {
    case CROSS_STATE_IDLE:
        inst->signal_count = 0;
        inst->encoder_sum = 0.0f;
        inst->state = CROSS_STATE_WAIT_SIGNAL;
        break;

    case CROSS_STATE_WAIT_SIGNAL:
        ad_sum = ad1 + ad2 + ad3 + ad4;
        if (ad_sum > CROSS_AD_SUM_THRESHOLD)
        {
            inst->signal_count++;
            if (inst->signal_count >= CROSS_SIGNAL_CONFIRM_COUNT)
            {
                inst->signal_count = 0;
                inst->encoder_sum = 0.0f;
                inst->state = CROSS_STATE_TIMING;
            }
        }
        else
        {
            inst->signal_count = 0;
        }
        break;

    case CROSS_STATE_TIMING:
        /* 0.012f：里程积分系数，由采样周期(2ms)和轮径/编码器标定共同决定，将速度值转为每周期行驶距离(cm)。 */
        creep_delta = (speed_l + speed_r) * 0.5f * 0.012f;
        inst->encoder_sum += creep_delta;
        if (inst->encoder_sum >= encoder_target)
        {
            cross_instance_reset(inst);
            return 1;
        }
        break;

    default:
        cross_instance_reset(inst);
        break;
    }

    return 0;
}

/* --- 双十字公开接口 --- */

/**
 * @brief 读取当前双十字状态机阶段。
 * @return CrossState 当前双十字状态。
 */
CrossState a_run_cross_get_state(void)
{
    return cross_inst.state;
}

/**
 * @brief 复位双十字状态机。
 */
void a_run_cross_reset(void)
{
    cross_instance_reset(&cross_inst);
}

/**
 * @brief 更新双十字识别/积分状态机，退出阈值取 app.cross.encoder_target。
 * @return uint8 1-双十字流程完成，0-仍在流程中。
 */
uint8 a_run_cross_update_5ms(void)
{
    return cross_instance_update(&cross_inst, app.cross.encoder_target);
}

/* --- 单十字公开接口 --- */

/**
 * @brief 读取当前单十字状态机阶段。
 * @return CrossSingleState 当前单十字状态。
 */
CrossSingleState a_run_cross_single_get_state(void)
{
    return cross_single_inst.state;
}

/**
 * @brief 复位单十字状态机。
 */
void a_run_cross_single_reset(void)
{
    cross_instance_reset(&cross_single_inst);
}

/**
 * @brief 更新单十字识别/积分状态机，退出阈值取 app.cross_single.encoder_target。
 * @return uint8 1-单十字流程完成，0-仍在流程中。
 */
uint8 a_run_cross_single_update_5ms(void)
{
    return cross_instance_update(&cross_single_inst, app.cross_single.encoder_target);
}

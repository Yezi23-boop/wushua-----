/**
 * @file a_run_cross.c
 * @brief 双十字元素状态机。
 * @details
 * 双十字使用墙面同款入口电感判定（四路和 > 100），进入后只负责
 * 屏蔽后续元素识别，不改速度、不改转向，编码器积分到阈值后结束。
 */
#include "zf_common_headfile.h"
#include "a_run_cross.h"

#define CROSS_AD_SUM_THRESHOLD 100u    /* 双十字入口四路归一化电感和阈值 */
#define CROSS_SIGNAL_CONFIRM_COUNT 2u  /* 电感和连续命中次数，2ms * 2 = 4ms */

enum CrossStep
{
    CROSS_IDLE = 0,
    CROSS_WAIT_SIGNAL = 1,
    CROSS_TIMING = 2
};

static enum CrossStep cross_state = CROSS_IDLE;
static uint16 cross_signal_count = 0;   /**< 宽松电感条件连续命中次数 */
static float cross_encoder_sum = 0.0f;  /**< 编码器里程累计，单位沿用速度积分标尺 cm */

/**
 * @brief 读取当前双十字状态机阶段。
 * @return int8 0-空闲，1-等电感强信号，2-编码器积分。
 */
int8 a_run_cross_get_state(void)
{
    return (int8)cross_state;
}

/**
 * @brief 复位双十字状态机。
 */
void a_run_cross_reset(void)
{
    cross_state = CROSS_IDLE;
    cross_signal_count = 0;
    cross_encoder_sum = 0.0f;
}

/**
 * @brief 更新双十字识别/积分状态机。
 *
 * WAIT_SIGNAL 阶段检测四路电感和 > 100 连续命中后进入 TIMING；
 * TIMING 阶段累计编码器里程，达到 app.cross.encoder_target 后完成。
 * 全程不覆盖 speed 和 angle_target。
 *
 * @return uint8 1-双十字流程完成，0-仍在流程中。
 */
uint8 a_run_cross_update_5ms(void)
{
    uint16 ad_sum;
    float creep_delta;

    switch (cross_state)
    {
    case CROSS_IDLE:
        cross_signal_count = 0;
        cross_encoder_sum = 0.0f;
        cross_state = CROSS_WAIT_SIGNAL;
        break;

    case CROSS_WAIT_SIGNAL:
        ad_sum = ad1 + ad2 + ad3 + ad4;
        if (ad_sum > CROSS_AD_SUM_THRESHOLD)
        {
            cross_signal_count++;
            if (cross_signal_count >= CROSS_SIGNAL_CONFIRM_COUNT)
            {
                cross_signal_count = 0;
                cross_encoder_sum = 0.0f;
                cross_state = CROSS_TIMING;
            }
        }
        else
        {
            cross_signal_count = 0;
        }
        break;

    case CROSS_TIMING:
        /* 0.012f：里程积分系数，由采样周期(2ms)和轮径/编码器标定共同决定，将速度值转为每周期行驶距离(cm)。 */
        creep_delta = (speed_l + speed_r) * 0.5f * 0.012f;
        cross_encoder_sum += creep_delta;
        if (cross_encoder_sum >= app.cross.encoder_target)
        {	
            a_run_cross_reset();
            return 1;
        }
        break;

    default:
        a_run_cross_reset();
        break;
    }

    return 0;
}

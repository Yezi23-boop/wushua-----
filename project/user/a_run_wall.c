/**
 * @file a_run_wall.c
 * @brief 墙面强信号等待与下墙计时状态机。
 */
#include "zf_common_headfile.h"
#include "a_run_wall.h"

#define WALL_AD_SUM_THRESHOLD 100u   /* 墙面入口四路归一化电感和阈值，超过后才允许推进墙面确认。 */
#define WALL_AD5_THRESHOLD 40u       /* 墙面专用传感器 ad5 强信号阈值，与四路和双条件确认墙面信号。 */
#define WALL_SIGNAL_CONFIRM_COUNT 2u /* 电感和连续命中次数，2ms * 2 = 4ms，用于过滤单拍毛刺。 */

static WallState wall_state = WALL_STATE_IDLE; /**< 墙面状态机阶段，圆桶/跷跷板完成后由 2ms 主环推进。 */
static uint16 wall_timer_count = 0;            /**< 墙面完整波形确认后的下墙计时，单位：2ms。 */
static uint16 wall_signal_count = 0;           /**< 宽松电感条件连续命中次数，断开即清零，避免离散毛刺累计。 */
static float wall_encoder_sum = 0.0f;          /**< 进入墙面后的编码器积分累计，达到阈值可提前结束墙面。 */

/**
 * @brief 读取当前墙面状态机阶段。
 * @return WallState 当前墙面状态。
 */
WallState a_run_wall_get_state(void)
{
    return wall_state;
}

/**
 * @brief 复位墙面状态机。
 *
 * 墙面只作为圆桶/跷跷板后的屏蔽确认段，复位时清掉计时状态。
 */
void a_run_wall_reset(void)
{
    wall_state = WALL_STATE_IDLE;
    wall_timer_count = 0;
    wall_signal_count = 0;
    wall_encoder_sum = 0.0f;
}

/**
 * @brief 按 2ms 主控制环周期更新墙面识别/下墙计时状态机。
 *
 * TIMING 全程按 app.wall.entry_speed 覆盖目标速度：低于 speed_run 减速、
 * 高于则加速，默认用于给负压建立吸力留出时间并稳住过墙速度；
 * 计时或累计编码器到阈值后完成。
 *
 * @param speed 输出目标速度指针；TIMING 全程会被墙面目标速度覆盖。
 * @return uint8 1-墙面流程完成，可重新开放下一元素；0-仍在墙面流程中。
 */
uint8 a_run_wall_update_2ms(float *speed)
{
    uint16 ad_sum;
    int entry_speed;
    int timing_count;

    entry_speed = app.wall.entry_speed;
    timing_count = app.wall.timing_count;

    if (entry_speed < 0)
    {
        entry_speed = 0;
    }
    if (timing_count < 1)
    {
        timing_count = 1;
    }

    ad_sum = ad1 + ad2 + ad3 + ad4;

    switch (wall_state)
    {
    case WALL_STATE_IDLE:
        wall_timer_count = 0;
        wall_signal_count = 0;
        wall_encoder_sum = 0.0f;
        wall_state = WALL_STATE_WAIT_SIGNAL;
        break;
    case WALL_STATE_WAIT_SIGNAL:
        // ad5 为墙面专用传感器，与四路和双条件确认墙面信号存在
        if (ad_sum > WALL_AD_SUM_THRESHOLD && ad5 > WALL_AD5_THRESHOLD)
        {
            wall_signal_count++;
            if (wall_signal_count >= WALL_SIGNAL_CONFIRM_COUNT)
            {
                wall_timer_count = 0;
                wall_signal_count = 0;
                wall_encoder_sum = 0.0f;
                wall_state = WALL_STATE_TIMING;
            }
        }
        else
        {
            wall_signal_count = 0;
        }
        break;

    case WALL_STATE_TIMING:
        wall_timer_count++;
        /* 0.012f：里程积分系数，由采样周期(2ms)和轮径/编码器标定共同决定，将速度值转为每周期行驶距离(cm)。 */
        wall_encoder_sum += (speed_l + speed_r) * 0.5f * 0.012f;
        /* 墙面全程按目标速度运行：低于 speed_run 减速、高于则加速。 */
        *speed = (float)entry_speed;
        if (wall_timer_count >= (uint16)timing_count ||
            wall_encoder_sum >= app.wall.encoder_target)
        {
            a_run_wall_reset();
            return 1;
        }
        break;

    default:
        a_run_wall_reset();
        break;
    }

    return 0;
}

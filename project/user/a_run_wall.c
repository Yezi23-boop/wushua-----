/**
 * @file a_run_wall.c
 * @brief 墙面强信号等待与下墙计时状态机。
 */
#include "zf_common_headfile.h"
#include "a_run_wall.h"

#define WALL_AD_SUM_THRESHOLD 100u     /* 墙面入口四路归一化电感和阈值，超过后才允许推进墙面确认。 */
#define WALL_SIGNAL_CONFIRM_COUNT 2u   /* 电感和连续命中次数，2ms * 2 = 4ms，用于过滤单拍毛刺。 */

enum WallStep
{
    WALL_IDLE = 0,
    WALL_WAIT_SIGNAL = 1,
    WALL_TIMING = 2
};

static enum WallStep wall_state = WALL_IDLE; /**< 墙面状态机阶段，圆桶/跷跷板完成后由 2ms 主环推进。 */
static uint16 wall_timer_count = 0;          /**< 墙面完整波形确认后的下墙计时，单位：2ms。 */
static uint16 wall_slow_count = 0;           /**< 降速阶段计时，单位：2ms。 */
static uint16 wall_signal_count = 0;         /**< 宽松电感条件连续命中次数，断开即清零，避免离散毛刺累计。 */
static float wall_encoder_sum = 0.0f;        /**< 进入墙面后的编码器积分累计，达到阈值可提前结束墙面。 */

/**
 * @brief 读取当前墙面状态机阶段。
 * @return int8 0-空闲，1-等墙面强信号，2-下墙计时。
 */
int8 a_run_wall_get_state(void)
{
    return (int8)wall_state;
}

/**
 * @brief 复位墙面状态机。
 *
 * 墙面只作为圆桶/跷跷板后的屏蔽确认段，复位时清掉计时状态。
 */
void a_run_wall_reset(void)
{
    wall_state = WALL_IDLE;
    wall_timer_count = 0;
    wall_slow_count = 0;
    wall_signal_count = 0;
    wall_encoder_sum = 0.0f;
}

/**
 * @brief 更新墙面识别/下墙计时状态机。
 *
 * TIMING 前段按 app.wall.slow_speed 降速，让负压有时间安稳吸住车身；
 * 后段恢复正常控制，继续计时或累计编码器到阈值后完成。
 *
 * @param speed 输出目标速度指针；TIMING 前段会被降速值覆盖。
 * @return uint8 1-墙面流程完成，可重新开放下一元素；0-仍在墙面流程中。
 */
uint8 a_run_wall_update_5ms(float *speed)
{
    uint16 ad_sum;
    int slow_speed;
    int slow_time;
    int timing_count;

    slow_speed = app.wall.slow_speed;
    slow_time = app.wall.slow_time;
    timing_count = app.wall.timing_count;

    if (slow_speed < 0)
    {
        slow_speed = 0;
    }
    if (slow_time < 0)
    {
        slow_time = 0;
    }
    if (timing_count < 1)
    {
        timing_count = 1;
    }

    ad_sum = ad1 + ad2 + ad3 + ad4;

    switch (wall_state)
    {
    case WALL_IDLE:
        wall_timer_count = 0;
        wall_slow_count = 0;
        wall_signal_count = 0;
        wall_encoder_sum = 0.0f;
        wall_state = WALL_WAIT_SIGNAL;
        break;
    case WALL_WAIT_SIGNAL:
        if (ad_sum > WALL_AD_SUM_THRESHOLD)
        {
            wall_signal_count++;
            if (wall_signal_count >= WALL_SIGNAL_CONFIRM_COUNT)
            {
                wall_timer_count = 0;
                wall_slow_count = 0;
                wall_signal_count = 0;
                wall_encoder_sum = 0.0f;
                wall_state = WALL_TIMING;
            }
        }
        else
        {
            wall_signal_count = 0;
        }
        break;

    case WALL_TIMING:
        wall_timer_count++;
        /* 0.012f：里程积分系数，由采样周期(2ms)和轮径/编码器标定共同决定，将速度值转为每周期行驶距离(cm)。 */
        wall_encoder_sum += (speed_l + speed_r) * 0.5f * 0.012f;
        if (wall_slow_count < (uint16)slow_time)
        {
            wall_slow_count++;
            *speed = (float)slow_speed;
        }
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

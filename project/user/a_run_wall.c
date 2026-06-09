/**
 * @file a_run_wall.c
 * @brief 墙面强信号等待与下墙计时状态机。
 */
#include "zf_common_headfile.h"
#include "a_run_wall.h"

#define WALL_AD_SIDE_THRESHOLD 35 /* 墙面横向有效阈值，ad1/ad4 同时超过才允许推进墙面波形。 */
#define WALL_AD_HIGH_THRESHOLD 55 /* 墙面纵向高值阈值，ad2/ad3 任一路超过该值认为到达上墙峰值。 */
#define WALL_TIMING_COUNT 500u    /* 墙面强信号确认后的下墙计时，2ms * 500 = 1000ms。 */

enum WallStep
{
    WALL_IDLE = 0,
    WALL_WAIT_SIGNAL = 1,
    WALL_TIMING = 2
};

static enum WallStep wall_state = WALL_IDLE; /**< 墙面状态机阶段，圆桶/跷跷板完成后由 2ms 主环推进。 */
static uint16 wall_timer_count = 0;          /**< 墙面完整波形确认后的下墙计时，单位：2ms。 */

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
}

/**
 * @brief 更新墙面识别/下墙计时状态机。
 * @return uint8 1-墙面流程完成，可重新开放下一元素；0-仍在墙面流程中。
 */
uint8 a_run_wall_update_5ms(void)
{
    uint8 side_valid = 0;
    uint8 high_valid = 0;

    if (ad1 > WALL_AD_SIDE_THRESHOLD && ad4 > WALL_AD_SIDE_THRESHOLD)
    {
        side_valid = 1;
    }
    if (ad2 > WALL_AD_HIGH_THRESHOLD || ad3 > WALL_AD_HIGH_THRESHOLD)
    {
        high_valid = 1;
    }

    switch (wall_state)
    {
    case WALL_IDLE:
        wall_timer_count = 0;
        wall_state = WALL_WAIT_SIGNAL;
        break;
    case WALL_WAIT_SIGNAL:
        if (side_valid != 0 && high_valid != 0)
        {
            wall_timer_count = 0;
            wall_state = WALL_TIMING;
        }
        break;

    case WALL_TIMING:
        wall_timer_count++;
        if (wall_timer_count >= WALL_TIMING_COUNT)
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

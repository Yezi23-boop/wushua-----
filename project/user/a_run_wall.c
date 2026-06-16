/**
 * @file a_run_wall.c
 * @brief 墙面强信号等待与下墙计时状态机。
 */
#include "zf_common_headfile.h"
#include "a_run_wall.h"

#define WALL_AD_SIDE_THRESHOLD 30 /* 墙面横向有效阈值，ad1/ad4 同时超过才允许推进墙面波形。 */
#define WALL_AD_HIGH_THRESHOLD 50 /* 墙面纵向高值阈值，ad2/ad3 任一路超过该值认为到达上墙峰值。 */

enum WallStep
{
    WALL_IDLE = 0,
    WALL_WAIT_SIGNAL = 1,
    WALL_TIMING = 2
};

static enum WallStep wall_state = WALL_IDLE; /**< 墙面状态机阶段，圆桶/跷跷板完成后由 2ms 主环推进。 */
static uint16 wall_timer_count = 0;          /**< 墙面完整波形确认后的下墙计时，单位：2ms。 */
static uint16 wall_slow_count = 0;           /**< 降速阶段计时，单位：2ms。 */

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
}

/**
 * @brief 更新墙面识别/下墙计时状态机。
 *
 * TIMING 前段按 app.wall.slow_speed 降速，让负压有时间安稳吸住车身；
 * 后段恢复正常控制，继续计时到 app.wall.timing_count 后完成。
 *
 * @param speed 输出目标速度指针；TIMING 前段会被降速值覆盖。
 * @return uint8 1-墙面流程完成，可重新开放下一元素；0-仍在墙面流程中。
 */
uint8 a_run_wall_update_5ms(float *speed)
{
    uint8 side_valid = 0;
    uint8 high_valid = 0;
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
//					stop=1;
            wall_timer_count = 0;
            wall_state = WALL_TIMING;
        }
        break;

    case WALL_TIMING:
        wall_timer_count++;
        if (wall_slow_count < (uint16)slow_time)
        {
            wall_slow_count++;
            *speed = (float)slow_speed;
        }
        if (wall_timer_count >= (uint16)timing_count)
        {
	//						stop=1;
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

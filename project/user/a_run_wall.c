/**
 * @file a_run_wall.c
 * @brief 墙面强信号等待与下墙计时状态机。
 */
#include "zf_common_headfile.h"
#include "a_run_wall.h"

#define WALL_AD_SIDE_THRESHOLD 30 /* 墙面横向有效阈值，ad1/ad4 同时超过才允许推进墙面波形。 */
#define WALL_AD_HIGH_THRESHOLD 30 /* 墙面纵向高值阈值，ad2/ad3 任一路超过该值认为到达上墙峰值。 */
#define WALL_UPDATE_PERIOD_MS 2u           /* 墙面状态机跟随 2ms 主控制环更新。 */
#define WALL_SIGNAL_CONFIRM_COUNT 2u       /* 电感宽松条件连续命中次数，2ms * 2 = 4ms，用于过滤单拍毛刺。 */
#define WALL_PITCH_CONFIRM_MS IMU_PITCH_WALL_WINDOW_MS /* pitch 二级确认窗口，和历史基准窗口共用现场调参值。 */
#define WALL_PITCH_CONFIRM_COUNT (WALL_PITCH_CONFIRM_MS / WALL_UPDATE_PERIOD_MS)
#define WALL_PITCH_DELTA_X10_THRESHOLD 200 /* 上墙姿态变化阈值，单位 0.1 度；200 表示 20.0 度。 */
#define WALL_PITCH_WRAP_HALF_X10 1800      /* 角度环绕半圈，单位 0.1 度。 */
#define WALL_PITCH_WRAP_FULL_X10 3600      /* 角度环绕整圈，单位 0.1 度。 */

enum WallStep
{
    WALL_IDLE = 0,
    WALL_WAIT_SIGNAL = 1,
    WALL_PITCH_CONFIRM = 2,
    WALL_TIMING = 3
};

static enum WallStep wall_state = WALL_IDLE; /**< 墙面状态机阶段，圆桶/跷跷板完成后由 2ms 主环推进。 */
static uint16 wall_timer_count = 0;          /**< 墙面完整波形确认后的下墙计时，单位：2ms。 */
static uint16 wall_slow_count = 0;           /**< 降速阶段计时，单位：2ms。 */
static uint16 wall_signal_count = 0;         /**< 宽松电感条件连续命中次数，断开即清零，避免离散毛刺累计。 */
static uint16 wall_pitch_confirm_count = 0;  /**< pitch 二级确认耗时，单位：2ms，超过窗口则回到等待电感。 */
static int16 wall_pitch_before_x10 = 0;      /**< 电感连续命中时锁存的窗口前 pitch，单位 0.1 度。 */

static int16 wall_pitch_delta_abs_x10(int16 now_x10, int16 before_x10);

/**
 * @brief 计算 pitch 的环绕最短角差。
 * @param now_x10 当前 pitch，单位 0.1 度。
 * @param before_x10 基准 pitch，单位 0.1 度。
 * @return int16 两个角度之间的最短绝对差值，单位 0.1 度。
 */
static int16 wall_pitch_delta_abs_x10(int16 now_x10, int16 before_x10)
{
    int16 delta;

    delta = now_x10 - before_x10;
    if (delta > WALL_PITCH_WRAP_HALF_X10)
    {
        delta -= WALL_PITCH_WRAP_FULL_X10;
    }
    else if (delta < -WALL_PITCH_WRAP_HALF_X10)
    {
        delta += WALL_PITCH_WRAP_FULL_X10;
    }

    if (delta < 0)
    {
        delta = -delta;
    }

    return delta;
}

/**
 * @brief 读取当前墙面状态机阶段。
 * @return int8 0-空闲，1-等墙面强信号，2-pitch 二级确认，3-下墙计时。
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
    wall_pitch_confirm_count = 0;
    wall_pitch_before_x10 = 0;
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
    int16 pitch_delta_x10;

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
        wall_slow_count = 0;
        wall_signal_count = 0;
        wall_pitch_confirm_count = 0;
        wall_state = WALL_WAIT_SIGNAL;
        break;
    case WALL_WAIT_SIGNAL:
        if (side_valid != 0 && high_valid != 0)
        {
            wall_signal_count++;
            if (wall_signal_count >= WALL_SIGNAL_CONFIRM_COUNT)
            {
                wall_pitch_before_x10 = imu_get_pitch_wall_window_ago_x10();
                wall_pitch_confirm_count = 0;
                wall_state = WALL_PITCH_CONFIRM;
            }
        }
        else
        {
            wall_signal_count = 0;
        }
        break;

    case WALL_PITCH_CONFIRM:
        /*
         * 确认期只判断姿态，不降速、不启动下墙计时。
         * 原因：普通循迹的强磁误候选不能影响速度输出，只有 pitch 变化达到上墙特征后才进入墙面控制。
         */
        pitch_delta_x10 = wall_pitch_delta_abs_x10(imu_get_pitch_current_x10(), wall_pitch_before_x10);
        if (pitch_delta_x10 >= WALL_PITCH_DELTA_X10_THRESHOLD)
        {
            wall_timer_count = 0;
            wall_slow_count = 0;
            wall_signal_count = 0;
            wall_state = WALL_TIMING;
            break;
        }

        wall_pitch_confirm_count++;
        if (wall_pitch_confirm_count >= WALL_PITCH_CONFIRM_COUNT)
        {
            wall_signal_count = 0;
            wall_pitch_confirm_count = 0;
            wall_state = WALL_WAIT_SIGNAL;
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

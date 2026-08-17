#ifndef __A_RUN_WALL_H__
#define __A_RUN_WALL_H__

#include "a_run_track_element.h"

/**
 * @brief 墙面状态机阶段。
 */
typedef enum
{
    WALL_STATE_IDLE = 0,        /**< 空闲状态。 */
    WALL_STATE_WAIT_SIGNAL = 1, /**< 等待墙面强电感信号。 */
    WALL_STATE_TIMING = 2       /**< 墙面计时与编码器积分阶段。 */
} WallState;

/**
 * @brief 复位墙面状态机。
 */
void a_run_wall_reset(void);

/**
 * @brief 按 2ms 主控制环周期更新墙面状态机。
 *
 * 电感和连续命中后进入 TIMING；TIMING 前段会覆盖速度为低速值，
 * 让负压有时间安稳吸住车身。
 *
 * @param speed 输出目标速度指针；TIMING 前段会被降速值覆盖。
 * @return uint8 1-墙面流程完成，0-未完成。
 */
uint8 a_run_wall_update_2ms(float *speed);

/**
 * @brief 读取墙面状态。
 * @return WallState 当前墙面状态。
 */
WallState a_run_wall_get_state(void);

#endif /* __A_RUN_WALL_H__ */

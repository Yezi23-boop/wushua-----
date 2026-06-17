#ifndef __A_RUN_WALL_H__
#define __A_RUN_WALL_H__

#include "a_run_track_element.h"

/**
 * @brief 复位墙面状态机。
 */
void a_run_wall_reset(void);

/**
 * @brief 按主控制环周期更新墙面状态机。
 *
 * 电感宽松条件连续命中后先进入 pitch 二级确认；确认期不改速度，只有俯仰角短时变化达标后才进入 TIMING。
 * TIMING 前段会覆盖速度为低速值并限制 PWM，让负压有时间安稳吸住车身。
 *
 * @param speed 输出目标速度指针；TIMING 前段会被降速值覆盖。
 * @return uint8 1-墙面流程完成，0-未完成。
 */
uint8 a_run_wall_update_5ms(float *speed);

/**
 * @brief 读取墙面状态。
 * @return int8 0-空闲，1-等电感强信号，2-pitch 二级确认，3-下墙计时。
 */
int8 a_run_wall_get_state(void);

#endif /* __A_RUN_WALL_H__ */

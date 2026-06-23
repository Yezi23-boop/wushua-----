#ifndef __A_RUN_CROSS_H__
#define __A_RUN_CROSS_H__

#include "a_run_track_element.h"

/**
 * @brief 复位双十字状态机。
 */
void a_run_cross_reset(void);

/**
 * @brief 按主控制环周期更新双十字状态机。
 *
 * 电感和连续命中后进入 TIMING；TIMING 阶段累计编码器里程，
 * 达到阈值后完成。不覆盖速度和转向目标。
 *
 * @return uint8 1-双十字流程完成，0-未完成。
 */
uint8 a_run_cross_update_5ms(void);

/**
 * @brief 读取双十字状态。
 * @return int8 0-空闲，1-等电感强信号，2-编码器积分。
 */
int8 a_run_cross_get_state(void);

#endif /* __A_RUN_CROSS_H__ */

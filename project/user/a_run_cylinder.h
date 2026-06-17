#ifndef __A_RUN_CYLINDER_H__
#define __A_RUN_CYLINDER_H__

#include "a_run_track_element.h"

#define A_RUN_CYLINDER_STATE_IDLE 0        /* 圆桶状态：空闲或尚未进入圆桶确认流程。 */
#define A_RUN_CYLINDER_STATE_WAIT_TOP 1    /* 圆桶状态：等待顶部强信号窗口确认。 */
#define A_RUN_CYLINDER_STATE_WAIT_GROUND 2 /* 圆桶状态：已确认圆桶，正在按编码器积分等待退出。 */

/**
 * @brief 复位圆桶状态机。
 */
void a_run_cylinder_reset(void);

/**
 * @brief 按主控制环周期更新圆桶状态机。
 * @return uint8 1-圆桶流程完成，0-未完成。
 */
uint8 a_run_cylinder_update_5ms(void);

/**
 * @brief 读取圆桶状态。
 * @return int8 状态编号，见 A_RUN_CYLINDER_STATE_xxx。
 */
int8 a_run_cylinder_get_state(void);

#endif /* __A_RUN_CYLINDER_H__ */

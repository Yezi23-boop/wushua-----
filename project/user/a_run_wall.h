#ifndef __A_RUN_WALL_H__
#define __A_RUN_WALL_H__

#include "a_run_track_element.h"

/**
 * @brief 复位墙面状态机。
 */
void a_run_wall_reset(void);

/**
 * @brief 5ms 更新墙面状态机。
 * @return uint8 1-墙面流程完成，0-未完成。
 */
uint8 a_run_wall_update_5ms(void);

/**
 * @brief 读取墙面状态。
 * @return int8 状态编号。
 */
int8 a_run_wall_get_state(void);

#endif /* __A_RUN_WALL_H__ */

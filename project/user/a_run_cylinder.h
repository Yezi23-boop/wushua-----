#ifndef __A_RUN_CYLINDER_H__
#define __A_RUN_CYLINDER_H__

#include "a_run_track_element.h"

/**
 * @brief 复位圆桶状态机。
 */
void a_run_cylinder_reset(void);

/**
 * @brief 5ms 更新圆桶状态机。
 * @return uint8 1-圆桶流程完成，0-未完成。
 */
uint8 a_run_cylinder_update_5ms(void);

/**
 * @brief 读取圆桶状态。
 * @return int8 状态编号。
 */
int8 a_run_cylinder_get_state(void);

#endif /* __A_RUN_CYLINDER_H__ */

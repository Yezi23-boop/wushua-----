#ifndef __A_RUN_MODE_H__
#define __A_RUN_MODE_H__

#include "zf_common_typedef.h"

/**
 * @brief 更新启动状态机
 * @details 检测特定按键（P35），控制小车的准备与运行切换
 */
void a_run_mode_update_start_state(void);

/**
 * @brief 更新负压系统工作状态
 */
void a_run_mode_update_fuya_state(void);

/**
 * @brief 更新飞坡慢速处理策略
 * @param speed 输出：计算后的当前期望速度
 */
void a_run_mode_update_fly_speed(int *speed);

#endif /* __A_RUN_MODE_H__ */

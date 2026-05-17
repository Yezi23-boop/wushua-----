#ifndef __A_RUN_MODE_H__
#define __A_RUN_MODE_H__

#include "zf_common_typedef.h"

/**
 * @brief 启动状态机（10ms定时运行，只更新内部状态）
 * @details 检测 P36 启动按键，每次有效短按切换状态。
 *          第二次按键后会先点亮 P43 约 1s，之后才真正进入运行态 2。
 */
void a_run_mode_update_start_state(void);

/**
 * @brief 读取当前启动状态
 * @return int8 当前状态值：0-未启动，1-状态1，2-状态2
 */
int8 a_run_mode_get_start_state(void);

/**
 * @brief 负压状态更新
 * @details 仅在启动状态有效且配置允许时执行负压控制，避免待机时误动作
 */
void a_run_mode_update_fuya_state(void);

#endif

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

/**
 * @brief 读取当前环岛状态机阶段，用于菜单调参显示。
 * @return int8 阶段编号：0-no_ring，1-ring，2-pre_ring，3-in_ring，4-pre_out_ring，5-out_ring。
 */
int8 a_run_mode_get_ring_state(void);

/**
 * @brief 读取当前期望赛道元素。
 * @return int8 0-无，1-左圆环，2-右圆环，3-圆桶，4-墙面，5-跷跷板。
 */
int8 a_run_mode_get_expected_element(void);

/**
 * @brief 读取当前圆桶状态机阶段。
 * @return int8 0-空闲，1-等顶部，2-等回平，3-稳定延迟。
 */
int8 a_run_mode_get_cylinder_state(void);

/**
 * @brief 读取当前墙面状态机阶段。
 * @return int8 0-空闲，1-等墙面强信号，2-下墙计时。
 */
int8 a_run_mode_get_wall_state(void);

/**
 * @brief 读取圆桶判断使用的 roll 角差。
 * @return float roll 角差，单位：度，范围 -180~180。
 */
float a_run_mode_get_cylinder_vz(void);

#endif

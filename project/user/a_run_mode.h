#ifndef __A_RUN_MODE_H__
#define __A_RUN_MODE_H__

#include "zf_common_typedef.h"

/**
 * @brief 启动状态机（10ms定时运行，只更新内部状态）
 * @details 检测P36引脚状态，每次按键切换状态：初始0 -> 第1次按1 -> 第2次按2 -> 第3次按1...
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
 * @brief 飞坡速度修正
 * @details 根据四路电感特征判断是否进入飞坡阶段，并在飞坡期间覆盖速度和目标角速度输出。
 *          该接口设计为 5ms 主环调用，飞坡触发与保持计时均按 5ms 标尺生效。
 * @param speed 输出的目标速度指针
 */
void a_run_mode_update_fly_speed(int *speed);

/**
 * @brief 更新赛道元素仲裁状态机
 * @details 5ms 调用，根据当前期望元素开放左圆环或圆筒识别。
 */
void a_run_mode_update_track_element_gate(void);

/**
 * @brief 根据环岛状态更新角速度目标
 * @param angle_target 指向目标角速度的指针
 */
void run_mode_update_angle_target(float *angle_target);

/**
 * @brief 读取当前环岛状态机阶段，用于菜单调参显示。
 * @return int8 阶段编号：0-no_ring，1-ring，2-pre_ring，3-in_ring，4-pre_out_ring，5-out_ring。
 */
int8 a_run_mode_get_ring_state(void);

/**
 * @brief 读取当前期望赛道元素。
 * @return int8 0-无，1-左圆环，2-右圆环，3-圆桶。
 */
int8 a_run_mode_get_expected_element(void);

/**
 * @brief 读取当前圆筒状态机阶段。
 * @return int8 0-空闲，1-等顶部，2-等回平，3-稳定延迟。
 */
int8 a_run_mode_get_cylinder_state(void);

/**
 * @brief 读取圆桶判断使用的重力向量 vz 滤波值。
 * @return float 滤波后的 vz，来自四元数解算。
 */
float a_run_mode_get_cylinder_vz(void);

/**
 * @brief 更新环岛里程累计与偏航角增量。
 *
 * 环岛模块内部按速度估计累计里程，并使用前后两次 yaw 的差值进行增量累加。
 * 这种方式可避免跨 0 点跳变，累加结果为实际转过的总角度量。
 *
 * @note 该函数依赖进入 `pre_ring` 时已经正确记录 `last_yaw`，由 5ms 主控制环调用。
 */
void gyro_integrals(void);

#endif

#ifndef __A_RUN_CYLINDER_H__
#define __A_RUN_CYLINDER_H__

#include "a_run_track_element.h"

/**
 * @brief 圆桶状态机阶段。
 */
typedef enum
{
    CYLINDER_STATE_IDLE = 0,        /**< 空闲或尚未进入圆桶确认流程。 */
    CYLINDER_STATE_WAIT_TOP = 1,    /**< 等待顶部强信号窗口确认。 */
    CYLINDER_STATE_WAIT_GROUND = 2, /**< 已确认圆桶，按编码器积分通过。 */
    CYLINDER_STATE_EXIT_SLOW = 3,   /**< 距离出口较近，阶梯减速到退出阈值。 */
    CYLINDER_STATE_RELEASE = 4      /**< 圆桶完成后后台阶梯恢复巡线速度。 */
} CylinderState;

/**
 * @brief 复位圆桶状态机。
 */
void a_run_cylinder_reset(void);

/**
 * @brief 按主控制环周期更新圆桶状态机。
 * @param speed 当前目标速度指针，出圆桶减速阶段会覆盖该值。
 * @return uint8 1-圆桶流程完成，0-未完成。
 */
uint8 a_run_cylinder_update_5ms(float *speed);

/**
 * @brief 更新圆桶完成后的后台阶梯加速。
 * @param speed 当前目标速度指针，后续元素仍可覆盖该值。
 */
void a_run_cylinder_update_release_speed(float *speed);

/**
 * @brief 读取圆桶状态。
 * @return CylinderState 当前圆桶状态。
 */
CylinderState a_run_cylinder_get_state(void);

#endif /* __A_RUN_CYLINDER_H__ */

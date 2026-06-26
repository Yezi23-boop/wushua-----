#ifndef __A_RUN_TRACK_ELEMENT_H__
#define __A_RUN_TRACK_ELEMENT_H__

#include "zf_common_typedef.h"

/* 元素编号与默认序列常量统一收拢在这里，避免再多一层薄配置头。 */
#define TRACK_ELEMENT_NONE 0       /**< 空槽位，用于跳过或现场临时关闭某个序列位置。 */
#define TRACK_ELEMENT_LEFT_RING 1  /**< 左圆环流程显示值。 */
#define TRACK_ELEMENT_RIGHT_RING 2 /**< 右圆环流程显示值。 */
#define TRACK_ELEMENT_CYLINDER 3   /**< 圆桶流程显示值。 */
#define TRACK_ELEMENT_WALL 4       /**< 墙面流程显示值。 */
#define TRACK_ELEMENT_SEESAW 5     /**< 跷跷板流程显示值。 */
#define TRACK_ELEMENT_CROSS 6      /**< 双十字流程显示值。 */

#define TRACK_ELEMENT_SEQUENCE_MAX 6 /**< 菜单和 EEPROM 固定保存的最大元素槽位数。 */
#define TRACK_ELEMENT_DEFAULT_LEN 5  /**< 默认有效长度：圆桶、墙面、跷跷板、双十字、左环。 */

/**
 * @brief 更新赛道元素仲裁状态机。
 *
 * 2ms 主控制链路直接调用该接口，避免高频路径多一层只转发的包装。
 *
 * @param speed 当前目标速度指针，保留小数速度设定；跷跷板流程和完成后阶梯增速会按阶段覆盖。
 * @param angle_target 当前目标角速度指针，圆环和跷跷板流程会按阶段覆盖。
 */
void a_run_track_element_update_gate(float *speed, float *angle_target);

/**
 * @brief 读取当前期望赛道元素。
 * @return int8 0-无，1-左圆环，2-右圆环，3-圆桶，4-墙面，5-跷跷板，6-双十字。
 */
int8 a_run_track_element_get_expected_element(void);

#endif /* __A_RUN_TRACK_ELEMENT_H__ */

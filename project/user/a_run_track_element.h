#ifndef __A_RUN_TRACK_ELEMENT_H__
#define __A_RUN_TRACK_ELEMENT_H__

#include "zf_common_typedef.h"

/* 元素编号常量统一收拢在这里，按编号升序排列，避免再多一层薄配置头。 */
#define TRACK_ELEMENT_NONE 0               /**< 空槽位，用于跳过或现场临时关闭某个序列位置。 */
#define TRACK_ELEMENT_LEFT_RING 1          /**< 左圆环元素编号，接入序列表串行仲裁。 */
#define TRACK_ELEMENT_RIGHT_RING 2         /**< 右圆环元素编号，复用圆环状态机并反向控制。 */
#define TRACK_ELEMENT_LARGE_RING_LEFT 3    /**< 大圆环左元素编号，复用圆环状态机，参数取大圆环组。 */
#define TRACK_ELEMENT_LARGE_RING_RIGHT 4   /**< 大圆环右元素编号，复用圆环状态机，参数取大圆环组。 */
#define TRACK_ELEMENT_CYLINDER 5           /**< 圆桶元素编号。 */
#define TRACK_ELEMENT_WALL 6               /**< 墙面元素编号。 */
#define TRACK_ELEMENT_SEESAW 7             /**< 跷跷板元素编号，复用 a_run_fly 的弱磁/恢复状态机。 */
#define TRACK_ELEMENT_DOUBLE_CROSS 8       /**< 双十字元素编号，电感和命中后编码器积分退出。 */
#define TRACK_ELEMENT_SINGLE_CROSS 9       /**< 单十字元素编号，入口判定与双十字相同，仅序列区分。 */

#define TRACK_ELEMENT_SEQUENCE_MAX 8 /**< 菜单和 EEPROM 固定保存的最大元素槽位数。 */
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
 * @brief 按当前激活元素覆盖转向环参数，无元素激活时不写指针。
 * @details
 * 调用方先把三个指针置为全局默认值，本函数只做元素覆盖；
 * 优先级：单十字TIMING > 双十字TIMING > 圆桶DECEL > 圆环有效阶段。
 *
 * @param kp 转向环比例系数指针。
 * @param kd 转向环微分系数指针。
 * @param kp2 转向环非线性增强系数指针。
 */
void a_run_track_element_apply_steer_params(float *kp, float *kd, float *kp2);

/**
 * @brief 按当前激活元素覆盖电感解算ABC权重，无元素激活时不写指针。
 * @details
 * 调用方先把三个指针置为全局默认值，本函数只做元素覆盖；
 * 顺序覆盖后写生效，优先级：圆环有效阶段 > 单十字TIMING > 双十字TIMING >
 * 跷跷板居中 > 圆桶DECEL。
 *
 * @param a_value 横向主差分权重指针。
 * @param b_value 竖向差分权重指针。
 * @param c_value 分母补偿权重指针。
 */
void a_run_track_element_apply_adc_params(float *a_value, float *b_value, float *c_value);

/**
 * @brief 读取当前期望赛道元素。
 * @return int8 0-无，1-左圆环，2-右圆环，3-大圆环左，4-大圆环右，
 *              5-圆桶，6-墙面，7-跷跷板，8-双十字，9-单十字。
 */
int8 a_run_track_element_get_expected_element(void);

#endif /* __A_RUN_TRACK_ELEMENT_H__ */

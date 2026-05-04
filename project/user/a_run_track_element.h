#ifndef __A_RUN_TRACK_ELEMENT_H__
#define __A_RUN_TRACK_ELEMENT_H__

#include "zf_common_typedef.h"

/**
 * @brief 环岛状态数据。
 * @details 保存环岛识别过程中用到的里程累计值、相对偏航角、阶段标志和计时器。
 */
typedef struct
{
    float encoder;        // 环岛阶段的里程累计量，用于出入环距离判定
    float yaw_delta_sum;  /**< 相对入环起点累计 yaw 角增量，单位为度；不是 gyro_z 角速度。 */
    float last_yaw;       // 上一次的偏航角，用于计算增量
    float diff_set;       // 环岛阶段固定目标角速度，非 0 时覆盖普通循迹目标
    int8 distance;        // 编码器累计使能：1-累计，0-停止累计
    uint32 time_r;        // 右环识别计时器
    uint32 time_l;        // 左环识别计时器
    int8 gyro_flat;       // 相对偏航角更新使能：1-更新，0-停止更新
    int8 flast_l;         // 左环过程标志
    int8 flast_r;         // 右环过程标志
    uint32 ing_ring_time; // 入环阶段确认计时
    uint32 out_ring_time; // 出环阶段确认计时
} RingStruct;

/** 环岛过程数据，菜单和调试界面允许直接读取。 */
extern RingStruct ring_data;

/**
 * @brief 更新赛道元素仲裁状态机。
 *
 * 该接口只供 a_run_mode 统一调配层调用，外部业务仍通过
 * a_run_mode_update_track_element_gate 访问。
 */
void a_run_track_element_update_gate(void);

/**
 * @brief 根据环岛状态更新角速度目标。
 *
 * @param angle_target 指向目标角速度的指针，环岛固定转向阶段会被覆盖。
 */
void a_run_track_element_update_angle_target(float *angle_target);

/**
 * @brief 读取当前环岛状态机阶段。
 * @return int8 0-no_ring，1-ring，2-pre_ring，3-in_ring，4-pre_out_ring，5-out_ring。
 */
int8 a_run_track_element_get_ring_state(void);

/**
 * @brief 读取当前期望赛道元素。
 * @return int8 0-无，1-左圆环，2-右圆环，3-圆桶。
 */
int8 a_run_track_element_get_expected_element(void);

/**
 * @brief 读取当前圆筒状态机阶段。
 * @return int8 0-空闲，1-等顶部，2-等回平，3-稳定延迟。
 */
int8 a_run_track_element_get_cylinder_state(void);

/**
 * @brief 读取圆桶判断使用的重力向量 vz 滤波值。
 * @return float 滤波后的 vz，来自四元数解算。
 */
float a_run_track_element_get_cylinder_vz(void);

/**
 * @brief 更新环岛里程累计与偏航角增量。
 *
 * 该接口只供 a_run_mode 统一调配层转发，保持原 gyro_integrals 对外名称不变。
 */
void a_run_track_element_update_integrals(void);

#endif /* __A_RUN_TRACK_ELEMENT_H__ */

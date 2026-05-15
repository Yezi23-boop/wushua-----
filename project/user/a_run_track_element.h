#ifndef __A_RUN_TRACK_ELEMENT_H__
#define __A_RUN_TRACK_ELEMENT_H__

#include "zf_common_typedef.h"
#include "track_element_config.h"

/**
 * @brief 环岛状态数据。
 * @details 保存环岛识别过程中用到的里程累计值、相对偏航角、阶段标志和计时器。
 */
typedef struct
{
    float encoder;        // 环岛阶段的里程累计量，用于出入环距离判定
    float yaw_delta_sum;  /**< 环岛阶段累计的 gyro_z 绝对角增量，单位为度。 */
    float last_yaw;       // 保留给历史 yaw 差值方案，当前 gyro_z 绝对积分不依赖该字段
    float diff_set;       // 环岛阶段固定目标角速度，非 0 时覆盖普通循迹目标
    int8 distance;        // 编码器累计使能：1-累计，0-停止累计
    uint32 time_r;        // 右环识别计时器
    uint32 time_l;        // 左环识别计时器
    int8 gyro_flat;       // gyro_z 绝对角增量累计使能：1-更新，0-停止更新
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
 * 5ms 主控制链路直接调用该接口，避免高频路径多一层只转发的包装。
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
 * @return int8 0-无，1-左圆环，2-右圆环，3-圆桶，4-墙面，5-跷跷板。
 */
int8 a_run_track_element_get_expected_element(void);

/**
 * @brief 读取当前圆桶状态机阶段。
 * @return int8 0-空闲，1-等顶部，2-等回平，3-稳定延迟。
 */
int8 a_run_track_element_get_cylinder_state(void);

/**
 * @brief 读取当前墙面状态机阶段。
 * @return int8 0-空闲，1-等墙面强信号，2-下墙计时。
 */
int8 a_run_track_element_get_wall_state(void);

/**
 * @brief 读取圆桶判断使用的 roll 角差。
 * @return float roll 角差，单位：度，范围 -180~180。
 */
float a_run_track_element_get_cylinder_vz(void);

/**
 * @brief 更新环岛里程累计与 gyro_z 绝对角增量。
 *
 * 由 5ms 主控制链路在编码器与 gyro_z 更新后调用。
 */
void a_run_track_element_update_integrals(void);

#endif /* __A_RUN_TRACK_ELEMENT_H__ */

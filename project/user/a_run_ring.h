#ifndef __A_RUN_RING_H__
#define __A_RUN_RING_H__

#include "a_run_track_element.h"

/**
 * @brief 圆环状态机阶段。
 */
typedef enum
{
    RING_STATE_IDLE = 0,           /**< 未进入圆环流程。 */
    RING_STATE_ENTRY = 1,          /**< 已识别圆环入口，按里程推进。 */
    RING_STATE_PRE_RING = 2,       /**< 预入环阶段。 */
    RING_STATE_IN_RING = 3,        /**< 环内阶段。 */
    RING_STATE_PRE_OUT_RING = 4,   /**< 预出环阶段。 */
    RING_STATE_DRIVE_OUT_RING = 5, /**< 出环前直走阶段。 */
    RING_STATE_OUT_RING = 6        /**< 出环确认阶段。 */
} RingState;

/**
 * @brief 环岛状态数据。
 * @details 保存环岛识别过程中用到的里程累计值、相对偏航角、阶段标志和计时器。
 */
typedef struct
{
    float encoder;        // 环岛阶段的里程累计量，用于出入环距离判定
    float yaw_delta_sum;  /**< 环岛阶段累计的 gyro_z 绝对角增量，单位为度。 */
    float last_yaw;       // 保留给历史 yaw 差值方案，当前 gyro_z 绝对积分不依赖该字段
    float diff_set;       // 环岛阶段固定目标角速度，非 0 时启用圆环角速度环
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
 * @brief 复位圆环状态机。
 */
void a_run_ring_reset(void);

/**
 * @brief 按主控制环周期更新圆环状态机。
 * @param ring_dir 圆环方向：1-左圆环，-1-右圆环。
 * @return uint8 1-当前圆环流程完成，0-未完成。
 */
uint8 a_run_ring_update_5ms(int8 ring_dir);

/**
 * @brief 根据圆环状态覆盖目标角速度。
 * @param angle_target 指向圆环目标角速度的指针。
 */
void a_run_ring_update_angle_target(float *angle_target);

/**
 * @brief 更新圆环里程累计与 gyro_z 绝对积分。
 */
void a_run_ring_update_integrals(void);

/**
 * @brief 读取圆环状态。
 * @return RingState 当前圆环状态。
 */
RingState a_run_ring_get_state(void);

#endif /* __A_RUN_RING_H__ */

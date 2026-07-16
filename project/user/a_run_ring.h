#ifndef __A_RUN_RING_H__
#define __A_RUN_RING_H__

#include "a_run_track_element.h"

/**
 * @brief 圆环控制算法。
 */
typedef enum
{
    RING_CONTROL_LEGACY = 0,     /**< 旧固定角速度圆环。 */
    RING_CONTROL_SENSOR_BIAS = 1 /**< 电感偏置圆环。 */
} RingControlMode;

/**
 * @brief 圆环状态机阶段。
 */
typedef enum
{
    RING_STATE_IDLE = 0,           /**< 未进入圆环流程。 */
    RING_STATE_ENTRY = 1,          /**< 已识别圆环入口；新模式在该阶段零角速度直走。 */
    RING_STATE_PRE_RING = 2,       /**< 预入环阶段；新算法在该阶段放大外侧电感。 */
    RING_STATE_IN_RING = 3,        /**< 环内阶段。 */
    RING_STATE_PRE_OUT_RING = 4,   /**< 预出环阶段。 */
    RING_STATE_DRIVE_OUT_RING = 5, /**< 达到最小距离后按外侧电感和 ad5 确认的固定向外转向阶段。 */
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
 * @brief 复位圆环状态机。
 */
void a_run_ring_reset(void);

/**
 * @brief 按 2ms 主控制环周期更新圆环状态机。
 * @param ring_dir 圆环方向：1-左圆环，-1-右圆环。
 * @return uint8 1-当前圆环流程完成，0-未完成。
 */
uint8 a_run_ring_update_2ms(int8 ring_dir);

/**
 * @brief 根据圆环状态更新角速度目标。
 * @param angle_target 指向目标角速度的指针。
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

/**
 * @brief 圆环有效阶段使用独立ABC覆盖本次电感偏差解算参数。
 * @param a_value 横向主差分权重指针。
 * @param b_value 辅助电感差分权重指针。
 * @param c_value 分母补偿权重指针。
 */
void a_run_ring_apply_adc_params(float *a_value, float *b_value, float *c_value);

/**
 * @brief 圆环有效阶段使用独立方向环参数。
 * @param kp 方向环比例系数指针。
 * @param kd 方向环微分系数指针。
 * @param kp2 方向环非线性增强系数指针。
 */
void a_run_ring_apply_steer_params(float *kp, float *kd, float *kp2);

/**
 * @brief 圆环有效阶段使用独立角速度环和差速分配参数。
 * @param kp 角速度内环比例系数指针。
 * @param kd 角速度内环微分系数指针。
 * @param inner_gain 内轮减速增益指针。
 * @param outer_gain 外轮增速增益指针。
 */
void a_run_ring_apply_angle_diff_params(float *kp,
                                        float *kd,
                                        float *inner_gain,
                                        float *outer_gain);

/**
 * @brief 圆环运行阶段使用锁存参数组的目标速度。
 * @param speed 当前控制链目标速度指针。
 */
void a_run_ring_apply_speed(float *speed);

/**
 * @brief 进环时放大入环侧电感，出环时反向放大另一侧电感。
 * @param left_signal 左侧主电感ad1的局部浮点值。
 * @param left_middle_signal 左侧辅助电感ad2的局部浮点值。
 * @param right_middle_signal 右侧辅助电感ad3的局部浮点值。
 * @param right_signal 右侧主电感ad4的局部浮点值。
 */
void a_run_ring_apply_adc_bias(float *left_signal,
                               float *left_middle_signal,
                               float *right_middle_signal,
                               float *right_signal);

#endif /* __A_RUN_RING_H__ */

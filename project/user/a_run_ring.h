#ifndef __A_RUN_RING_H__
#define __A_RUN_RING_H__

#include "zf_common_typedef.h"
#include "eeprom.h"

/**
 * @brief 圆环状态机阶段。
 */
typedef enum
{
    RING_STATE_IDLE = 0,     /**< 未进入圆环流程。 */
    RING_STATE_ENTRY = 1,    /**< 已识别圆环入口，该阶段零角速度直走。 */
    RING_STATE_PRE_RING = 2, /**< 预入环阶段，该阶段放大入环同侧两路电感。 */
    RING_STATE_IN_RING = 3,  /**< 环内阶段。 */
    RING_STATE_OUT_RING = 4, /**< 出环确认阶段。 */
    RING_STATE_RELEASE = 5   /**< 出环完成后后台阶梯恢复巡线速度。 */
} RingState;

/**
 * @brief 圆环状态数据。
 * @details 保存圆环过程中用到的里程累计、角度积分、方向标志和计时器。
 */
typedef struct
{
    float distance_cm;    // 圆环阶段里程累计（cm），由编码器速度积分得到
    float yaw_delta_sum;  /**< 圆环阶段累计的 gyro_z 绝对角增量，单位为度。 */
    float angle_set;      // 圆环阶段固定角速度目标设定，非 0 时覆盖巡线目标角速度
    int8 distance_enable; // 里程累计使能：1-累计，0-停止累计
    uint32 entry_timer;   // 入口识别300ms确认窗口计时器
    int8 yaw_enable;      // gyro_z 绝对角积分累计使能：1-更新，0-停止更新
    int8 ring_dir;        // 当前圆环方向：1-左圆环，-1-右圆环，0-未进入
    uint32 out_ring_timer; // 出环阶段确认计时
} RingStruct;

/** 圆环过程数据，菜单和调试界面允许直接读取。 */
extern RingStruct ring_data;

/**
 * @brief 复位圆环状态机。
 */
void a_run_ring_reset(void);

/**
 * @brief 按 2ms 主控制环周期更新圆环状态机。
 * @param ring_dir 圆环方向：1-左圆环，-1-右圆环。
 * @param profile 本圈生效的参数组指针，由仲裁层按元素类型传入
 *                （&app.ring.small_profile 或 &app.ring.large_profile），
 *                入口确认时锁存，本圈全程不变。
 * @return uint8 1-当前圆环流程完成，0-未完成。
 */
uint8 a_run_ring_update_2ms(int8 ring_dir, const AppRingProfileConfig *profile);

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
 * @brief 解析角速度内环参数：先回落全局默认值，圆环有效阶段再覆盖。
 * @param kp 角速度内环比例系数指针。
 * @param kd 角速度内环微分系数指针。
 */
void a_run_ring_apply_angle_params(float *kp, float *kd);

/**
 * @brief 圆环有效阶段使用圆环参数组的目标速度。
 * @param speed 当前控制链目标速度指针。
 */
void a_run_ring_apply_speed(float *speed);

/**
 * @brief 更新圆环出环后的后台阶梯加速。
 *
 * 与圆桶释放同模式：出环后从 target_speed 逐拍爬回 speed_run，
 * 释放完成或再次进环时才真正复位状态机。
 *
 * @param speed 当前目标速度指针，非 RELEASE 态不修改。
 */
void a_run_ring_update_release_speed(float *speed);

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

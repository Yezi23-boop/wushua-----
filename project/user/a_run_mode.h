#ifndef __A_RUN_MODE_H__
#define __A_RUN_MODE_H__

#include "zf_common_typedef.h"

/**
 * @brief 环岛状态数据
 * @details 保存环岛识别过程中用到的里程累计值、相对偏航角、阶段标志和计时器
 */
typedef struct
{
    float encoder;        // 环岛阶段的里程累计量，用于出入环距离判定
    float Gyroz;          // 累计转过的偏航角度
    float last_yaw;       // 上一次的偏航角，用于计算增量
    float diff_set;       // 环岛阶段固定目标角速度，非 0 时覆盖普通循迹目标
    int8 distance;        // 编码器累计使能：1-累计，0-停止累计
    uint32 time_r;        // 右环识别计时器
    uint32 time_l;        // 左环识别计时器
    int8 gyro_flat;       // 相对偏航角更新使能：1-更新，0-停止更新
    int8 flast_l;         // 左环过程标志
    int8 flast_r;         // 右环过程标志
    int8 star_r;          // 右环起始标志
    int8 star_l;          // 左环起始标志
    int8 condition;       // 环岛流程辅助条件标志
    uint32 ing_ring_time; // 入环阶段确认计时
    uint32 out_ring_time; // 出环阶段确认计时
} RingStruct;

/** 全局环岛状态数据 */
extern RingStruct ring_data;

/**
 * @brief 飞坡控制阶段
 * @details flat_fly 使用该枚举值对外暴露当前阶段，非 0 阶段都会被丢线保护视为飞坡过渡期。
 */
typedef enum
{
    FLY_STATE_IDLE = 0,     /**< 普通巡线，允许检测飞坡入口 */
    FLY_STATE_HOLD = 1,     /**< 飞坡保持，锁定速度和目标角速度 */
    FLY_STATE_RECOVER = 2,  /**< 落地恢复，弱磁未恢复前继续锁角 */
    FLY_STATE_COOLDOWN = 3  /**< 退出冷却，防止弱磁区域重复触发 */
} FlyState;

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
 * @brief 读取当前圆环姿态门控结果，用于菜单调参显示。
 * @return int8 1-允许圆环识别，0-姿态门控禁止圆环识别。
 */
int8 a_run_mode_get_ring_pose_flat(void);

/**
 * @brief 读取当前期望赛道元素。
 * @return int8 0-无，1-左圆环，2-右圆环，3-圆筒。
 */
int8 a_run_mode_get_expected_element(void);

/**
 * @brief 读取当前圆筒状态机阶段。
 * @return int8 0-空闲，1-等顶部，2-等回平，3-稳定延迟。
 */
int8 a_run_mode_get_cylinder_state(void);

/**
 * @brief 读取圆桶判断使用的加速度 Z 方向滤波值。
 * @return float 滤波后的 acc_z / |acc|。
 */
float a_run_mode_get_cylinder_acc_vz(void);

void circle_check_l(uint8 allow_entry);
void gyro_integrals(void);

#endif

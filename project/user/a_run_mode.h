#ifndef __A_RUN_MODE_H__
#define __A_RUN_MODE_H__

#include "zf_common_typedef.h"

/**
 * @brief 环岛状态数据
 * @details 保存环岛识别过程中用到的编码器累计值、角速度累计值、阶段标志和计时器
 */
typedef struct
{
    float encoder;        // 环岛阶段的编码器累计量，用于出入环距离判定
    float Gyroz;          // Z 轴角速度累计量，用于阶段切换判定
    float diff_set;       // 环岛阶段下发给主控链的直接差速覆盖量
    int8 distance;        // 编码器累计使能：1-累计，0-停止累计
    uint32 time_r;        // 右环识别计时器
    uint32 time_l;        // 左环识别计时器
    int8 gyro_flat;       // 角速度累计使能：1-累计，0-停止累计
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
 * @brief 根据环岛状态更新最终差速输出
 * @param diff_output 指向差速输出的指针
 */
void run_mode_update_diff_output(float *diff_output);

void circle_check_r(void);
void gyro_integrals(void);

#endif

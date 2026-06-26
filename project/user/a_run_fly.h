#ifndef __A_RUN_FLY_H__
#define __A_RUN_FLY_H__

#include "zf_common_typedef.h"

/**
 * @brief 飞坡/跷跷板控制阶段。
 * @details flat_fly 使用该枚举值保存当前阶段，外部可按 0/非 0 判断跷跷板过渡期。
 */
typedef enum
{
    FLY_STATE_IDLE = 0,     /**< 普通巡线，允许在元素仲裁授权后检测跷跷板入口 */
    FLY_STATE_LOW = 1,      /**< 跷跷板上低速通过，无 PWM 限制，转向保留循迹 */
    FLY_STATE_COOLDOWN = 2  /**< 落地后居中权重阶梯增速 */
} FlyState;

/**
 * @brief 跷跷板停止等待控制阶段。
 * @details seesaw_state 使用该枚举值保存当前阶段。
 */
typedef enum
{
    SEESAW_IDLE = 0,      /**< 等待入口检测 */
    SEESAW_STOP = 1,      /**< 停车，目标速度为 0 */
    SEESAW_BRAKE = 2,     /**< 零速闭环刹车，抵消上板惯性 */
    SEESAW_CREEP = 3,     /**< 刹车后低速前挪，让车重更靠近跷跷板转轴后方 */
    SEESAW_WAIT = 4,      /**< 等待跷跷板倾斜 */
    SEESAW_CHECK = 5,     /**< 检查电感信号恢复 */
    SEESAW_RECOVER = 6,   /**< 阶梯增速恢复 */
    SEESAW_COOLDOWN = 7   /**< 复用飞坡 COOLDOWN */
} SeesawState;

/**
 * @brief 更新飞坡模式速度覆盖状态机。
 *
 * 该接口只供元素仲裁调用，入口检测由赛道元素仲裁授权。
 * 流程：IDLE → LOW（低速通过并等待落地回升）→ COOLDOWN（阶梯增速）。
 *
 * @param speed 输出目标速度指针；LOW/FLY/COOLDOWN 阶段会被状态机覆盖。
 * @param allow_entry 1-当前轮到跷跷板元素，允许从空闲态检测入口；0-禁止新入口。
 */
void a_run_fly_update_speed(float *speed, uint8 allow_entry);

/**
 * @brief 更新跷跷板完成后的阶梯增速。
 *
 * 该接口不依赖当前元素是否仍为跷跷板，只要 flat_fly 处于 COOLDOWN，
 * 就继续限制速度并逐拍释放，直到恢复到巡线目标速度后复位飞坡状态机。
 *
 * @param speed 输出目标速度指针，保留小数速度设定；COOLDOWN 阶段会被斜坡释放值覆盖。
 */
void a_run_fly_update_release_speed(float *speed);

/**
 * @brief 取出并清除飞坡/跷跷板完成事件。
 * @return uint8 1-本轮已完成恢复，可切入下一个元素；0-无完成事件。
 */
uint8 a_run_fly_take_finish_event(void);

/**
 * @brief 复位飞坡/跷跷板状态机。
 *
 * 元素仲裁关闭或重新进入跷跷板阶段前调用，清掉计数、阶段和完成事件。
 */
void a_run_fly_reset(void);

/**
 * @brief 更新跷跷板停止等待模式速度状态机。
 *
 * 检测到跷跷板后停车等待 1 秒，利用重力让跷跷板倾斜，
 * 电感信号恢复后出发，阶梯增速恢复到巡线速度。
 *
 * @param speed 输出目标速度指针。
 * @param allow_entry 1-当前期望元素为跷跷板，允许从空闲态检测入口；0-禁止新入口。
 */
void a_run_seesaw_update_speed(float *speed, uint8 allow_entry);

/**
 * @brief 复位跷跷板停止等待状态机。
 */
void a_run_seesaw_reset(void);

extern volatile uint8 fly_lost_line_blocked; /**< 飞坡状态机写、丢线保护读；1 表示临时屏蔽丢线，0 表示恢复丢线保护。 */
extern volatile int32 fly_pwm_output_limit; /**< COOLDOWN 写、电机输出读；大于 0 时限制实际 PWM 占空比。 */
extern volatile uint8 seesaw_zero_brake_active; /**< 停止等待模式零速闭环刹车窗口；1 时速度环使用 signed 编码器反馈。 */
extern volatile uint8 seesaw_centering_active; /**< 跷跷板前挪/恢复期临时居中权重开关，ADC 解算读。 */

#endif /* __A_RUN_FLY_H__ */

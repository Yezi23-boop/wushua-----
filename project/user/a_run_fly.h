#ifndef __A_RUN_FLY_H__
#define __A_RUN_FLY_H__

#include "zf_common_typedef.h"

/**
 * @brief 跷跷板控制阶段。
 * @details 停止等待模式完整使用各阶段；飞坡模式仅使用 RELEASE 标记释放期。
 * 外部通过 a_run_seesaw_get_state() 查询。
 */
typedef enum
{
    SEESAW_STATE_IDLE = 0,        /**< 等待入口检测。 */
    SEESAW_STATE_BRAKE = 1,       /**< 零速闭环刹车，抵消上板惯性。 */
    SEESAW_STATE_CREEP = 2,       /**< 刹车后低速前挪到设定距离。 */
    SEESAW_STATE_HOLD_DELAY = 3,  /**< 停车等待设定周期，靠重力让板倾斜。 */
    SEESAW_STATE_WAIT_SIGNAL = 4, /**< 继续停车并等待电感恢复。 */
    SEESAW_STATE_RELEASE = 5      /**< 完成驻留态，后台阶梯恢复速度。 */
} SeesawState;

/**
 * @brief 读取停止等待状态机阶段。
 *
 * 返回 SEESAW_STATE_RELEASE 表示处于两种模式共用的释放期，
 * 元素仲裁据此决定是否跳过复位以保住阶梯增速。
 *
 * @return SeesawState 当前阶段。
 */
SeesawState a_run_seesaw_get_state(void);

/**
 * @brief 更新飞坡模式速度覆盖状态机。
 *
 * 该接口只供元素仲裁调用，入口检测由赛道元素仲裁授权。
 * 流程：IDLE → LOW（低速通过并等待落地回升）→ RELEASE（阶梯增速）。
 *
 * @param speed 输出目标速度指针；LOW 阶段会被状态机覆盖。
 * @param allow_entry 1-当前轮到跷跷板元素，允许从空闲态检测入口；0-禁止新入口。
 */
void a_run_fly_update_speed(float *speed, uint8 allow_entry);

/**
 * @brief 更新跷跷板完成后的阶梯增速。
 *
 * 该接口不依赖当前元素是否仍为跷跷板，只要状态处于 RELEASE，
 * 就继续限制速度并逐拍释放，直到恢复到巡线目标速度后复位状态机。
 *
 * @param speed 输出目标速度指针，保留小数速度设定；释放期会被斜坡释放值覆盖。
 */
void a_run_seesaw_update_release_speed(float *speed);

/**
 * @brief 取出并清除跷跷板完成事件。
 * @return uint8 1-本轮已完成恢复，可切入下一个元素；0-无完成事件。
 */
uint8 a_run_seesaw_take_finish_event(void);

/**
 * @brief 更新跷跷板停止等待模式速度状态机。
 *
 * 检测到跷跷板后刹车、前挪并短暂保持，利用重力让跷跷板倾斜，
 * 电感信号恢复后出发，阶梯增速恢复到巡线速度。
 *
 * @param speed 输出目标速度指针。
 * @param allow_entry 1-当前期望元素为跷跷板，允许从空闲态检测入口；0-禁止新入口。
 */
void a_run_seesaw_update_speed(float *speed, uint8 allow_entry);

/**
 * @brief 复位飞坡/跷跷板状态机。
 *
 * 两种模式互斥运行，共用同一个复位入口：元素仲裁关闭或重新进入
 * 跷跷板阶段前调用，清掉两套模式的计数、阶段和完成事件。
 */
void a_run_seesaw_reset(void);

extern volatile uint8 seesaw_lost_line_blocked; /**< 状态机写、丢线保护读；1 表示临时屏蔽丢线，0 表示恢复丢线保护。 */
extern volatile int32 seesaw_pwm_output_limit;  /**< 释放期写、电机输出读；大于 0 时限制实际 PWM 占空比。 */
extern volatile uint8 seesaw_zero_brake_active; /**< 停止等待模式零速闭环刹车窗口；1 时速度环使用 signed 编码器反馈。 */
extern volatile uint8 seesaw_centering_active;  /**< 跷跷板前挪/恢复期临时居中权重开关，ADC 解算读。 */

#endif /* __A_RUN_FLY_H__ */

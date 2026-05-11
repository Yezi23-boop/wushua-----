#ifndef __A_RUN_FLY_H__
#define __A_RUN_FLY_H__

#include "zf_common_typedef.h"

/**
 * @brief 飞坡/跷跷板控制阶段。
 * @details flat_fly 使用该枚举值保存当前阶段，外部可按 0/非 0 判断跷跷板过渡期。
 */
typedef enum
{
    FLY_STATE_IDLE = 0,    /**< 普通巡线，允许在元素仲裁授权后检测跷跷板入口 */
    FLY_STATE_HOLD = 1,    /**< 跷跷板保持，锁定速度和目标角速度 */
    FLY_STATE_RECOVER = 2, /**< 落地恢复，弱磁未恢复前继续低速回线 */
    FLY_STATE_COOLDOWN = 3 /**< 退出冷却，防止弱磁区域重复触发 */
} FlyState;

/**
 * @brief 更新飞坡/跷跷板速度覆盖状态机。
 *
 * 该接口只供 a_run_mode 统一调配层调用，入口检测由赛道元素仲裁授权，
 * 避免跷跷板之外的弱磁区域误触发飞坡状态机。
 *
 * @param speed 输出目标速度指针，保持/恢复阶段会被覆盖为飞坡低速值。
 * @param allow_entry 1-当前轮到跷跷板元素，允许从空闲态检测入口；0-禁止新入口。
 */
void a_run_fly_update_speed(int *speed, uint8 allow_entry);

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

extern volatile uint8 fly_lost_line_blocked; /**< 飞坡状态机写、丢线保护读；1 表示临时屏蔽丢线，0 表示恢复丢线保护。 */

#endif /* __A_RUN_FLY_H__ */

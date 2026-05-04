#ifndef __A_RUN_FLY_H__
#define __A_RUN_FLY_H__

/**
 * @brief 飞坡控制阶段。
 * @details flat_fly 使用该枚举值保存当前阶段，外部可按 0/非 0 判断飞坡过渡期。
 */
typedef enum
{
    FLY_STATE_IDLE = 0,    /**< 普通巡线，允许检测飞坡入口 */
    FLY_STATE_HOLD = 1,    /**< 飞坡保持，锁定速度和目标角速度 */
    FLY_STATE_RECOVER = 2, /**< 落地恢复，弱磁未恢复前继续锁角 */
    FLY_STATE_COOLDOWN = 3 /**< 退出冷却，防止弱磁区域重复触发 */
} FlyState;

/**
 * @brief 更新飞坡速度覆盖状态机。
 *
 * 该接口只供 a_run_mode 统一调配层调用，外部业务仍通过
 * a_run_mode_update_fly_speed 访问，避免控制链路出现多个入口。
 *
 * @param speed 输出目标速度指针，飞坡保持/恢复阶段会被覆盖为飞坡速度。
 */
void a_run_fly_update_speed(int *speed);

#endif /* __A_RUN_FLY_H__ */

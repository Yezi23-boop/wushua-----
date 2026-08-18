#ifndef __A_RUN_CROSS_H__
#define __A_RUN_CROSS_H__

/**
 * @brief 十字类元素状态机阶段，双十字与单十字共用同一枚举。
 */
typedef enum
{
    CROSS_STATE_IDLE = 0,        /**< 空闲状态。 */
    CROSS_STATE_WAIT_SIGNAL = 1, /**< 等待电感和连续命中。 */
    CROSS_STATE_TIMING = 2       /**< 编码器积分通过十字。 */
} CrossState;

/**
 * @brief 复位双十字状态机。
 */
void a_run_cross_reset(void);

/**
 * @brief 按 2ms 主控制环周期更新双十字状态机。
 *
 * 电感和连续命中后进入 TIMING；TIMING 阶段累计编码器里程，
 * 达到阈值后完成。不覆盖速度和转向目标。
 *
 * @return uint8 1-双十字流程完成，0-未完成。
 */
uint8 a_run_cross_update_2ms(void);

/**
 * @brief 读取双十字状态。
 * @return CrossState 当前双十字状态。
 */
CrossState a_run_cross_get_state(void);

/**
 * @brief 复位单十字状态机。
 */
void a_run_cross_single_reset(void);

/**
 * @brief 按 2ms 主控制环周期更新单十字状态机。
 *
 * 入口判定与双十字完全相同，仅靠元素序列区分两者；
 * 退出阈值读 app.cross_single.encoder_target。
 *
 * @return uint8 1-单十字流程完成，0-未完成。
 */
uint8 a_run_cross_single_update_2ms(void);

/**
 * @brief 读取单十字状态。
 * @return CrossState 当前单十字状态。
 */
CrossState a_run_cross_single_get_state(void);

#endif /* __A_RUN_CROSS_H__ */

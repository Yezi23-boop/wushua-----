#ifndef __SOFT_TIMER_H__
#define __SOFT_TIMER_H__

#include "zf_common_headfile.h"

/**
 * @brief 注册变量到后台 10ms 累加队列并检测是否达到目标时间
 * @param count 指向需要累加计时的变量指针
 * @param target_time 目标超时时间（单位：毫秒）
 * @return 1 表示时间已到，0 表示未到
 */
int timeadd(uint32 *count, uint32 target_time);

/**
 * @brief 停止变量的后台累加并清零
 * @param count 指向需要销毁的计时变量指针
 */
void timedestroy(uint32 *count);

/**
 * @brief 供 10ms 定时器中断调用的后台更新函数
 */
void soft_timer_update_10ms(void);

#endif /* __SOFT_TIMER_H__ */

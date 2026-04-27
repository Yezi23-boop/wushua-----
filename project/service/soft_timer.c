/**
 * @file soft_timer.c
 * @brief 软件定时器管理器
 * @details
 * 提供一个简单的软件定时组件，支持倒计时检测。
 * 所提供的回调应当无阻塞，本模块依赖外部硬件中断（如 10ms 的 run_time_2）的固定周期调用步进。
 */
#include "soft_timer.h"

#define MAX_SOFT_TIMERS 10

/* 存放需要后台自动累加的变量指针数组 */
static uint32 *s_timer_list[MAX_SOFT_TIMERS] = {NULL};

/**
 * @brief 注册变量到后台 10ms 累加队列并检测是否达到目标时间
 * @param count 指向需要累加计时的变量指针
 * @param target_time 目标超时时间（单位：毫秒）
 * @return 1 表示时间已到，0 表示未到
 */
int timeadd(uint32 *count, uint32 target_time)
{
    int i;
    int found = 0;

    /* 检查该指针是否已经在队列中 */
    for (i = 0; i < MAX_SOFT_TIMERS; i++)
    {
        if (s_timer_list[i] == count)
        {
            found = 1;
            break;
        }
    }

    /* 如果没有找到，则寻找空闲位置存入 */
    if (found == 0)
    {
        for (i = 0; i < MAX_SOFT_TIMERS; i++)
        {
            if (s_timer_list[i] == NULL)
            {
                s_timer_list[i] = count;
                break;
            }
        }
    }

    /* 检查变量当前值是否已经达到目标时间 */
    if (*count >= target_time)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 停止变量的后台累加并清零
 * @param count 指向需要销毁的计时变量指针
 */
void timedestroy(uint32 *count)
{
    int i;

    /* 从后台更新队列中移除 */
    for (i = 0; i < MAX_SOFT_TIMERS; i++)
    {
        if (s_timer_list[i] == count)
        {
            s_timer_list[i] = NULL;
            break;
        }
    }

    /* 清零变量状态，以便下次使用 */
    *count = 0;
}

/**
 * @brief 供 10ms 定时器中断调用的后台更新函数
 */
void soft_timer_update_10ms(void)
{
    int i;
    for (i = 0; i < MAX_SOFT_TIMERS; i++)
    {
        if (s_timer_list[i] != NULL)
        {
            /* 每次增加 10 毫秒 */
            *(s_timer_list[i]) += 10;
        }
    }
}

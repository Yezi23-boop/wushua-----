#include "zf_common_headfile.h"
#include "key.h"

/* 硬件引脚定义 */
#define KEY1_PIN P37 /* 上/增加 */
#define KEY2_PIN P35 /* 下/减少 */
#define KEY3_PIN P34 /* 确定/切换 */
#define KEY4_PIN P33 /* 返回/取消 */

#define KEY_NUM 4

/* 扫描参数配置 */
#define LONG_PRESS_THRESHOLD 3 /* 判定长按所需的扫描周期数 */
#define DEBOUNCE_THRESHOLD 1   /* 消抖确认周期 */
#define REPEAT_INTERVAL 1      /* 长按触发连发效果的间隔 */

/* 全局按键状态变量 */
uint8 keystroke_label = 0;             /* 对外发布的按键事件编码 */
uint8 key_last_status[KEY_NUM] = {0};  /* 上次稳定状态 */
uint8 key_status[KEY_NUM] = {0};       /* 当前消抖后的稳定状态 */
uint16 key_press_time[KEY_NUM] = {0};  /* 累计按下时长 */
uint8 key_debounce_cnt[KEY_NUM] = {0}; /* 消抖计数器 */

/**
 * @brief 物理按键扫描逻辑
 * @details 采用非阻塞状态机实现，支持四路独立按键的消抖、短按和长按识别
 */
void Keystroke_Scan(void)
{
    uint8 i = 0;
    uint8 raw[KEY_NUM];

    keystroke_label = 0; /* 本周期默认无按键事件 */

    /* 读取硬件引脚原始值（低电平有效，取反后 1 代表按下） */
    raw[0] = (uint8)(!KEY1_PIN);
    raw[1] = (uint8)(!KEY2_PIN);
    raw[2] = (uint8)(!KEY3_PIN);
    raw[3] = (uint8)(!KEY4_PIN);

    for (i = 0; i < KEY_NUM; i++)
    {
        /* --- 消抖逻辑 --- */
        if (raw[i] != key_status[i])
        {
            key_debounce_cnt[i]++;
            if (key_debounce_cnt[i] >= DEBOUNCE_THRESHOLD)
            {
                /* 状态发生稳定切换 */
                key_last_status[i] = key_status[i];
                key_status[i] = raw[i];
                key_debounce_cnt[i] = 0;

                if (key_status[i])
                {
                    key_press_time[i] = 0; /* 新按下，开始计时 */
                }
                else
                {
                    /* 按键释放，判断是否为有效的短按 */
                    if (key_last_status[i] && key_press_time[i] < LONG_PRESS_THRESHOLD)
                    {
                        keystroke_label = (uint8)(i + 1); /* 产生短按事件 (1-4) */
                        break;
                    }
                    key_press_time[i] = 0;
                }
            }
        }
        else
        {
            key_debounce_cnt[i] = 0; /* 电平稳定，清零消抖计数 */
        }

        /* --- 长按与连发逻辑 --- */
        if (key_status[i])
        {
            key_press_time[i]++;

            /* 刚达到长按阈值，产生首个长按事件 (5-8) */
            if (key_press_time[i] == LONG_PRESS_THRESHOLD)
            {
                keystroke_label = (uint8)(i + 5);
                break;
            }
            /* 超过阈值后，按照 REPEAT_INTERVAL 产生连发事件 */
            else if (key_press_time[i] > LONG_PRESS_THRESHOLD)
            {
                if (((key_press_time[i] - LONG_PRESS_THRESHOLD) % REPEAT_INTERVAL) == 0)
                {
                    keystroke_label = (uint8)(i + 5);
                    break;
                }
            }
        }
    }
}

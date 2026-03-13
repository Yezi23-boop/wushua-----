#include "zf_common_headfile.h"
#define KEY1_PIN P33 // 上键
#define KEY2_PIN P34 // 下键
#define KEY3_PIN P36 // 确定键
#define KEY4_PIN P37 // 返回键
#define KEY_NUM 4
// 长按时间阈值：达到阈值判定为长按
#define LONG_PRESS_THRESHOLD 3
// 消抖阈值：状态变化需连续 N 次稳定才确认
#define DEBOUNCE_THRESHOLD 1
// 长按重复周期：长按保持期间每隔 N 次扫描重复触发一次
#define REPEAT_INTERVAL 1
// 按键数量
uint8 keystroke_label = 0;             // 当前产生的按键事件编码（0=无，1..4=短按，5..8=长按）
uint8 key_last_status[KEY_NUM] = {0};  // 上一次稳定状态（0=未按，1=按下）
uint8 key_status[KEY_NUM] = {0};       // 当前稳定状态（消抖后）
uint16 key_press_time[KEY_NUM] = {0};  // 按下持续时间（单位：扫描次数）
uint8 key_debounce_cnt[KEY_NUM] = {0}; // 消抖计数：状态变化时开始累计

void Keystroke_Scan(void)
{
    uint8 i = 0;
    keystroke_label = 0; // 默认本次扫描无事件

    {
        uint8 raw[4];
        // 按键为低电平有效：读引脚并取反，得到“按下=1，未按=0”的原始值，增加或减少按键要修改
        raw[0] = (uint8)(!KEY1_PIN);
        raw[1] = (uint8)(!KEY2_PIN);
        raw[2] = (uint8)(!KEY3_PIN);
        raw[3] = (uint8)(!KEY4_PIN);

        for (i = 0; i < KEY_NUM; i++)
        {
            // 原始值与稳定值不一致：进入消抖过程
            if (raw[i] != key_status[i])
            {
                key_debounce_cnt[i]++;
                if (key_debounce_cnt[i] >= DEBOUNCE_THRESHOLD)
                {
                    key_last_status[i] = key_status[i]; // 记录切换前的稳定值
                    key_status[i] = raw[i];             // 更新稳定值为原始值
                    key_debounce_cnt[i] = 0;
                    if (key_status[i])
                    {
                        key_press_time[i] = 0; // 新按下开始计时
                    }
                    else
                    {
                        // 释放且未达长按阈值：短按事件
                        if (key_last_status[i] && key_press_time[i] < LONG_PRESS_THRESHOLD)
                        {
                            keystroke_label = (uint8)(i + 1);
                            break;
                        }
                        key_press_time[i] = 0; // 释放后清零计时
                    }
                }
            }
            else
            {
                key_debounce_cnt[i] = 0; // 无变化：清零消抖计数
            }

            // 稳定状态为按下：累计按下时长并产生长按/重复事件
            if (key_status[i])
            {
                key_press_time[i]++;
                if (key_press_time[i] == LONG_PRESS_THRESHOLD)
                {
                    keystroke_label = (uint8)(i + 5);
                    break;
                }
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
}

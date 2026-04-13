#include "zf_common_headfile.h"
/* 硬件引脚定义 */
#define KEY1_PIN P26 /* 上/增加 */
#define KEY2_PIN P35 /* 下/减少 */
#define KEY3_PIN P34 /* 确定/切换 */
#define KEY4_PIN P37 /* 返回/取消 */

#define KEY_NUM 4

/* 扫描参数配置（10ms 节拍） */
#define KEY_EVENT_QUEUE_SIZE 8
#define LONG_PRESS_THRESHOLD 30 /* 300ms */
#define DEBOUNCE_THRESHOLD 2    /* 20ms */
#define REPEAT_INTERVAL 10      /* 100ms */

/* 全局按键状态变量 */
volatile uint8 keystroke_label = 0; /* 最近一次发布的按键事件编码 */
static volatile uint8 key_event_queue[KEY_EVENT_QUEUE_SIZE];
static volatile uint8 key_event_head = 0;
static volatile uint8 key_event_tail = 0;
static volatile uint8 key_repeat_pending_mask = 0; /* 长按连发事件在队列中的挂起位图 */
static volatile uint8 key_repeat_cancel_mask = 0;  /* 松手后需要丢弃的长按遗留事件位图 */
static uint8 key_last_status[KEY_NUM] = {0};  /* 上次稳定状态 */
static uint8 key_status[KEY_NUM] = {0};       /* 当前消抖后的稳定状态 */
static uint16 key_press_time[KEY_NUM] = {0};  /* 累计按下时长 */
static uint8 key_debounce_cnt[KEY_NUM] = {0}; /* 消抖计数器 */

static uint8 Keystroke_Is_Long_Event(uint8 event_code)
{
    return (uint8)((event_code >= 5) && (event_code <= 8));
}

static uint8 Keystroke_Long_Event_Mask(uint8 event_code)
{
    return (uint8)(1u << (event_code - 5));
}

static void Keystroke_Publish_Event(uint8 event_code)
{
    uint8 next_tail;
    uint8 repeat_mask;

    if (event_code == 0)
        return;

    if (Keystroke_Is_Long_Event(event_code))
    {
        repeat_mask = Keystroke_Long_Event_Mask(event_code);
        if (key_repeat_pending_mask & repeat_mask)
            return;
        key_repeat_cancel_mask &= (uint8)(~repeat_mask);
    }

    next_tail = (uint8)(key_event_tail + 1);
    if (next_tail >= KEY_EVENT_QUEUE_SIZE)
        next_tail = 0;

    /* 无锁环形队列：tail 的下一个位置撞上 head 时视为满，丢弃新事件 */
    if (next_tail == key_event_head)
        return;

    key_event_queue[key_event_tail] = event_code;
    key_event_tail = next_tail;
    if (Keystroke_Is_Long_Event(event_code))
        key_repeat_pending_mask |= repeat_mask;
    keystroke_label = event_code;
}

/**
 * @brief 10ms 节拍物理按键扫描逻辑
 * @details 采用非阻塞状态机实现，支持四路独立按键的消抖、短按和长按识别
 */
void Keystroke_Scan_10ms(void)
{
    uint8 i = 0;
    uint8 raw[KEY_NUM];

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
                        Keystroke_Publish_Event((uint8)(i + 1)); /* 产生短按事件 (1-4) */
                        return;
                    }
                    else if (key_last_status[i])
                    {
                        /* 长按松手后，丢弃队列里尚未消费的该键连发事件，避免松手后还继续移动。 */
                        key_repeat_cancel_mask |= (uint8)(1u << i);
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
                Keystroke_Publish_Event((uint8)(i + 5));
                return;
            }
            /* 超过阈值后，按照 REPEAT_INTERVAL 产生连发事件 */
            else if (key_press_time[i] > LONG_PRESS_THRESHOLD)
            {
                if (((key_press_time[i] - LONG_PRESS_THRESHOLD) % REPEAT_INTERVAL) == 0)
                {
                    Keystroke_Publish_Event((uint8)(i + 5));
                    return;
                }
            }
        }
    }
}

void Keystroke_Scan(void)
{
    Keystroke_Scan_10ms();
}

uint8 Keystroke_Get_Event(void)
{
    uint8 event_code = 0;
    uint8 repeat_mask;

    while (key_event_head != key_event_tail)
    {
        event_code = key_event_queue[key_event_head];
        key_event_head++;
        if (key_event_head >= KEY_EVENT_QUEUE_SIZE)
            key_event_head = 0;

        if (Keystroke_Is_Long_Event(event_code))
        {
            repeat_mask = Keystroke_Long_Event_Mask(event_code);
            key_repeat_pending_mask &= (uint8)(~repeat_mask);
            if (key_repeat_cancel_mask & repeat_mask)
            {
                key_repeat_cancel_mask &= (uint8)(~repeat_mask);
                event_code = 0;
                continue;
            }
        }

        return event_code;
    }

    return 0;
}

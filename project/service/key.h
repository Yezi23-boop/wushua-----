#ifndef _KEY_H_
#define _KEY_H_

#include "zf_common_typedef.h"

/**
 * @brief 按键事件编码
 * @details 
 * 0: 无事件
 * 1~4: 按键 1~4 的短按事件
 * 5~8: 按键 1~4 的长按/连按事件
 */
extern volatile uint8 keystroke_label;

/**
 * @brief 10ms 节拍按键扫描服务
 * @details 应在 10ms 定时中断中调用，负责消抖、短按与长按判定，并锁存事件
 */
void Keystroke_Scan_10ms(void);

/**
 * @brief 兼容旧接口的按键扫描入口
 * @details 当前语义等同于 Keystroke_Scan_10ms
 */
void Keystroke_Scan(void);

/**
 * @brief 主循环读取并清除一个锁存按键事件
 * @return 0 表示当前无事件，非 0 为按键事件编码
 */
uint8 Keystroke_Get_Event(void);

#endif /* _KEY_H_ */

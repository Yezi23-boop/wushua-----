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
extern uint8 keystroke_label;

/**
 * @brief 物理按键扫描服务函数
 * @details 应在定时中断或主循环中周期性调用，负责消抖、短按与长按判定
 */
void Keystroke_Scan(void);

#endif /* _KEY_H_ */

#ifndef _KEY_H_
#define _KEY_H_

// 按键事件编码：0=无事件；1..4=短按(KEY1..KEY4)；5..8=长按(KEY1..KEY4)
extern uint8 keystroke_label;

// 周期调用进行按键扫描与事件生成（带消抖与长按重复）
void Keystroke_Scan(void);

#endif

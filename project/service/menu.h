#ifndef _MENU_H_
#define _MENU_H_

#include "zf_common_typedef.h"

/**
 * @brief 当前菜单页面编码
 * @details 0 表示首页；其余页面沿用阻塞式菜单的层级编码。
 */
extern int display_codename;

/**
 * @brief 设置菜单服务使能状态
 */
void Menu_Set_Service_Enable(uint8 enabled);

/**
 * @brief 查询菜单服务是否使能
 */
uint8 Menu_Is_Service_Enabled(void);

/**
 * @brief 菜单 10ms 节拍入口
 * @details 阻塞式菜单直接消费按键队列，此接口保留给现有中断链路兼容。
 */
void Menu_Tick_10ms(void);

/**
 * @brief 菜单主处理函数
 * @details 阻塞式菜单在当前页面内等待按键事件，直到切页或退出。
 */
void Keystroke_Menu(void);

#endif /* _MENU_H_ */

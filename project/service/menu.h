#ifndef _MENU_H_
#define _MENU_H_

#include "zf_common_typedef.h"

/**
 * @brief 菜单系统状态变量
 * @details display_codename 表示当前所处的菜单页面 ID，用于页面切换和渲染
 */
extern int display_codename;

/* --- 核心控制函数 --- */

/**
 * @brief 处理菜单光标移动
 */
void Cursor(void);

/**
 * @brief 执行菜单页面跳转逻辑
 */
void Menu_Next_Back(void);

/**
 * @brief 检查指定的页面 ID 是否包含子菜单
 */
int Have_Sub_Menu(int menu_id);

/**
 * @brief 通用按键处理函数（用于步进倍数切换等）
 */
void HandleKeystroke(int keystroke_label);

/* --- 参数修改交互函数 --- */

/**
 * @brief 修改二值型参数（如 1 或 -1）
 */
void Keystroke_Special_Value(int16 *parameter);

/**
 * @brief 修改整型参数，支持动态步进倍数
 */
void Keystroke_int(int *parameter, int change_unit_MIN);

/**
 * @brief 修改浮点型参数，支持动态步进倍数
 */
void Keystroke_float(float *parameter, float change_unit_MIN);

/* --- 页面显示与处理主函数 --- */

/**
 * @brief 菜单系统顶层调度函数
 * @details 在主循环中调用，根据 display_codename 分发到各页面处理函数
 */
void Keystroke_Menu(void);

/**
 * @brief 主界面（HOME）渲染与处理
 */
void Keystroke_Menu_HOME(void);

/* --- 各子页面渲染与处理函数 --- */

/**
 * @brief 启动设置页面渲染
 */
void Menu_Start_Show(uint8 control_line);

/**
 * @brief 启动设置页面逻辑处理
 */
void Menu_Start_Process(void);

/**
 * @brief 速度环 PID 参数页面渲染
 */
void Menu_Speed_Show(uint8 control_line);

/**
 * @brief 速度环 PID 参数页面逻辑处理
 */
void Menu_Speed_Process(void);

/**
 * @brief 角度环 PID 参数页面渲染
 */
void Menu_Angle_Show(uint8 control_line);

/**
 * @brief 角度环 PID 参数页面逻辑处理
 */
void Menu_Angle_Process(void);

/**
 * @brief 圆环控制参数页面渲染
 */
void Menu_Circle_Show(uint8 control_line);

/**
 * @brief 圆环控制参数页面逻辑处理
 */
void Menu_Circle_Process(void);

/**
 * @brief 飞坡控制参数页面渲染
 */
void Menu_Fly_Show(uint8 control_line);

/**
 * @brief 飞坡控制参数页面逻辑处理
 */
void Menu_Fly_Process(void);

/**
 * @brief 传感器实时数值页面渲染
 */
void Menu_Sensor_Show(void);

/**
 * @brief 传感器实时数值页面逻辑处理
 */
void Menu_Sensor_Process(void);

#endif /* _MENU_H_ */

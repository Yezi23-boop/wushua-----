#ifndef _MENU_H_
#define _MENU_H_

/*********************************************
 * 菜单系统基础定义和变量声明
 *********************************************/
extern int display_codename; // 显示页面代号，用于表示当前所在菜单层级

/*********************************************
 * 核心功能函数声明
 *********************************************/
/**
 * @brief 光标处理函数
 * @details 处理菜单中光标的移动和显示
 */
void Cursor(void);

/**
 * @brief 菜单层级跳转处理
 * @details 处理菜单的上下级切换
 */
void Menu_Next_Back(void);

/**
 * @brief 检查是否存在子菜单
 * @param menu_id 菜单ID
 * @return 1=存在子菜单，0=不存在子菜单
 */
int Have_Sub_Menu(int menu_id);

/**
 * @brief 按键操作处理函数
 * @param keystroke_label 按键标识值
 * @details 处理菜单中的按键操作
 */
void HandleKeystroke(int keystroke_label);

/*********************************************
 * 参数修改功能函数声明
 *********************************************/
/**
 * @brief 特殊值修改函数（-1或1）
 * @param parameter 需要修改的参数指针
 */
void Keystroke_Special_Value(int *parameter);

/**
 * @brief 整型参数修改函数
 * @param parameter 需要修改的参数指针
 * @param change_unit_MIN 最小修改单位
 */
void Keystroke_int(int *parameter, int change_unit_MIN);

/**
 * @brief 浮点型参数修改函数
 * @param parameter 需要修改的参数指针
 * @param change_unit_MIN 最小修改单位
 */
void Keystroke_float(float *parameter, float change_unit_MIN);

/*********************************************
 * 菜单系统主控制函数声明
 *********************************************/
/**
 * @brief 菜单系统主函数
 * @details 根据当前菜单代号显示对应菜单
 */
void Keystroke_Menu(void);

/**
 * @brief 主菜单显示与处理
 */
void Keystroke_Menu_HOME(void);

/*********************************************
 * 各级子菜单显示函数声明
 *********************************************/
/**
 * @brief 启动配置菜单显示函数
 */
void Menu_Start_Show(uint8 control_line);

/**
 * @brief 启动配置菜单处理函数
 */
void Menu_Start_Process(void);

/**
 * @brief PID_Direction速度参数菜单显示函数
 */
void Menu_Speed_Show(uint8 control_line);

/**
 * @brief PID_Direction速度参数菜单处理函数
 */
void Menu_Speed_Process(void);

/**
 * @brief PID_Direction角度参数菜单显示函数
 */
void Menu_Angle_Show(uint8 control_line);

/**
 * @brief PID_Direction角度参数菜单处理函数
 */
void Menu_Angle_Process(void);

/**
 * @brief 圆环控制参数菜单显示
 * @param control_line 当前控制行
 */
void Menu_Circle_Show(uint8 control_line);

/**
 * @brief 圆环控制参数菜单处理
 */
void Menu_Circle_Process(void);
void Menu_Fly_Show(uint8 control_line);
void Menu_Fly_Process(void);

/**
 * @brief 电感数据打印菜单显示函数
 * @details 显示五个电感的实时数值（ad1,ad2,ad4,ad6,ad7）
 */
void Menu_Sensor_Show(void);

/**
 * @brief 电感数据打印菜单处理函数
 */
void Menu_Sensor_Process(void);
#endif

#include "zf_common_headfile.h"
/*********************************************
 * 菜单与菜单系统说明
 *********************************************/
// 按键值定义（与底层按键模块保持一致）
#define KEYSTROKE_ONE 1   // 上
#define KEYSTROKE_TWO 2   // 下
#define KEYSTROKE_THREE 3 // 确认
#define KEYSTROKE_FOUR 4  // 返回
// 长按事件编码（与按键模块保持一致：5..8 为 1..4 的长按）
#define KEYSTROKE_ONE_LONG 5   // 上-长按
#define KEYSTROKE_TWO_LONG 6   // 下-长按
#define KEYSTROKE_THREE_LONG 7 // 确认-长按
#define KEYSTROKE_FOUR_LONG 8  // 返回-长按
// 按键数量（用于菜单层逻辑，如需与底层 Key_Init 对齐可在初始化时同步）
// #define KEYSTROKE_COUNT 4

// 屏幕显示相关常量（IPS114 行高 18、列宽 8）
#define ROWS_MAX 7 * 18      // 屏幕上可移动光标的最大行
#define ROWS_MIN 1 * 18      // 屏幕上可移动光标的最小行
#define CENTER_COLUMN 12 * 8 // 居中列位置
#define EEPROM_MODE 1        // EEPROM 读写模式设为 1

/** 全局状态变量 */
int display_codename = 0;       // 当前显示页面编码
int cursor_row = 2 * 18;        // 当前光标所在行
int previous_cursor_row = -1;   // 上一次光标所在行
int menu_next_flag = 0;         // 菜单导航标志位（进入/返回）
float change_unit = 0;          // 参数修改的步进单位值
int change_unit_multiplier = 1; // 步进倍数（确认键循环 1/10/100）
int keystroke_three_count = 0;  // 统计 KEYSTROKE_THREE 的触发次数（防抖/长按）
int Have_Sub_Menu(int menu_id);
void Keystroke_Menu_HOME(void);
void Menu_Start_Process(void);
void Menu_Speed_Process(void);
void Menu_Angle_Process(void);
void Menu_Sensor_Process(void);
void Menu_Circle_Process(void);
// 提示文字点阵数据（用于刷写完成提示）
const uint8 chinese_data[] = {
    // 字模 1
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x20,
    0x10, 0x3B, 0xFF, 0xF0, 0x0C, 0x33, 0x0C, 0x30, 0x06, 0x63, 0x0C, 0x30, 0x03, 0xC3, 0x0C, 0x30,
    0x01, 0x83, 0x0C, 0x30, 0x01, 0x83, 0xFF, 0xF0, 0x02, 0x83, 0x0C, 0x30, 0x04, 0xC3, 0x0C, 0x30,
    0x18, 0xC3, 0x0C, 0x30, 0x20, 0x43, 0x0C, 0x30, 0x00, 0xC3, 0xFF, 0xF0, 0x00, 0xE3, 0x00, 0x30,
    0x01, 0xE2, 0x00, 0x00, 0x01, 0x60, 0x00, 0x18, 0x03, 0x7F, 0xFF, 0xFC, 0x06, 0x61, 0x90, 0x00,
    0x04, 0x61, 0x88, 0x10, 0x08, 0x61, 0x88, 0x38, 0x10, 0x61, 0x8C, 0x70, 0x20, 0x61, 0x85, 0x80,
    0x40, 0x61, 0x86, 0x00, 0x00, 0x61, 0x83, 0x00, 0x00, 0x61, 0x89, 0xC0, 0x00, 0xC1, 0xB0, 0xE0,
    0x0F, 0xC1, 0xC0, 0x7E, 0x03, 0x83, 0x80, 0x18, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 字模 2
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x0C, 0x00,
    0x00, 0x03, 0x0C, 0x30, 0x00, 0x31, 0x8C, 0x30, 0x3F, 0xF8, 0xCC, 0x60, 0x01, 0x00, 0xCC, 0x80,
    0x01, 0x00, 0xCD, 0x00, 0x01, 0x02, 0x0D, 0x00, 0x01, 0x03, 0xFF, 0xF8, 0x01, 0x03, 0x00, 0x30,
    0x01, 0x03, 0x00, 0x30, 0x01, 0x13, 0x08, 0x30, 0x3F, 0xFB, 0x0E, 0x30, 0x01, 0x03, 0x0C, 0x30,
    0x01, 0x03, 0x0C, 0x30, 0x01, 0x03, 0x0C, 0x30, 0x01, 0x03, 0x0C, 0x30, 0x01, 0x03, 0x0C, 0x30,
    0x01, 0x03, 0x08, 0x30, 0x01, 0x03, 0x08, 0x30, 0x01, 0x03, 0x18, 0x30, 0x01, 0x1F, 0x18, 0x30,
    0x01, 0xE2, 0x18, 0x00, 0x0F, 0x00, 0x36, 0x00, 0x3C, 0x00, 0x31, 0xC0, 0x30, 0x00, 0x60, 0xF0,
    0x00, 0x00, 0xC0, 0x38, 0x00, 0x03, 0x00, 0x1C, 0x00, 0x0C, 0x00, 0x0C, 0x00, 0x30, 0x00, 0x00,
    // 字模 3
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00,
    0x00, 0x0E, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x60, 0x0F, 0xFF, 0xFF, 0xF0,
    0x00, 0x18, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x31, 0xC0, 0x00, 0x00, 0x61, 0x80, 0x00,
    0x00, 0xC1, 0x80, 0x00, 0x00, 0xC1, 0x80, 0x00, 0x01, 0x81, 0x80, 0x00, 0x03, 0x81, 0x80, 0xC0,
    0x07, 0xFF, 0xFF, 0xE0, 0x03, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80, 0x00,
    0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80, 0x18, 0x3F, 0xFF, 0xFF, 0xFC, 0x00, 0x01, 0x80, 0x00,
    0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80, 0x00,
    0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00};

// 可进入的菜单页编码列表（删除/新增页面时务必同步维护）
int menu_have_sub[] = {
    0, // 主菜单
    1,
    11,
    12,
    13,
    14,
    15,
    16,
    17, // 启动配置菜单
    2,
    21,
    22,
    23,
    24,
    25, // PID_Direction 速度参数菜单
    3,
    31,
    32,
    33,
    34,
    35,
    36, // PID_Direction 角度参数菜单
    4,  // 传感器显示菜单
    5,
    51,
    52,
    53,
    54,
    55,
    56, // 圆环控制参数菜单
    6,
    61,
    62,
    63,
    64, // FLY 控制参数菜单
};

/**
 * @brief 光标移动与菜单进入/退出
 * @details 处理上下移动以及进入/返回操作
 */
void Cursor(void)
{
    menu_next_flag = 0;
    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
        cursor_row = (cursor_row > ROWS_MIN) ? cursor_row - 1 * 18 : ROWS_MAX; // 光标上移
        break;
    case KEYSTROKE_TWO:
        cursor_row = (cursor_row < ROWS_MAX) ? cursor_row + 1 * 18 : ROWS_MIN; // 光标下移
        break;
    case KEYSTROKE_THREE:
        menu_next_flag = 1; // 进入子菜单
        break;
    case KEYSTROKE_FOUR:
        menu_next_flag = -1; // 返回上级菜单
        break;
    }

    ips114_show_string(0, cursor_row, ">"); // 在当前位置显示箭头

    // 清除之前箭头位置的显示，避免残影
    if (previous_cursor_row != cursor_row)
    {
        ips114_show_string(0, previous_cursor_row, " ");
        previous_cursor_row = cursor_row;
    }
}

/**
 * @brief 菜单事件跳转处理
 * @details 处理菜单层级的切换逻辑
 */
void Menu_Next_Back()
{
    int menu_id = 0;
    switch (menu_next_flag)
    {
    case 0:
        break;
    case -1: // 返回上一级
        display_codename = display_codename / 10;
        cursor_row = ROWS_MIN;
        ips114_clear(RGB565_WHITE);
        break;
    case 1: // 进入下一级
        menu_id = display_codename * 10 + (cursor_row / 18);
        if (Have_Sub_Menu(menu_id))
        {
            display_codename = menu_id;
            ips114_clear(RGB565_WHITE);
        }
        break;
    }
    menu_next_flag = 0;
}

/**
 * @brief 判断当前页是否存在子菜单
 * @param menu_id 菜单 ID
 * @return 1=存在子菜单，0=不存在子菜单
 */
int Have_Sub_Menu(int menu_id)
{
    int i = 0;
    for (i = 0; i < sizeof(menu_have_sub) / sizeof(menu_have_sub[0]); i++)
    {
        if (menu_have_sub[i] == menu_id)
            return 1;
    }
    return 0;
}

/**
 * @brief 处理按键，扫描当前页并修改参数
 * @param keystroke_label 按键标识
 */
void HandleKeystroke(int keystroke_label)
{
    switch (keystroke_label)
    {
    case KEYSTROKE_FOUR:
        display_codename /= 10; // 返回上一页
        ips114_clear(RGB565_WHITE);
        break;
    case KEYSTROKE_THREE:
        keystroke_three_count++;
        // 确认键用于切换步进倍数：1 → 10 → 100 循环
        change_unit_multiplier = (keystroke_three_count % 3 == 0) ? 1 : (keystroke_three_count % 3 == 1) ? 10
                                                                                                         : 100;
        if (keystroke_three_count >= 3)
            keystroke_three_count = 0;
        break;
    }
}

/**
 * @brief 整数参数修改
 * @param parameter 需修改的参数指针
 * @param change_unit_MIN 最小修改单位
 */
void Keystroke_int(int *parameter, int change_unit_MIN)
{
    int change_unit = change_unit_MIN * change_unit_multiplier;
    // 显示当前步进值（整数）
    ips114_show_int32(15 * 8, 0, change_unit, 4);

    Keystroke_Scan();
    HandleKeystroke(keystroke_label);

    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
        *parameter += change_unit;
        break;
    case KEYSTROKE_TWO:
        *parameter -= change_unit;
        break;
    case KEYSTROKE_ONE_LONG:
        *parameter += change_unit;
        break;
    case KEYSTROKE_TWO_LONG:
        *parameter -= change_unit;
        break;
    }
}

/**
 * @brief 浮点参数修改
 * @param parameter 需修改的参数指针
 * @param change_unit_MIN 最小修改单位
 */
void Keystroke_float(float *parameter, float change_unit_MIN)
{
    float change_unit = (float)change_unit_MIN * (float)change_unit_multiplier;
    int32 scale = 1;
    int32 base;
    int32 unit;
    if (change_unit_MIN >= 1.0f)
        scale = 1;
    else if (change_unit_MIN >= 0.1f)
        scale = 10;
    else if (change_unit_MIN >= 0.01f)
        scale = 100;
    else
        scale = 1000;
    ips114_show_float(14 * 8, 0, change_unit, 4, 3);
    base = (int32)(change_unit_MIN * (float)scale);
    unit = base * change_unit_multiplier;
    change_unit = (float)unit / (float)scale;
    Keystroke_Scan();
    HandleKeystroke(keystroke_label);

    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
        *parameter += change_unit;
        break;
    case KEYSTROKE_TWO:
        *parameter -= change_unit;
        break;
    case KEYSTROKE_ONE_LONG:
        *parameter += change_unit;
        break;
    case KEYSTROKE_TWO_LONG:
        *parameter -= change_unit;
        break;
    }
}

/**
 * @brief 特殊取值修改（-1 与 1）
 * @param parameter 需修改的参数指针
 */
void Keystroke_Special_Value(int *parameter)
{
    Keystroke_Scan();
    HandleKeystroke(keystroke_label);

    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
        *parameter = 1;
        break;
    case KEYSTROKE_TWO:
        *parameter = -1;
        break;
    }
}

/**
 * @brief 菜单目录控制
 * @details 根据 display_codename 显示对应菜单页面
 * @note 删除页面时请同步修改 menu_have_sub[] 列表
 */
void Keystroke_Menu(void)
{
    switch (display_codename)
    {
    case 0: // 主菜单
        Keystroke_Menu_HOME();
        break;

    case 1: // 启动配置菜单
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        Menu_Start_Process();
        break;

    case 2: // PID_Direction 速度参数菜单
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
        Menu_Speed_Process();
        break;

    case 3: // PID_Direction 角度参数菜单
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
        Menu_Angle_Process();
        break;

    case 4: // 传感器显示菜单
        Menu_Sensor_Process();
        break;

    case 5: // 圆环控制参数菜单
    case 51:
    case 52:
    case 53:
    case 54:
    case 55:
    case 56:
        Menu_Circle_Process();
        break;
    case 6: // FLY 控制参数菜单
    case 61:
    case 62:
    case 63:
    case 64:
        Menu_Fly_Process();
        break;

    default:
        break;
    }
}

/*********************************************
 * 主菜单显示与处理
 *********************************************/
void Keystroke_Menu_HOME(void)
{
    while (menu_next_flag == 0)
    {
        printf("%f,%f,%f,%f\n", Err, PID.left_speed.speed, PID.right_speed.speed, 0.0);
        // 显示一级菜单标题
        ips114_show_string(CENTER_COLUMN, 0, "MENU");

        // 显示一级菜单项
        ips114_show_string(8 * 2, 1 * 18, "STRAT");  // 启动设置
        ips114_show_string(8 * 2, 2 * 18, "PID_1");  // PID_Direction 速度参数
        ips114_show_string(8 * 2, 3 * 18, "PID_2");  // PID_Direction 角度参数
        ips114_show_string(8 * 2, 4 * 18, "PRINTF"); // 调试数据显示
        ips114_show_string(8 * 2, 5 * 18, "RING");   // 圆环控制参数
        ips114_show_string(8 * 2, 6 * 18, "FLY");

        // 显示实时数据标签
        ips114_show_string(7 * 15, 1 * 18, "Err");
        ips114_show_string(7 * 15, 2 * 18, "steer");
        ips114_show_string(7 * 15, 3 * 18, "angle");
        ips114_show_string(7 * 15, 4 * 18, "dianya");
        ips114_show_string(7 * 15, 5 * 18, "flat_fly");
        ips114_show_float(8 * 23, 1 * 18, Err, 3, 2);
        // 显示位置式 PID_Direction 的转向输出
        ips114_show_float(8 * 23, 2 * 18, PID.steer.output, 3, 1);
        ips114_show_float(8 * 23, 3 * 18, PID.angle.output, 3, 1);
        ips114_show_float(8 * 23, 4 * 18, dianya, 4, 2);
        ips114_show_int32(8 * 23, 5 * 18, flat_fly, 4);
        ips114_show_float(7 * 15, 6 * 18, speed_run - PID.angle.output, 4, 2);
        ips114_show_float(8 * 23, 6 * 18, speed_run + PID.angle.output, 4, 2);
        Keystroke_Scan();
        Cursor();
    }

    // 菜单跳转处理
    if (menu_next_flag == 1)
    {
        int menu_id = display_codename * 10 + (cursor_row / 18);
        if (Have_Sub_Menu(menu_id))
        {
            display_codename = menu_id;
            cursor_row = ROWS_MIN;
            ips114_clear(RGB565_WHITE);
        }
    }
    // EEPROM 写入处理
    else if (menu_next_flag == -1 && EEPROM_MODE == 1)
    {
        eeprom_flash();
        // 刷写完成提示
        ips114_clear(RGB565_WHITE);
        ips114_show_chinese(11 * 6, 8 * 6, 32, chinese_data, 3, RGB565_BLACK);
        system_delay_ms(200);
        ips114_clear(RGB565_WHITE);
    }

    menu_next_flag = 0;
}

/*********************************************
 * 启动配置菜单（子菜单 1）
 *********************************************/
void Menu_Start_Show(uint8 control_line)
{
    ips114_show_string(4 * 8, 0 * 18, "<<STRAT");

    ips114_show_string(1 * 8, 1 * 18, "Start_Flag");   // 启动标志
    ips114_show_string(1 * 8, 2 * 18, "circle_flags"); // 出环方向标志
    ips114_show_string(1 * 8, 3 * 18, "fuya_xili");    // 负压吸力

    ips114_show_int32(14 * 8, 1 * 18, start_flag, 3);
    ips114_show_int32(14 * 8, 2 * 18, circle_flags, 3);
    ips114_show_float(14 * 8, 3 * 18, fuya_xili, 4, 3);

    // 显示当前编辑标识
    if (control_line == 1)
    {
        ips114_show_string(0, control_line, " ");
    }
    else
    {
        ips114_show_string(0, control_line, "&");
    }
}

void Menu_Start_Process(void)
{
    switch (display_codename)
    {
    case 1: // 启动配置主菜单
        while (menu_next_flag == 0)
        {
            Menu_Start_Show(1);
            Keystroke_Scan();
            Cursor();
        }
        Menu_Next_Back();
        break;

    case 11: // 启动标志修改
        Menu_Start_Show(1 * 18);
        Keystroke_Special_Value(&start_flag);
        break;
    case 12: // 出环方向修改
        Menu_Start_Show(2 * 18);
        Keystroke_Special_Value(&circle_flags);
        break;
    case 13: // 负压吸力
        Menu_Start_Show(3 * 18);
        Keystroke_float(&fuya_xili, 100);
        break;
    }
}

/*********************************************
 * PID_Direction 速度参数菜单（子菜单 2）
 *********************************************/
void Menu_Speed_Show(uint8 control_line)
{
    ips114_show_string(1 * 8, 0 * 18, "<<PID_SPEED");

    ips114_show_string(1 * 8, 1 * 18, "kp_Err");       // 位置误差比例系数
    ips114_show_string(1 * 8, 2 * 18, "kd_Err");       // 位置误差微分系数
    ips114_show_string(1 * 8, 3 * 18, "speed_run");    // 运行速度
    ips114_show_string(1 * 8, 4 * 18, "limiting_Err"); // 限幅
    ips114_show_string(1 * 8, 5 * 18, "kp2_Err");      // 位置误差比例平方系数

    ips114_show_float(14 * 8, 1 * 18, kp_Err, 3, 3);
    ips114_show_float(14 * 8, 2 * 18, kd_Err, 3, 3);
    ips114_show_float(14 * 8, 3 * 18, speed_run, 3, 3);
    ips114_show_float(14 * 8, 4 * 18, limiting_Err, 3, 3);
    ips114_show_float(14 * 8, 5 * 18, kp2_Err, 3, 3);

    // 显示当前编辑标识
    if (control_line == 1)
    {
        ips114_show_string(0, control_line, " ");
    }
    else
    {
        ips114_show_string(0, control_line, "&");
    }
}

void Menu_Speed_Process(void)
{
    switch (display_codename)
    {
    case 2: // PID_Direction 速度参数主菜单
        while (menu_next_flag == 0)
        {
            Menu_Speed_Show(0);
            Keystroke_Scan();
            Cursor();
        }
        Menu_Next_Back();
        break;

    case 21: // 位置误差比例系数
        Menu_Speed_Show(1 * 18);
        Keystroke_float(&kp_Err, 0.01f);
        break;
    case 22: // 位置误差微分系数
        Menu_Speed_Show(2 * 18);
        Keystroke_float(&kd_Err, 0.01f);
        break;
    case 23: // 运行速度
        Menu_Speed_Show(3 * 18);
        Keystroke_float(&speed_run, 1.00f);
        break;
    case 24: // 陀螺微分权重
        Menu_Speed_Show(4 * 18);
        Keystroke_float(&limiting_Err, 0.01f);
        break;
    case 25: // PWM 滤波
        Menu_Speed_Show(5 * 18);
        Keystroke_float(&kp2_Err, 0.001f);
        break;
    }
}

/*********************************************
 * PID_Direction 角度参数菜单（子菜单 3）
 *********************************************/
void Menu_Angle_Show(uint8 control_line)
{
    ips114_show_string(1 * 8, 0 * 18, "<<PID_ANGLE");

    ips114_show_string(1 * 8, 1 * 18, "kp_Angle");       // 角度比例系数
    ips114_show_string(1 * 8, 2 * 18, "kd_Angle");       // 角度微分系数
    ips114_show_string(1 * 8, 3 * 18, "limiting_Angle"); // 角度限幅
    ips114_show_string(1 * 8, 4 * 18, "A_1");            // 校正 A_1
    ips114_show_string(1 * 8, 5 * 18, "B_1");            // 校正 B_1
    ips114_show_string(1 * 8, 6 * 18, "C_l");            // 额外微调

    ips114_show_float(15 * 8, 1 * 18, kp_Angle, 3, 2);
    ips114_show_float(15 * 8, 2 * 18, kd_Angle, 3, 2);
    ips114_show_float(15 * 8, 3 * 18, limiting_Angle, 3, 2);
    ips114_show_float(14 * 8, 4 * 18, A_1, 3, 2);
    ips114_show_float(14 * 8, 5 * 18, B_1, 3, 2);
    ips114_show_float(14 * 8, 6 * 18, C_l, 3, 2);

    // 显示当前编辑标识
    if (control_line == 1)
    {
        ips114_show_string(0, control_line, " ");
    }
    else
    {
        ips114_show_string(0, control_line, "&");
    }
}

void Menu_Angle_Process(void)
{
    switch (display_codename)
    {
    case 3: // PID_Direction 角度参数主菜单
        while (menu_next_flag == 0)
        {
            Menu_Angle_Show(1);
            Keystroke_Scan();
            Cursor();
        }
        Menu_Next_Back();
        break;

    case 31: // 角度比例系数
        Menu_Angle_Show(1 * 18);
        Keystroke_float(&kp_Angle, 0.01f);
        break;
    case 32: // 角度微分系数
        Menu_Angle_Show(2 * 18);
        Keystroke_float(&kd_Angle, 0.01f);
        break;
    case 33: // 角度限幅
        Menu_Angle_Show(3 * 18);
        Keystroke_float(&limiting_Angle, 10.00f);
        break;
    case 34: // 校正 A_1
        Menu_Angle_Show(4 * 18);
        Keystroke_float(&A_1, 0.01f);
        break;
    case 35: // 校正 B_1
        Menu_Angle_Show(5 * 18);
        Keystroke_float(&B_1, 0.01f);
        break;
    case 36: // 校正 C_l
        Menu_Angle_Show(6 * 18);
        Keystroke_float(&C_l, 0.01f);
        break;
    }
}

/*********************************************
 * 传感器显示菜单（子菜单 4）
 *********************************************/
void Menu_Sensor_Show(void)
{
    // 显示赛道传感器标签（依次为 ad1..ad4）
    ips114_show_string(1 * 14, 18 * 0, "ad1"); // 左前
    ips114_show_string(1 * 14, 18 * 1, "ad2"); // 左后
    ips114_show_string(1 * 14, 18 * 2, "ad3"); // 右前
    ips114_show_string(1 * 14, 18 * 3, "ad4"); // 右后

    // 显示当前值（0-100），使用整数显示
    ips114_show_int32(14 * 4, 18 * 0, ad1, 3);
    ips114_show_int32(14 * 4, 18 * 1, ad2, 3);
    ips114_show_int32(14 * 4, 18 * 2, ad3, 3);
    ips114_show_int32(14 * 4, 18 * 3, ad4, 3);

    // 显示标定最大值 MA[]（整数显示供参考）
    ips114_show_int32(14 * 8, 18 * 0, MA[0], 5);   // ad1 最大值
    ips114_show_int32(14 * 8, 18 * 1, MA[1], 5);   // ad2 最大值
    ips114_show_int32(14 * 8, 18 * 2, MA[2], 5);   // ad3 最大值
    ips114_show_int32(14 * 8, 18 * 3, MA[3], 5);   // ad4 最大值
                                                   // 显示原始值 RAW[]
    ips114_show_int32(14 * 12, 18 * 0, RAW[0], 5); // ad1 最大值
    ips114_show_int32(14 * 12, 18 * 1, RAW[1], 5); // ad2 最大值
    ips114_show_int32(14 * 12, 18 * 2, RAW[2], 5); // ad3 最大值
    ips114_show_int32(14 * 12, 18 * 3, RAW[3], 5); // ad4 最大值
}

void Menu_Sensor_Process(void)
{
    switch (display_codename)
    {
    case 4:
        while (menu_next_flag == 0)
        {
            Menu_Sensor_Show();
            Keystroke_Scan();
            Cursor();
        }
        Menu_Next_Back();
        break;
    }
}
/*********************************************
 * 圆环控制参数菜单（子菜单 5）
 *********************************************/
void Menu_Circle_Show(uint8 control_line)
{
    ips114_show_string(1 * 8, 0 * 18, "<<RING_CTRL");

    ips114_show_string(1 * 8, 1 * 18, "ring_en"); // 圆环编码器开关值
    ips114_show_string(1 * 8, 2 * 18, "p_r_G");   // 入环前陀螺设定值
    ips114_show_string(1 * 8, 3 * 18, "i_r_G");   // 入环陀螺 Z 轴值
    ips114_show_string(1 * 8, 4 * 18, "p_o_G");   // 出环前陀螺设定值
    ips114_show_string(1 * 8, 5 * 18, "p_o__G");  // 出环前陀螺 Z 值
    ips114_show_string(1 * 8, 6 * 18, "p_o_en");  // 出环编码器开关值

    ips114_show_float(14 * 8, 1 * 18, ring_encoder, 3, 2);
    ips114_show_float(14 * 8, 2 * 18, pre_ring_Gyro_set, 3, 2);
    ips114_show_float(14 * 8, 3 * 18, in_ring_Gyroz, 3, 2);
    ips114_show_float(14 * 8, 4 * 18, pre_out_ring_Gyro_set, 3, 2);
    ips114_show_float(14 * 8, 5 * 18, pre_out_ring_Gyroz, 3, 2);
    ips114_show_float(14 * 8, 6 * 18, pre_out_ring_encoder, 3, 2);

    // 显示当前编辑标识
    if (control_line == 1)
    {
        ips114_show_string(0, control_line, " ");
    }
    else
    {
        ips114_show_string(0, control_line, "&");
    }
}

void Menu_Circle_Process(void)
{
    switch (display_codename)
    {
    case 5: // 圆环控制参数主菜单
        while (menu_next_flag == 0)
        {
            Menu_Circle_Show(1);
            Keystroke_Scan();
            Cursor();
        }
        Menu_Next_Back();
        break;

    case 51: // 圆环编码器开关值
        Menu_Circle_Show(1 * 18);
        Keystroke_float(&ring_encoder, 1.00f);
        break;
    case 52: // 入环前陀螺设定值
        Menu_Circle_Show(2 * 18);
        Keystroke_float(&pre_ring_Gyro_set, 10.00f);
        break;
    case 53: // 入环陀螺 Z 轴值
        Menu_Circle_Show(3 * 18);
        Keystroke_float(&in_ring_Gyroz, 10.00f);
        break;
    case 54: // 出环前陀螺设定值
        Menu_Circle_Show(4 * 18);
        Keystroke_float(&pre_out_ring_Gyro_set, 10.00f);
        break;
    case 55: // 出环前陀螺 Z 值
        Menu_Circle_Show(5 * 18);
        Keystroke_float(&pre_out_ring_Gyroz, 10.00f);
        break;
    case 56: // 出环编码器开关值
        Menu_Circle_Show(6 * 18);
        Keystroke_float(&pre_out_ring_encoder, 1);
        break;
    }
}
void Menu_Fly_Show(uint8 control_line)
{
    ips114_show_string(1 * 8, 0 * 18, "<<FLY_CTRL");
    ips114_show_string(1 * 8, 1 * 18, "fly_speed");  // 慢速速度
    ips114_show_string(1 * 8, 2 * 18, "fly_time_1"); // 连续触发帧数
    ips114_show_string(1 * 8, 3 * 18, "fly_time_2"); // 维持慢速帧数
    ips114_show_string(1 * 8, 4 * 18, "fly_angle");  // 慢速期间方向输出
    ips114_show_int32(14 * 8, 1 * 18, count_fly_speed, 4);
    ips114_show_int32(14 * 8, 2 * 18, count_fly_time_1, 4);
    ips114_show_int32(14 * 8, 3 * 18, count_fly_time_2, 4);
    ips114_show_int32(14 * 8, 4 * 18, count_fly_angle, 4);
    if (control_line == 1)
    {
        ips114_show_string(0, control_line, " ");
    }
    else
    {
        ips114_show_string(0, control_line, "&");
    }
}
void Menu_Fly_Process(void)
{
    switch (display_codename)
    {
    case 6:
        while (menu_next_flag == 0)
        {
            Menu_Fly_Show(1);
            Keystroke_Scan();
            Cursor();
        }
        Menu_Next_Back();
        break;
    case 61:
        Menu_Fly_Show(1 * 18);
        Keystroke_int(&count_fly_speed, 1);
        break;
    case 62:
        Menu_Fly_Show(2 * 18);
        Keystroke_int(&count_fly_time_1, 1);
        break;
    case 63:
        Menu_Fly_Show(3 * 18);
        Keystroke_int(&count_fly_time_2, 1);
        break;
    case 64:
        Menu_Fly_Show(4 * 18);
        Keystroke_int(&count_fly_angle, 1);
        break;
    }
}

#include "zf_common_headfile.h"
#include "menu.h"

/* --- 菜单按键编码定义 --- */
#define KEYSTROKE_ONE 1   /* 上移 */
#define KEYSTROKE_TWO 2   /* 下移 */
#define KEYSTROKE_THREE 3 /* 确认/切换倍率 */
#define KEYSTROKE_FOUR 4  /* 返回/取消 */

/* --- 菜单长按事件编码 --- */
#define KEYSTROKE_ONE_LONG 5
#define KEYSTROKE_TWO_LONG 6
#define KEYSTROKE_THREE_LONG 7
#define KEYSTROKE_FOUR_LONG 8

/* --- 菜单布局参数 --- */
#define ROWS_MAX (7 * 18)      /* 光标可停留的最底部行 */
#define ROWS_MIN (1 * 18)      /* 光标可停留的最顶部行 */
#define CENTER_COLUMN (12 * 8) /* 标题居中显示的列坐标 */
#define EEPROM_MODE 1          /* 返回主菜单时是否自动保存到 EEPROM */

/* --- 菜单运行状态 --- */
int display_codename = 0;       /* 当前显示的菜单 ID */
int cursor_row = 2 * 18;        /* 当前光标所在行 */
int previous_cursor_row = -1;   /* 上一次光标所在行，用于擦除旧光标 */
int menu_next_flag = 0;         /* 1-进入下级，-1-返回上级，0-保持当前页面 */
int change_unit_multiplier = 1; /* 参数调节倍率，在 1、10、100 之间切换 */
int keystroke_three_count = 0;  /* 确认键累计次数，用于轮换倍率 */

/* --- 内部辅助函数声明 --- */
static void show_config_saved_prompt(void);

/* 所有带子菜单的菜单 ID，用于判断是否允许继续进入下一级 */
int menu_have_sub[] = {
    0, 1, 11, 12, 13, 14, 15, 16, 17,
    2, 21, 22, 23, 24, 25,
    3, 31, 32, 33, 34, 35, 36,
    4,
    5, 51, 52, 53, 54, 55, 56,
    6, 61, 62, 63, 64, 65};

/* --- 光标与层级控制 --- */

/**
 * @brief 根据按键更新菜单光标位置
 * @details 上下键移动光标，确认和返回键只设置层级跳转标志
 */
void Cursor(void)
{
    menu_next_flag = 0;
    switch (keystroke_label)
    {
    case KEYSTROKE_ONE: /* 向上移动光标 */
        cursor_row = (cursor_row > ROWS_MIN) ? cursor_row - 18 : ROWS_MAX;
        break;
    case KEYSTROKE_TWO: /* 向下移动光标 */
        cursor_row = (cursor_row < ROWS_MAX) ? cursor_row + 18 : ROWS_MIN;
        break;
    case KEYSTROKE_THREE: /* 请求进入下一级 */
        menu_next_flag = 1;
        break;
    case KEYSTROKE_FOUR: /* 请求返回上一级 */
        menu_next_flag = -1;
        break;
    }

    /* 在当前行左侧显示光标 ">" */
    ips114_show_string(0, cursor_row, ">");

    /* 清除上一行光标，避免残留 */
    if (previous_cursor_row != cursor_row)
    {
        ips114_show_string(0, previous_cursor_row, " ");
        previous_cursor_row = cursor_row;
    }
}

/**
 * @brief 根据层级跳转标志进入子菜单或返回上一级
 * @details 结合当前光标位置计算目标菜单 ID，并在切换时清屏
 */
void Menu_Next_Back(void)
{
    int menu_id = 0;
    switch (menu_next_flag)
    {
    case -1: /* 返回上一级：菜单 ID 去掉最低位 */
        display_codename /= 10;
        cursor_row = ROWS_MIN;
        ips114_clear(RGB565_WHITE);
        break;
    case 1: /* 进入下一级：父 ID * 10 + 当前行号 */
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
 * @brief 判断指定菜单 ID 是否存在子菜单
 */
int Have_Sub_Menu(int menu_id)
{
    int i;
    for (i = 0; i < sizeof(menu_have_sub) / sizeof(menu_have_sub[0]); i++)
    {
        if (menu_have_sub[i] == menu_id)
            return 1;
    }
    return 0;
}

/**
 * @brief 处理菜单公共功能键
 * @details 返回键回上级，确认键循环切换参数调节倍率
 */
void HandleKeystroke(int label)
{
    switch (label)
    {
    case KEYSTROKE_FOUR:
        display_codename /= 10;
        ips114_clear(RGB565_WHITE);
        break;
    case KEYSTROKE_THREE:
        keystroke_three_count++;
        /* 在 1、10、100 三档之间循环切换步进 */
        change_unit_multiplier = (keystroke_three_count % 3 == 0) ? 1 : (keystroke_three_count % 3 == 1) ? 10
                                                                                                         : 100;
        if (keystroke_three_count >= 3)
            keystroke_three_count = 0;
        break;
    }
}

/* --- 参数修改助手 --- */

/**
 * @brief 整数参数修改处理
 */
void Keystroke_int(int *parameter, int change_unit_MIN)
{
    int unit = change_unit_MIN * change_unit_multiplier;
    uint8 changed = 0;

    /* 顶部显示当前整数步进 */
    ips114_show_int32(15 * 8, 0, unit, 4);

    Keystroke_Scan();
    HandleKeystroke(keystroke_label);

    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
        *parameter += unit;
        changed = 1;
        break;
    case KEYSTROKE_TWO:
        *parameter -= unit;
        changed = 1;
        break;
    case KEYSTROKE_ONE_LONG:
        *parameter += unit;
        changed = 1;
        break;
    case KEYSTROKE_TWO_LONG:
        *parameter -= unit;
        changed = 1;
        break;
    }

    /* 参数变更后立即同步配置，避免界面值与控制器参数不一致 */
    if (changed)
        control_apply_config();
}

/**
 * @brief 浮点参数修改处理
 */
void Keystroke_float(float *parameter, float change_unit_MIN)
{
    float unit;
    uint8 changed = 0;

    /* 将基础步进按倍率放大，便于粗调和细调切换 */
    unit = change_unit_MIN * (float)change_unit_multiplier;

    ips114_show_float(14 * 8, 0, unit, 4, 3);

    Keystroke_Scan();
    HandleKeystroke(keystroke_label);

    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
        *parameter += unit;
        changed = 1;
        break;
    case KEYSTROKE_TWO:
        *parameter -= unit;
        changed = 1;
        break;
    case KEYSTROKE_ONE_LONG:
        *parameter += unit;
        changed = 1;
        break;
    case KEYSTROKE_TWO_LONG:
        *parameter -= unit;
        changed = 1;
        break;
    }

    if (changed)
        control_apply_config();
}

/**
 * @brief 特殊参数切换，仅在 1 和 -1 之间切换
 */
void Keystroke_Special_Value(int16 *parameter)
{
    uint8 changed = 0;
    Keystroke_Scan();
    HandleKeystroke(keystroke_label);

    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
        *parameter = 1;
        changed = 1;
        break;
    case KEYSTROKE_TWO:
        *parameter = -1;
        changed = 1;
        break;
    }

    if (changed)
        control_apply_config();
}

/* --- 菜单入口分发 --- */

void Keystroke_Menu(void)
{
    /*
     * 根据 display_codename 调度对应页面
     * 每个页面函数负责自身的显示、按键处理和参数修改
     */
    switch (display_codename)
    {
    case 0:
        Keystroke_Menu_HOME();
        break;
    case 1:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
        Menu_Start_Process();
        break;
    case 2:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
        Menu_Speed_Process();
        break;
    case 3:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
        Menu_Angle_Process();
        break;
    case 4:
        Menu_Sensor_Process();
        break;
    case 5:
    case 51:
    case 52:
    case 53:
    case 54:
    case 55:
    case 56:
        Menu_Circle_Process();
        break;
    case 6:
    case 61:
    case 62:
    case 63:
    case 64:
        Menu_Fly_Process();
        break;
    }
}

/**
 * @brief 主菜单页面处理
 * @details 显示首页总览信息，并根据按键进入参数页或执行配置保存
 */
void Keystroke_Menu_HOME(void)
{
    while (menu_next_flag == 0)
    {
        /* 显示主标题 */
        ips114_show_string(CENTER_COLUMN, 0, "MENU");

        /* 显示一级菜单项 */
        ips114_show_string(16, 1 * 18, "STRAT");  /* 启动相关 */
        ips114_show_string(16, 2 * 18, "PID_1");  /* 速度 PID */
        ips114_show_string(16, 3 * 18, "PID_2");  /* 角度 PID */
        ips114_show_string(16, 4 * 18, "PRINTF"); /* 调试/传感器观测 */
        ips114_show_string(16, 5 * 18, "RING");   /* 环岛参数 */
        ips114_show_string(16, 6 * 18, "FLY");    /* 飞坡参数 */

        /* 显示首页实时监控项 */
        ips114_show_string(105, 1 * 18, "Err");
        ips114_show_string(105, 2 * 18, "steer");
        ips114_show_string(105, 3 * 18, "angle");
        ips114_show_string(105, 4 * 18, "V_bat");

        /* 显示首页实时数值 */
        ips114_show_float(184, 1 * 18, Err, 3, 2);
        ips114_show_float(184, 2 * 18, PID.steer.output, 3, 1);
        ips114_show_float(184, 3 * 18, PID.angle.output, 3, 1);
        ips114_show_float(184, 4 * 18, dianya, 4, 2);

        Keystroke_Scan();
        Cursor();
    }

    /* 确认键进入选中的子菜单 */
    if (menu_next_flag == 1)
    {
        int id = display_codename * 10 + (cursor_row / 18);
        if (Have_Sub_Menu(id))
        {
            display_codename = id;
            cursor_row = ROWS_MIN;
            ips114_clear(RGB565_WHITE);
        }
    }
    /* 返回键可触发参数保存，并给出 EEPROM 保存提示 */
    else if (menu_next_flag == -1 && EEPROM_MODE == 1)
    {
        config_save();
        show_config_saved_prompt();
    }

    menu_next_flag = 0;
}

/**
 * @brief 弹出参数保存成功提示
 */
static void show_config_saved_prompt(void)
{
    ips114_clear(RGB565_WHITE);
    ips114_show_string(CENTER_COLUMN - 24, 3 * 18, "SAVED OK!");
    system_delay_ms(300);
    ips114_clear(RGB565_WHITE);
}

/* --- 启动配置页面 (ID: 1x) --- */

void Menu_Start_Show(uint8 line)
{
    ips114_show_string(32, 0, "<<STRAT");
    ips114_show_string(8, 1 * 18, "Start_Flag");
    ips114_show_string(8, 2 * 18, "circle_flags");
    ips114_show_string(8, 3 * 18, "fuya_xili");

    ips114_show_int32(112, 1 * 18, app.start.start_flag, 3);
    ips114_show_int32(112, 2 * 18, app.start.circle_flags, 3);
    ips114_show_float(112, 3 * 18, app.start.fuya_xili, 4, 3);

    /* 绘制当前选中行标记 */
    ips114_show_string(0, line, (line == 1) ? " " : "&");
}

void Menu_Start_Process(void)
{
    switch (display_codename)
    {
    case 1: /* 一级菜单页面 */
        while (menu_next_flag == 0)
        {
            Menu_Start_Show(1);
            Keystroke_Scan();
            Cursor();
        }
        Menu_Next_Back();
        break;
    case 11:
        Menu_Start_Show(1 * 18);
        Keystroke_Special_Value(&app.start.start_flag);
        break;
    case 12:
        Menu_Start_Show(2 * 18);
        Keystroke_Special_Value(&app.start.circle_flags);
        break;
    case 13:
        Menu_Start_Show(3 * 18);
        Keystroke_float(&app.start.fuya_xili, 100.0f);
        break;
    }
}

/* --- 速度参数页面 (ID: 2x) --- */

void Menu_Speed_Show(uint8 line)
{
    ips114_show_string(8, 0, "<<PID_SPEED");
    ips114_show_string(8, 1 * 18, "kp_Err");
    ips114_show_string(8, 2 * 18, "kd_Err");
    ips114_show_string(8, 3 * 18, "speed_run");
    ips114_show_string(8, 4 * 18, "limit_Err");
    ips114_show_string(8, 5 * 18, "kp2_Err");

    ips114_show_float(112, 1 * 18, app.speed.kp_Err, 3, 3);
    ips114_show_float(112, 2 * 18, app.speed.kd_Err, 3, 3);
    ips114_show_float(112, 3 * 18, app.speed.speed_run, 3, 3);
    ips114_show_float(112, 4 * 18, app.speed.limiting_Err, 3, 3);
    ips114_show_float(112, 5 * 18, app.speed.kp2_Err, 3, 3);

    ips114_show_string(0, line, (line == 0) ? " " : "&");
}

void Menu_Speed_Process(void)
{
    switch (display_codename)
    {
    case 2:
        while (menu_next_flag == 0)
        {
            Menu_Speed_Show(0);
            Keystroke_Scan();
            Cursor();
        }
        Menu_Next_Back();
        break;
    case 21:
        Menu_Speed_Show(1 * 18);
        Keystroke_float(&app.speed.kp_Err, 0.01f);
        break;
    case 22:
        Menu_Speed_Show(2 * 18);
        Keystroke_float(&app.speed.kd_Err, 0.01f);
        break;
    case 23:
        Menu_Speed_Show(3 * 18);
        Keystroke_float(&app.speed.speed_run, 1.0f);
        break;
    case 24:
        Menu_Speed_Show(4 * 18);
        Keystroke_float(&app.speed.limiting_Err, 0.01f);
        break;
    case 25:
        Menu_Speed_Show(5 * 18);
        Keystroke_float(&app.speed.kp2_Err, 0.001f);
        break;
    }
}

/* --- 角度参数页面 (ID: 3x) --- */

void Menu_Angle_Show(uint8 line)
{
    ips114_show_string(8, 0, "<<PID_ANGLE");
    ips114_show_string(8, 1 * 18, "kp_Angle");
    ips114_show_string(8, 2 * 18, "kd_Angle");
    ips114_show_string(8, 3 * 18, "limit_Angle");
    ips114_show_string(8, 4 * 18, "A_1");
    ips114_show_string(8, 5 * 18, "B_1");
    ips114_show_string(8, 6 * 18, "C_l");

    ips114_show_float(120, 1 * 18, app.angle.kp_Angle, 3, 2);
    ips114_show_float(120, 2 * 18, app.angle.kd_Angle, 3, 2);
    ips114_show_float(120, 3 * 18, app.angle.limiting_Angle, 3, 2);
    ips114_show_float(112, 4 * 18, app.angle.A_1, 3, 2);
    ips114_show_float(112, 5 * 18, app.angle.B_1, 3, 2);
    ips114_show_float(112, 6 * 18, app.angle.C_l, 3, 2);

    ips114_show_string(0, line, (line == 1) ? " " : "&");
}

void Menu_Angle_Process(void)
{
    switch (display_codename)
    {
    case 3:
        while (menu_next_flag == 0)
        {
            Menu_Angle_Show(1);
            Keystroke_Scan();
            Cursor();
        }
        Menu_Next_Back();
        break;
    case 31:
        Menu_Angle_Show(1 * 18);
        Keystroke_float(&app.angle.kp_Angle, 0.01f);
        break;
    case 32:
        Menu_Angle_Show(2 * 18);
        Keystroke_float(&app.angle.kd_Angle, 0.01f);
        break;
    case 33:
        Menu_Angle_Show(3 * 18);
        Keystroke_float(&app.angle.limiting_Angle, 10.0f);
        break;
    case 34:
        Menu_Angle_Show(4 * 18);
        Keystroke_float(&app.angle.A_1, 0.01f);
        break;
    case 35:
        Menu_Angle_Show(5 * 18);
        Keystroke_float(&app.angle.B_1, 0.01f);
        break;
    case 36:
        Menu_Angle_Show(6 * 18);
        Keystroke_float(&app.angle.C_l, 0.01f);
        break;
    }
}

/* --- 传感器观测页面 (ID: 4) --- */

void Menu_Sensor_Show(void)
{
    ips114_show_string(8, 0, "<<SENSOR");
    ips114_show_string(56, 0, "NORM");
    ips114_show_string(112, 0, "RAW");
    ips114_show_string(176, 0, "MAX");

    ips114_show_string(8, 1 * 18, "ad1");
    ips114_show_string(8, 2 * 18, "ad2");
    ips114_show_string(8, 3 * 18, "ad3");
    ips114_show_string(8, 4 * 18, "ad4");
    ips114_show_string(8, 6 * 18, "Err");

    /* 第 0 行保留给标题，避免与数据区重叠 */
    ips114_show_int32(56, 1 * 18, ad1, 3);
    ips114_show_int32(56, 2 * 18, ad2, 3);
    ips114_show_int32(56, 3 * 18, ad3, 3);
    ips114_show_int32(56, 4 * 18, ad4, 3);

    ips114_show_int32(112, 1 * 18, RAW[0], 4);
    ips114_show_int32(112, 2 * 18, RAW[1], 4);
    ips114_show_int32(112, 3 * 18, RAW[2], 4);
    ips114_show_int32(112, 4 * 18, RAW[3], 4);

    ips114_show_int32(176, 1 * 18, MA[0], 4);
    ips114_show_int32(176, 2 * 18, MA[1], 4);
    ips114_show_int32(176, 3 * 18, MA[2], 4);
    ips114_show_int32(176, 4 * 18, MA[3], 4);

    ips114_show_float(56, 6 * 18, Err, 4, 1);
}

void Menu_Sensor_Process(void)
{
    while (menu_next_flag == 0)
    {
        Menu_Sensor_Show();
        Keystroke_Scan();
        Cursor();
    }
    Menu_Next_Back();
}

/* --- 环岛与飞坡页面 (ID: 5x / 6x) --- */
/* 下方分别处理 Menu_Circle_Process 和 Menu_Fly_Process */
void Menu_Circle_Show(uint8 line)
{
    ips114_show_string(8, 0, "<<RING_CTRL");
    ips114_show_string(8, 1 * 18, "ring_en");
    ips114_show_string(8, 2 * 18, "pre_r_G");
    ips114_show_string(8, 3 * 18, "in_r_G");
    ips114_show_string(8, 4 * 18, "pre_o_G");
    ips114_show_string(8, 5 * 18, "pre_o_Gz");
    ips114_show_string(8, 6 * 18, "pre_o_en");

    ips114_show_float(112, 1 * 18, app.ring.ring_encoder, 3, 2);
    ips114_show_float(112, 2 * 18, app.ring.pre_ring_Gyro_set, 3, 2);
    ips114_show_float(112, 3 * 18, app.ring.in_ring_Gyroz, 3, 2);
    ips114_show_float(112, 4 * 18, app.ring.pre_out_ring_Gyro_set, 3, 2);
    ips114_show_float(112, 5 * 18, app.ring.pre_out_ring_Gyroz, 3, 2);
    ips114_show_float(112, 6 * 18, app.ring.pre_out_ring_encoder, 3, 2);
    ips114_show_string(0, line, (line == 1) ? " " : "&");
}

void Menu_Circle_Process(void)
{
    switch (display_codename)
    {
    case 5:
        while (menu_next_flag == 0)
        {
            Menu_Circle_Show(1);
            Keystroke_Scan();
            Cursor();
        }
        Menu_Next_Back();
        break;
    case 51:
        Menu_Circle_Show(1 * 18);
        Keystroke_float(&app.ring.ring_encoder, 1.0f);
        break;
    case 52:
        Menu_Circle_Show(2 * 18);
        Keystroke_float(&app.ring.pre_ring_Gyro_set, 10.0f);
        break;
    case 53:
        Menu_Circle_Show(3 * 18);
        Keystroke_float(&app.ring.in_ring_Gyroz, 10.0f);
        break;
    case 54:
        Menu_Circle_Show(4 * 18);
        Keystroke_float(&app.ring.pre_out_ring_Gyro_set, 10.0f);
        break;
    case 55:
        Menu_Circle_Show(5 * 18);
        Keystroke_float(&app.ring.pre_out_ring_Gyroz, 10.0f);
        break;
    case 56:
        Menu_Circle_Show(6 * 18);
        Keystroke_float(&app.ring.pre_out_ring_encoder, 1.0f);
        break;
    }
}

void Menu_Fly_Show(uint8 line)
{
    ips114_show_string(8, 0, "<<FLY_CTRL");
    ips114_show_string(8, 1 * 18, "fly_speed");
    ips114_show_string(8, 2 * 18, "fly_time_1");
    ips114_show_string(8, 3 * 18, "fly_time_2");
    ips114_show_string(8, 4 * 18, "fly_angle");
    ips114_show_string(8, 5 * 18, "fly_en");

    ips114_show_int32(112, 1 * 18, app.fly.count_fly_speed, 4);
    ips114_show_int32(112, 2 * 18, app.fly.count_fly_time_1, 4);
    ips114_show_int32(112, 3 * 18, app.fly.count_fly_time_2, 4);
    ips114_show_int32(112, 4 * 18, app.fly.count_fly_angle, 4);
    ips114_show_int32(112, 5 * 18, app.fly.fly_ramp_enable, 4);
    ips114_show_string(0, line, (line == 1) ? " " : "&");
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
        Keystroke_int(&app.fly.count_fly_speed, 1);
        break;
    case 62:
        Menu_Fly_Show(2 * 18);
        Keystroke_int(&app.fly.count_fly_time_1, 1);
        break;
    case 63:
        Menu_Fly_Show(3 * 18);
        Keystroke_int(&app.fly.count_fly_time_2, 1);
        break;
    case 64:
        Menu_Fly_Show(4 * 18);
        Keystroke_int(&app.fly.count_fly_angle, 1);
        break;
    case 65:
        Menu_Fly_Show(5 * 18);
        Keystroke_int(&app.fly.fly_ramp_enable, 1);
        break;
    }
}

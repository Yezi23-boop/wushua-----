/**
 * @file menu.c
 * @brief 屏幕菜单与交互状态机实现
 * @details
 * 负责通过 IPS 屏幕和独立按键进行小车所有实时环境参数（速度、PID、摄像头、环岛阈值等）的就地修改和 EEPROM 同步。
 * 大部分时间以阻塞形式（或状态机驻留形式）独占 CPU 的前台任务，但在调整参数时中断服务依然照常运作。
 */
#include "zf_common_headfile.h"
#include "menu.h"
#include "../user/a_run_mode.h"

/* 按键事件编码，与 key 模块保持一致。 */
#define KEYSTROKE_ONE 1
#define KEYSTROKE_TWO 2
#define KEYSTROKE_THREE 3
#define KEYSTROKE_FOUR 4
#define KEYSTROKE_ONE_LONG 5
#define KEYSTROKE_TWO_LONG 6
#define KEYSTROKE_FOUR_LONG 8

/* 阻塞式菜单固定使用 18 像素行高。 */
#define MENU_ROW_HEIGHT 18
#define MENU_ROW_MIN (1 * MENU_ROW_HEIGHT)
#define MENU_HOME_ROW_MAX (5 * MENU_ROW_HEIGHT)
#define MENU_PAGE_COUNT 8
#define MENU_CENTER_X (12 * 8)
#define MENU_STEP_X (14 * 8)
#define MENU_STEP_INT_X (15 * 8)
#define EEPROM_MODE 1
#define MENU_SAVE_PROMPT_DELAY_MS 300

static uint8 menu_service_enabled = 0;
static int cursor_row = MENU_ROW_MIN;
static int previous_cursor_row = -1;
static int menu_next_flag = 0;
static int change_unit_multiplier = 1;
static int keystroke_three_count = 0;
static int menu_saved_row[MENU_PAGE_COUNT];
static int menu_entry_row[MENU_PAGE_COUNT];

int display_codename = 0;

static const int menu_have_sub[] = {
    0,
    1, 11, 12, 13, 14, 15, 16,
    2, 21, 22, 23, 24, 25, 26,
    3, 31, 32, 33, 34, 35, 36,
    4, 41, 411, 412, 413, 414, 415, 416,
        42, 421, 422, 423, 424, 425, 426, 427,
        43, 431, 432, 433,
        44, 441, 442, 443, 444, 445, 446,
        45, 451,
    5,
    160, 1601, 1602, 1603, 1604, 1605, 1606};

static void Menu_Clear_Pending_Key_Events(void);
static void Menu_Clear_Page(void);
static void Menu_Reset_Page_Memory(void);
static void Menu_Reset_Cursor(void);
static uint8 Menu_Read_Key_Event(void);
static int Menu_Get_Page_Root(int page_id);
static int Menu_Get_Page_Row_Max(int page_root);
static int Menu_Normalize_Row(int page_id, int row);
static void Menu_Save_Page_Position(int page_id, int row);
static int Menu_Load_Page_Position(int page_id);
static int Menu_Have_Sub_Menu(int menu_id);
static void Menu_Draw_Navigation_Cursor(int row_max);
static void Menu_Cursor_Update(int row_max);
static void Menu_Render_Current_Page(void);
static void Menu_Next_Back(void);
static void Menu_Handle_Common_Key(int label);
static void Menu_Show_Save_Prompt(void);
static void Menu_Draw_Home(void);
static void Menu_Draw_Start(int edit_line);
static void Menu_Draw_Speed(int edit_line);
static void Menu_Draw_Model(int edit_line);
static void Menu_Draw_Sensor(void);
static void Menu_Draw_Yuanshu(int edit_line);
static void Menu_Draw_Ring_Sub(int edit_line);
static void Menu_Draw_Cylinder_Sub(int edit_line);
static void Menu_Draw_Wall_Sub(int edit_line);
static void Menu_Draw_Cross_Sub(int edit_line);
static void Menu_Draw_Fly_Sub(int edit_line);
static void Menu_Draw_Element_Len(int edit_line);
static void Menu_Draw_Element(int edit_line);
static void Menu_Process_Special_Value(int16 *parameter);
static void Menu_Process_Int_Value(int *parameter, int change_unit_min);
static void Menu_Process_Float_Value(float *parameter, float change_unit_min);
static void Keystroke_Menu_HOME(void);
static void Menu_Start_Process(void);
static void Menu_Speed_Process(void);
static void Menu_Model_Process(void);
static void Menu_Sensor_Process(void);
static void Menu_Yuanshu_Process(void);
static void Menu_Element_Len_Process(void);
static void Menu_Element_Process(void);

void Menu_Set_Service_Enable(uint8 enabled)
{
    menu_service_enabled = enabled ? 1 : 0;

    if (menu_service_enabled)
    {
        Menu_Reset_Page_Memory();
        display_codename = 0;
        menu_next_flag = 0;
        change_unit_multiplier = 1;
        keystroke_three_count = 0;
        Menu_Reset_Cursor();
    }
    else
    {
        Menu_Reset_Page_Memory();
        display_codename = 0;
        menu_next_flag = 0;
        change_unit_multiplier = 1;
        keystroke_three_count = 0;
        Menu_Reset_Cursor();
    }

    Menu_Clear_Pending_Key_Events();
}

uint8 Menu_Is_Service_Enabled(void)
{
    return menu_service_enabled;
}

void Menu_Tick_10ms(void)
{
    /* 阻塞式菜单直接消费按键队列，此处无需额外 UI 节拍。 */
}

static void Menu_Clear_Pending_Key_Events(void)
{
    while (Keystroke_Get_Event() != 0)
    {
    }
}

static void Menu_Clear_Page(void)
{
    ips114_clear(RGB565_WHITE);
    previous_cursor_row = -1;
}

static void Menu_Reset_Page_Memory(void)
{
    int i;

    for (i = 0; i < MENU_PAGE_COUNT; i++)
    {
        menu_saved_row[i] = MENU_ROW_MIN;
        menu_entry_row[i] = MENU_ROW_MIN;
    }
}

static void Menu_Reset_Cursor(void)
{
    cursor_row = Menu_Load_Page_Position(display_codename);
    previous_cursor_row = -1;
}

static uint8 Menu_Read_Key_Event(void)
{
    uint8 event_code;

    if (!menu_service_enabled)
        return 0;

    event_code = Keystroke_Get_Event();
    if (event_code != 0)
    {
        keystroke_label = event_code;
        return event_code;
    }

    return 0;
}

static int Menu_Get_Page_Root(int page_id)
{
    if (page_id == 160 || (page_id >= 1601 && page_id <= 1606))
    {
        return 7;
    }

    while (page_id >= 10)
        page_id /= 10;

    if (page_id < 0 || page_id >= MENU_PAGE_COUNT)
        return 0;

    return page_id;
}

static int Menu_Get_Page_Row_Max(int page_root)
{
    switch (page_root)
    {
    case 0:
        return MENU_HOME_ROW_MAX;
    case 1:
        return 5 * MENU_ROW_HEIGHT;
    case 2:
        return 6 * MENU_ROW_HEIGHT;
    case 3:
        return 6 * MENU_ROW_HEIGHT;
    case 4:
        return 5 * MENU_ROW_HEIGHT;
    case 5:
        return MENU_ROW_MIN;
    case 7:
        return 6 * MENU_ROW_HEIGHT;
    default:
        return MENU_ROW_MIN;
    }
}

static int Menu_Normalize_Row(int page_id, int row)
{
    int row_max;
    int page_root;

    page_root = Menu_Get_Page_Root(page_id);
    row_max = Menu_Get_Page_Row_Max(page_root);
    if (row < MENU_ROW_MIN || row > row_max)
        return MENU_ROW_MIN;

    return row;
}

static void Menu_Save_Page_Position(int page_id, int row)
{
    int page_root;

    page_root = Menu_Get_Page_Root(page_id);
    menu_saved_row[page_root] = Menu_Normalize_Row(page_root, row);
}

static int Menu_Load_Page_Position(int page_id)
{
    int page_root;

    page_root = Menu_Get_Page_Root(page_id);
    return Menu_Normalize_Row(page_root, menu_saved_row[page_root]);
}

static int Menu_Have_Sub_Menu(int menu_id)
{
    int i;
    int item_count;

    item_count = (int)(sizeof(menu_have_sub) / sizeof(menu_have_sub[0]));
    for (i = 0; i < item_count; i++)
    {
        if (menu_have_sub[i] == menu_id)
            return 1;
    }
    return 0;
}

static void Menu_Draw_Navigation_Cursor(int row_max)
{
    if (cursor_row < MENU_ROW_MIN || cursor_row > row_max)
        cursor_row = MENU_ROW_MIN;

    ips114_show_string(0, cursor_row, ">");
    if (previous_cursor_row != cursor_row && previous_cursor_row >= MENU_ROW_MIN && previous_cursor_row <= row_max)
        ips114_show_string(0, previous_cursor_row, " ");
    previous_cursor_row = cursor_row;
}

static void Menu_Cursor_Update(int row_max)
{
    menu_next_flag = 0;

    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
    case KEYSTROKE_ONE_LONG:
        cursor_row = (cursor_row > MENU_ROW_MIN) ? (cursor_row - MENU_ROW_HEIGHT) : row_max;
        break;
    case KEYSTROKE_TWO:
    case KEYSTROKE_TWO_LONG:
        cursor_row = (cursor_row < row_max) ? (cursor_row + MENU_ROW_HEIGHT) : MENU_ROW_MIN;
        break;
    case KEYSTROKE_THREE:
        menu_next_flag = 1;
        break;
    case KEYSTROKE_FOUR:
    case KEYSTROKE_FOUR_LONG:
        menu_next_flag = -1;
        break;
    default:
        break;
    }

    ips114_show_string(0, cursor_row, ">");
    if (previous_cursor_row != cursor_row && previous_cursor_row >= MENU_ROW_MIN && previous_cursor_row <= row_max)
        ips114_show_string(0, previous_cursor_row, " ");
    previous_cursor_row = cursor_row;
}

static void Menu_Render_Current_Page(void)
{
    switch (display_codename)
    {
    case 0:
        Menu_Draw_Home();
        Menu_Draw_Navigation_Cursor(MENU_HOME_ROW_MAX);
        break;
    case 1:
        Menu_Draw_Start(0);
        Menu_Draw_Navigation_Cursor(5 * MENU_ROW_HEIGHT);
        break;
    case 11:
        Menu_Draw_Start(1 * MENU_ROW_HEIGHT);
        break;
    case 12:
        Menu_Draw_Start(2 * MENU_ROW_HEIGHT);
        break;
    case 13:
        Menu_Draw_Start(3 * MENU_ROW_HEIGHT);
        break;
    case 14:
        Menu_Draw_Start(4 * MENU_ROW_HEIGHT);
        break;
    case 15:
        Menu_Draw_Element_Len(MENU_ROW_MIN);
        break;
    case 2:
        Menu_Draw_Speed(0);
        Menu_Draw_Navigation_Cursor(6 * MENU_ROW_HEIGHT);
        break;
    case 21:
        Menu_Draw_Speed(1 * MENU_ROW_HEIGHT);
        break;
    case 22:
        Menu_Draw_Speed(2 * MENU_ROW_HEIGHT);
        break;
    case 23:
        Menu_Draw_Speed(3 * MENU_ROW_HEIGHT);
        break;
    case 24:
        Menu_Draw_Speed(4 * MENU_ROW_HEIGHT);
        break;
    case 25:
        Menu_Draw_Speed(5 * MENU_ROW_HEIGHT);
        break;
    case 26:
        Menu_Draw_Speed(6 * MENU_ROW_HEIGHT);
        break;
    case 3:
        Menu_Draw_Model(0);
        Menu_Draw_Navigation_Cursor(6 * MENU_ROW_HEIGHT);
        break;
    case 31:
        Menu_Draw_Model(1 * MENU_ROW_HEIGHT);
        break;
    case 32:
        Menu_Draw_Model(2 * MENU_ROW_HEIGHT);
        break;
    case 33:
        Menu_Draw_Model(3 * MENU_ROW_HEIGHT);
        break;
    case 34:
        Menu_Draw_Model(4 * MENU_ROW_HEIGHT);
        break;
    case 35:
        Menu_Draw_Model(5 * MENU_ROW_HEIGHT);
        break;
    case 36:
        Menu_Draw_Model(6 * MENU_ROW_HEIGHT);
        break;
    case 4:
        Menu_Draw_Yuanshu(0);
        Menu_Draw_Navigation_Cursor(4 * MENU_ROW_HEIGHT);
        break;
    case 41:
        Menu_Draw_Ring_Sub(0);
        Menu_Draw_Navigation_Cursor(7 * MENU_ROW_HEIGHT);
        break;
    case 411:
        Menu_Draw_Ring_Sub(1 * MENU_ROW_HEIGHT);
        break;
    case 412:
        Menu_Draw_Ring_Sub(2 * MENU_ROW_HEIGHT);
        break;
    case 413:
        Menu_Draw_Ring_Sub(3 * MENU_ROW_HEIGHT);
        break;
    case 414:
        Menu_Draw_Ring_Sub(4 * MENU_ROW_HEIGHT);
        break;
    case 415:
        Menu_Draw_Ring_Sub(5 * MENU_ROW_HEIGHT);
        break;
    case 416:
        Menu_Draw_Ring_Sub(6 * MENU_ROW_HEIGHT);
        break;
    case 417:
        Menu_Draw_Ring_Sub(7 * MENU_ROW_HEIGHT);
        break;
    case 42:
        Menu_Draw_Cylinder_Sub(0);
        Menu_Draw_Navigation_Cursor(7 * MENU_ROW_HEIGHT);
        break;
    case 421:
        Menu_Draw_Cylinder_Sub(1 * MENU_ROW_HEIGHT);
        break;
    case 422:
        Menu_Draw_Cylinder_Sub(2 * MENU_ROW_HEIGHT);
        break;
    case 423:
        Menu_Draw_Cylinder_Sub(3 * MENU_ROW_HEIGHT);
        break;
    case 424:
        Menu_Draw_Cylinder_Sub(4 * MENU_ROW_HEIGHT);
        break;
    case 425:
        Menu_Draw_Cylinder_Sub(5 * MENU_ROW_HEIGHT);
        break;
    case 426:
        Menu_Draw_Cylinder_Sub(6 * MENU_ROW_HEIGHT);
        break;
    case 427:
        Menu_Draw_Cylinder_Sub(7 * MENU_ROW_HEIGHT);
        break;
    case 43:
        Menu_Draw_Wall_Sub(0);
        Menu_Draw_Navigation_Cursor(3 * MENU_ROW_HEIGHT);
        break;
    case 431:
        Menu_Draw_Wall_Sub(1 * MENU_ROW_HEIGHT);
        break;
    case 432:
        Menu_Draw_Wall_Sub(2 * MENU_ROW_HEIGHT);
        break;
    case 433:
        Menu_Draw_Wall_Sub(3 * MENU_ROW_HEIGHT);
        break;
    case 44:
        Menu_Draw_Fly_Sub(0);
        Menu_Draw_Navigation_Cursor(6 * MENU_ROW_HEIGHT);
        break;
    case 441:
        Menu_Draw_Fly_Sub(1 * MENU_ROW_HEIGHT);
        break;
    case 442:
        Menu_Draw_Fly_Sub(2 * MENU_ROW_HEIGHT);
        break;
    case 443:
        Menu_Draw_Fly_Sub(3 * MENU_ROW_HEIGHT);
        break;
    case 444:
        Menu_Draw_Fly_Sub(4 * MENU_ROW_HEIGHT);
        break;
    case 445:
        Menu_Draw_Fly_Sub(5 * MENU_ROW_HEIGHT);
        break;
    case 446:
        Menu_Draw_Fly_Sub(6 * MENU_ROW_HEIGHT);
        break;
    case 45:
        Menu_Draw_Cross_Sub(0);
        Menu_Draw_Navigation_Cursor(1 * MENU_ROW_HEIGHT);
        break;
    case 451:
        Menu_Draw_Cross_Sub(1 * MENU_ROW_HEIGHT);
        break;
    case 5:
        Menu_Draw_Sensor();
        break;
    case 160:
        Menu_Draw_Element(0);
        Menu_Draw_Navigation_Cursor(6 * MENU_ROW_HEIGHT);
        break;
    case 1601:
        Menu_Draw_Element(1 * MENU_ROW_HEIGHT);
        break;
    case 1602:
        Menu_Draw_Element(2 * MENU_ROW_HEIGHT);
        break;
    case 1603:
        Menu_Draw_Element(3 * MENU_ROW_HEIGHT);
        break;
    case 1604:
        Menu_Draw_Element(4 * MENU_ROW_HEIGHT);
        break;
    case 1605:
        Menu_Draw_Element(5 * MENU_ROW_HEIGHT);
        break;
    case 1606:
        Menu_Draw_Element(6 * MENU_ROW_HEIGHT);
        break;
    default:
        break;
    }
}

static void Menu_Next_Back(void)
{
    int current_page;
    int current_root;
    int menu_id;
    int target_root;

    current_page = display_codename;
    current_root = Menu_Get_Page_Root(current_page);
    menu_id = 0;
    switch (menu_next_flag)
    {
    case -1:
        Menu_Save_Page_Position(current_page, cursor_row);
        display_codename /= 10;
        if (display_codename == 0 && current_root != 0)
            cursor_row = Menu_Normalize_Row(0, menu_entry_row[current_root]);
        else
            cursor_row = Menu_Load_Page_Position(display_codename);
        Menu_Clear_Page();
        break;
    case 1:
        Menu_Save_Page_Position(current_page, cursor_row);
        menu_id = current_page * 10 + (cursor_row / MENU_ROW_HEIGHT);
        if (Menu_Have_Sub_Menu(menu_id))
        {
            target_root = Menu_Get_Page_Root(menu_id);
            menu_entry_row[target_root] = Menu_Normalize_Row(current_page, cursor_row);
            display_codename = menu_id;
            cursor_row = Menu_Load_Page_Position(display_codename);
            Menu_Clear_Page();
        }
        break;
    default:
        break;
    }

    cursor_row = Menu_Normalize_Row(display_codename, cursor_row);
    previous_cursor_row = -1;
    menu_next_flag = 0;
    Menu_Render_Current_Page();
    Menu_Clear_Pending_Key_Events();
}

static void Menu_Handle_Common_Key(int label)
{
    int current_root;

    switch (label)
    {
    case KEYSTROKE_FOUR:
    case KEYSTROKE_FOUR_LONG:
        Menu_Save_Page_Position(display_codename, cursor_row);
        current_root = Menu_Get_Page_Root(display_codename);
        display_codename /= 10;
        if (display_codename == 0 && current_root != 0)
            cursor_row = Menu_Normalize_Row(0, menu_entry_row[current_root]);
        else
            cursor_row = Menu_Load_Page_Position(display_codename);
        Menu_Clear_Page();
        previous_cursor_row = -1;
        Menu_Render_Current_Page();
        Menu_Clear_Pending_Key_Events();
        break;
    case KEYSTROKE_THREE:
        keystroke_three_count++;
        change_unit_multiplier = (keystroke_three_count % 3 == 0) ? 1 : ((keystroke_three_count % 3 == 1) ? 10 : 100);
        if (keystroke_three_count >= 3)
            keystroke_three_count = 0;
        break;
    default:
        break;
    }
}

static void Menu_Show_Save_Prompt(void)
{
    Menu_Clear_Page();
    /* 保存完成后短暂停留，避免提示一闪而过看不清。 */
    ips114_show_string(MENU_CENTER_X - 16, 3 * MENU_ROW_HEIGHT, "save");
    previous_cursor_row = -1;
    Menu_Clear_Pending_Key_Events();
    system_delay_ms(MENU_SAVE_PROMPT_DELAY_MS);
    /* 提示结束后先清屏，避免返回首页时残留 save 字样。 */
    Menu_Clear_Page();
    Menu_Clear_Pending_Key_Events();
}

static void Menu_Draw_Home(void)
{
    ips114_show_string(MENU_CENTER_X, 0, "MENU");

    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "START");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "CTRL");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "MODEL");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "YUANSHU");
    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "SENSOR");

    ips114_show_string(105, 1 * MENU_ROW_HEIGHT, "Err");
    ips114_show_string(105, 2 * MENU_ROW_HEIGHT, "steer");
    ips114_show_string(105, 3 * MENU_ROW_HEIGHT, "gyro_z");
    ips114_show_string(105, 4 * MENU_ROW_HEIGHT, "V_bat");
    ips114_show_string(105, 5 * MENU_ROW_HEIGHT, "L_pwm");
    ips114_show_string(105, 6 * MENU_ROW_HEIGHT, "R_pwm");

    ips114_show_float(184, 1 * MENU_ROW_HEIGHT, Err, 3, 2);
    ips114_show_float(184, 2 * MENU_ROW_HEIGHT, PID.steer.output, 3, 1);
    ips114_show_float(184, 3 * MENU_ROW_HEIGHT, gyro_z, 3, 2);
    ips114_show_float(184, 4 * MENU_ROW_HEIGHT, dianya, 4, 2);
    ips114_show_float(184, 5 * MENU_ROW_HEIGHT, left_target, 4, 1);
    ips114_show_float(184, 6 * MENU_ROW_HEIGHT, right_target, 4, 1);
}

static void Menu_Draw_Start(int edit_line)
{
    ips114_show_string(8, 0, "<<START");
    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "Start_Flag");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "elem_en");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "fuya_ground");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "gyro_fbN");
    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "ELEM");

    ips114_show_int32(112, 1 * MENU_ROW_HEIGHT, app.start.start_flag, 3);
    ips114_show_int32(112, 2 * MENU_ROW_HEIGHT, app.start.element_enable, 3);
    ips114_show_float(112, 3 * MENU_ROW_HEIGHT, app.start.fuya_xili, 4, 1);
    ips114_show_float(112, 4 * MENU_ROW_HEIGHT, app.angle.gyro_feedback_scale, 4, 2);
    ips114_show_int32(112, 5 * MENU_ROW_HEIGHT, app.start.element_len, 3);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

static void Menu_Draw_Speed(int edit_line)
{
    ips114_show_string(8, 0, "<<CTRL");
    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "kp_Err");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "kd_Err");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "gyro_dmp");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "speed_run");
    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "limit_Err");
    ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "kp2_Err");

    ips114_show_float(112, 1 * MENU_ROW_HEIGHT, app.speed.kp_Err, 3, 3);
    ips114_show_float(112, 2 * MENU_ROW_HEIGHT, app.speed.kd_Err, 3, 3);
    ips114_show_float(112, 3 * MENU_ROW_HEIGHT, app.speed.gyro_damp_Err, 3, 3);
    ips114_show_float(112, 4 * MENU_ROW_HEIGHT, app.speed.speed_run, 4, 1);
    ips114_show_float(112, 5 * MENU_ROW_HEIGHT, app.speed.limiting_Err, 3, 3);
    ips114_show_float(112, 6 * MENU_ROW_HEIGHT, app.speed.kp2_Err, 3, 3);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

static void Menu_Draw_Model(int edit_line)
{
    ips114_show_string(8, 0, "<<MODEL");
    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "kp_Ang");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "kd_Ang");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "lim_Ang");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "A_1");
    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "B_1");
    ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "C_l");

    ips114_show_float(112, 1 * MENU_ROW_HEIGHT, app.angle.kp_Angle, 3, 3);
    ips114_show_float(112, 2 * MENU_ROW_HEIGHT, app.angle.kd_Angle, 3, 3);
    ips114_show_float(112, 3 * MENU_ROW_HEIGHT, app.angle.limiting_Angle, 3, 2);
    ips114_show_float(112, 4 * MENU_ROW_HEIGHT, app.angle.A_1, 3, 2);
    ips114_show_float(112, 5 * MENU_ROW_HEIGHT, app.angle.B_1, 3, 2);
    ips114_show_float(112, 6 * MENU_ROW_HEIGHT, app.angle.C_l, 3, 2);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

static void Menu_Draw_Sensor(void)
{
    ips114_show_string(8, 0, "<<SENSOR");
    ips114_show_string(64, 0, "NORM");
    ips114_show_string(112, 0, "RAW");

    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "LH");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "LV");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "RH");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "RV");

    ips114_show_int32(64, 1 * MENU_ROW_HEIGHT, ad1, 3);
    ips114_show_int32(64, 2 * MENU_ROW_HEIGHT, ad2, 3);
    ips114_show_int32(64, 3 * MENU_ROW_HEIGHT, ad3, 3);
    ips114_show_int32(64, 4 * MENU_ROW_HEIGHT, ad4, 3);

    ips114_show_int32(112, 1 * MENU_ROW_HEIGHT, RAW[0], 4);
    ips114_show_int32(112, 2 * MENU_ROW_HEIGHT, RAW[1], 4);
    ips114_show_int32(112, 3 * MENU_ROW_HEIGHT, RAW[2], 4);
    ips114_show_int32(112, 4 * MENU_ROW_HEIGHT, RAW[3], 4);

    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "Err");
    ips114_show_float(48, 5 * MENU_ROW_HEIGHT, Err, 4, 1);
    ips114_show_string(112, 5 * MENU_ROW_HEIGHT, "cyl");
    ips114_show_int32(144, 5 * MENU_ROW_HEIGHT, a_run_cylinder_get_state(), 1);
    ips114_show_string(168, 5 * MENU_ROW_HEIGHT, "rdeg");
    ips114_show_float(192, 5 * MENU_ROW_HEIGHT, imu_get_gravity_vz(), 4, 1);
}

/**
 * @brief 绘制元素参数导航页面（YUANSHU）。
 * @param edit_line 当前编辑行，0 表示根页导航模式。
 *
 * 包含 RING、CYLINDER、WALL、FLY 四个子页面入口，避免首页超过屏幕可见行。
 */
static void Menu_Draw_Yuanshu(int edit_line)
{
    ips114_show_string(8, 0, "<<YUANSHU");

    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "RING");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "CYLINDER");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "WALL");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "FLY");
    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "CROSS");

    ips114_show_string(105, 1 * MENU_ROW_HEIGHT, "S");
    ips114_show_int32(120, 1 * MENU_ROW_HEIGHT, a_run_ring_get_state(), 1);
    ips114_show_string(105, 2 * MENU_ROW_HEIGHT, "C");
    ips114_show_int32(120, 2 * MENU_ROW_HEIGHT, a_run_cylinder_get_state(), 1);
    ips114_show_string(105, 3 * MENU_ROW_HEIGHT, "W");
    ips114_show_int32(120, 3 * MENU_ROW_HEIGHT, a_run_wall_get_state(), 1);
    ips114_show_string(105, 4 * MENU_ROW_HEIGHT, "X");
    ips114_show_int32(120, 4 * MENU_ROW_HEIGHT, a_run_track_element_get_expected_element(), 1);
    ips114_show_string(105, 5 * MENU_ROW_HEIGHT, "R");
    ips114_show_int32(120, 5 * MENU_ROW_HEIGHT, a_run_cross_get_state(), 1);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

/**
 * @brief 绘制双十字参数子页面。
 * @param edit_line 当前编辑行，0 表示根页导航模式。
 */
static void Menu_Draw_Cross_Sub(int edit_line)
{
    ips114_show_string(8, 0, "<<CROSS");
    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "enc_target");

    ips114_show_float(112, 1 * MENU_ROW_HEIGHT, app.cross.encoder_target, 4, 1);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

/**
 * @brief 绘制圆环参数子页面。
 * @param edit_line 当前编辑行，0 表示根页导航模式。
 */
static void Menu_Draw_Ring_Sub(int edit_line)
{
    ips114_show_string(8, 0, "<<RING");
    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "entry_E");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "pre_r_T");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "pre_r_Gz");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "in_r_Gz");
    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "pre_o_T");
    ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "pre_o_Gz");
    ips114_show_string(16, 7 * MENU_ROW_HEIGHT, "drv_o_E");

    ips114_show_float(112, 1 * MENU_ROW_HEIGHT, app.ring.ring_entry_encoder, 3, 2);
    ips114_show_float(112, 2 * MENU_ROW_HEIGHT, app.ring.pre_ring_Gyro_target, 3, 2);
    ips114_show_float(112, 3 * MENU_ROW_HEIGHT, app.ring.pre_ring_Gyroz, 3, 2);
    ips114_show_float(112, 4 * MENU_ROW_HEIGHT, app.ring.in_ring_Gyroz, 3, 2);
    ips114_show_float(112, 5 * MENU_ROW_HEIGHT, app.ring.pre_out_ring_Gyro_target, 3, 2);
    ips114_show_float(112, 6 * MENU_ROW_HEIGHT, app.ring.pre_out_ring_Gyroz, 3, 2);
    ips114_show_float(112, 7 * MENU_ROW_HEIGHT, app.ring.drive_out_ring_encoder, 3, 2);

    /* 右侧只显示调参关键量，避免新增页面导致现场切换成本变高。 */
    ips114_show_string(168, 1 * MENU_ROW_HEIGHT, "S");
    ips114_show_int32(184, 1 * MENU_ROW_HEIGHT, a_run_ring_get_state(), 1);
    ips114_show_string(168, 2 * MENU_ROW_HEIGHT, "Yd");
    ips114_show_float(184, 2 * MENU_ROW_HEIGHT, ring_data.yaw_delta_sum, 4, 0);
    ips114_show_string(168, 3 * MENU_ROW_HEIGHT, "E");
    ips114_show_float(184, 3 * MENU_ROW_HEIGHT, ring_data.encoder, 4, 0);
    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

/**
 * @brief 绘制圆桶参数子页面。
 * @param edit_line 当前编辑行，0 表示根页导航模式。
 */
static void Menu_Draw_Cylinder_Sub(int edit_line)
{
    ips114_show_string(8, 0, "<<CYLINDER");

    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "cyl_enc");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "cyl_both");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "adc_a_1");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "adc_b_1");
    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "adc_c_l");
    ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "cyl_kp");
    ips114_show_string(16, 7 * MENU_ROW_HEIGHT, "cyl_kd");

    ips114_show_float(112, 1 * MENU_ROW_HEIGHT, app.cylinder.encoder_target, 3, 2);
    ips114_show_int32(112, 2 * MENU_ROW_HEIGHT, app.cylinder.ad_both_high_threshold, 3);
    ips114_show_float(112, 3 * MENU_ROW_HEIGHT, app.cylinder.adc_a_1, 3, 2);
    ips114_show_float(112, 4 * MENU_ROW_HEIGHT, app.cylinder.adc_b_1, 3, 2);
    ips114_show_float(112, 5 * MENU_ROW_HEIGHT, app.cylinder.adc_c_l, 3, 2);
    ips114_show_float(112, 6 * MENU_ROW_HEIGHT, app.cylinder.kp_Err, 3, 2);
    ips114_show_float(112, 7 * MENU_ROW_HEIGHT, app.cylinder.kd_Err, 3, 2);

    ips114_show_string(168, 1 * MENU_ROW_HEIGHT, "C");
    ips114_show_int32(184, 1 * MENU_ROW_HEIGHT, a_run_cylinder_get_state(), 1);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

/**
 * @brief 绘制墙面参数子页面。
 * @param edit_line 当前编辑行，0 表示根页导航模式。
 */
static void Menu_Draw_Wall_Sub(int edit_line)
{
    ips114_show_string(8, 0, "<<WALL");

    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "wall_spd");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "wall_slow_t");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "wall_timing");

    ips114_show_int32(112, 1 * MENU_ROW_HEIGHT, app.wall.slow_speed, 3);
    ips114_show_int32(112, 2 * MENU_ROW_HEIGHT, app.wall.slow_time, 3);
    ips114_show_int32(112, 3 * MENU_ROW_HEIGHT, app.wall.timing_count, 3);

    ips114_show_string(168, 1 * MENU_ROW_HEIGHT, "W");
    ips114_show_int32(184, 1 * MENU_ROW_HEIGHT, a_run_wall_get_state(), 1);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

/**
 * @brief 绘制飞坡参数子页面。
 * @param edit_line 当前编辑行，0 表示根页导航模式。
 */
static void Menu_Draw_Fly_Sub(int edit_line)
{
    ips114_show_string(8, 0, "<<SEESAW");
    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "seesaw_mode");
    ips114_show_int32(112, 1 * MENU_ROW_HEIGHT, app.fly.seesaw_mode, 1);

    if (app.fly.seesaw_mode == 0)
    {
        /* 飞坡模式参数 */
        ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "fly_speed");
        ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "detect_cnt");
        ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "airborne");
        ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "recover_spd");
        ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "release_stp");

        ips114_show_int32(112, 2 * MENU_ROW_HEIGHT, app.fly.fly_speed, 4);
        ips114_show_int32(112, 3 * MENU_ROW_HEIGHT, app.fly.fly_detect_count, 4);
        ips114_show_int32(112, 4 * MENU_ROW_HEIGHT, app.fly.fly_airborne_th, 4);
        ips114_show_int32(112, 5 * MENU_ROW_HEIGHT, app.fly.fly_recover_speed, 4);
        ips114_show_float(112, 6 * MENU_ROW_HEIGHT, app.fly.fly_release_step, 4, 2);
    }
    else
    {
        /* 停止等待模式参数 */
        ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "seesaw_spd");
        ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "detect_cnt");
        ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "wait_cnt");
        ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "creep_cm");
        ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "release_stp");

        ips114_show_int32(112, 2 * MENU_ROW_HEIGHT, app.fly.seesaw_speed, 4);
        ips114_show_int32(112, 3 * MENU_ROW_HEIGHT, app.fly.seesaw_detect_count, 4);
        ips114_show_int32(112, 4 * MENU_ROW_HEIGHT, app.fly.seesaw_wait_count, 4);
        ips114_show_float(112, 5 * MENU_ROW_HEIGHT, app.fly.seesaw_creep_cm, 4, 2);
        ips114_show_float(112, 6 * MENU_ROW_HEIGHT, app.fly.seesaw_release_step, 4, 2);
    }

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

/**
 * @brief 绘制元素序列长度编辑页。
 * @param edit_line 当前编辑行，固定为 MENU_ROW_MIN。
 *
 * 该页从 START 第 6 行进入，K3 会继续进入 E1~E6 槽位页。
 */
static void Menu_Draw_Element_Len(int edit_line)
{
    ips114_show_string(8, 0, "<<ELEM");
    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "LEN");
    ips114_show_int32(112, 1 * MENU_ROW_HEIGHT, app.start.element_len, 3);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

/**
 * @brief 绘制元素序列槽位页。
 * @param edit_line 当前编辑行，0 表示根页导航模式。
 *
 * E1~E6 的值直接对应 app.start.element_seq[]，不可执行编号由仲裁层跳过。
 */
static void Menu_Draw_Element(int edit_line)
{
    ips114_show_string(8, 0, "<<ELEM");
    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "E1");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "E2");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "E3");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "E4");
    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "E5");
    ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "E6");

    ips114_show_int32(112, 1 * MENU_ROW_HEIGHT, app.start.element_seq[0], 3);
    ips114_show_int32(112, 2 * MENU_ROW_HEIGHT, app.start.element_seq[1], 3);
    ips114_show_int32(112, 3 * MENU_ROW_HEIGHT, app.start.element_seq[2], 3);
    ips114_show_int32(112, 4 * MENU_ROW_HEIGHT, app.start.element_seq[3], 3);
    ips114_show_int32(112, 5 * MENU_ROW_HEIGHT, app.start.element_seq[4], 3);
    ips114_show_int32(112, 6 * MENU_ROW_HEIGHT, app.start.element_seq[5], 3);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

static void Menu_Process_Special_Value(int16 *parameter)
{
    uint8 event_code;
    uint8 changed;

    changed = 0;
    ips114_show_string(MENU_STEP_X, 0, "0/1");

    event_code = Menu_Read_Key_Event();
    if (event_code == 0)
        return;

    Menu_Handle_Common_Key(keystroke_label);

    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
    case KEYSTROKE_ONE_LONG:
        *parameter = 1;
        changed = 1;
        break;
    case KEYSTROKE_TWO:
    case KEYSTROKE_TWO_LONG:
        *parameter = 0;
        changed = 1;
        break;
    default:
        break;
    }

    if (changed)
        control_apply_config();
}

static void Menu_Process_Int_Value(int *parameter, int change_unit_min)
{
    uint8 event_code;
    int unit;
    uint8 changed;

    unit = change_unit_min * change_unit_multiplier;
    changed = 0;

    ips114_show_int32(MENU_STEP_INT_X, 0, unit, 4);

    event_code = Menu_Read_Key_Event();
    if (event_code == 0)
        return;

    Menu_Handle_Common_Key(keystroke_label);

    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
    case KEYSTROKE_ONE_LONG:
        *parameter += unit;
        changed = 1;
        break;
    case KEYSTROKE_TWO:
    case KEYSTROKE_TWO_LONG:
        *parameter -= unit;
        changed = 1;
        break;
    default:
        break;
    }

    if (changed)
        control_apply_config();
}

static void Menu_Process_Float_Value(float *parameter, float change_unit_min)
{
    uint8 event_code;
    float unit;
    uint8 changed;

    unit = change_unit_min * (float)change_unit_multiplier;
    changed = 0;

    ips114_show_float(MENU_STEP_X, 0, unit, 4, 3);

    event_code = Menu_Read_Key_Event();
    if (event_code == 0)
        return;

    Menu_Handle_Common_Key(keystroke_label);

    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
    case KEYSTROKE_ONE_LONG:
        *parameter += unit;
        changed = 1;
        break;
    case KEYSTROKE_TWO:
    case KEYSTROKE_TWO_LONG:
        *parameter -= unit;
        changed = 1;
        break;
    default:
        break;
    }

    if (changed)
        control_apply_config();
}

static void Keystroke_Menu_HOME(void)
{
    uint8 event_code;

    Menu_Draw_Home();
    Menu_Draw_Navigation_Cursor(MENU_HOME_ROW_MAX);
    event_code = Menu_Read_Key_Event();
    if (event_code == 0)
        return;

    Menu_Cursor_Update(MENU_HOME_ROW_MAX);

    if (menu_next_flag == 1)
    {
        Menu_Next_Back();
    }
    else if (menu_next_flag == -1 && EEPROM_MODE == 1)
    {
        config_save();
        Menu_Show_Save_Prompt();
        menu_next_flag = 0;
    }
}

static void Menu_Start_Process(void)
{
    uint8 event_code;

    switch (display_codename)
    {
    case 1:
        Menu_Draw_Start(0);
        Menu_Draw_Navigation_Cursor(5 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(5 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 11:
        Menu_Draw_Start(1 * MENU_ROW_HEIGHT);
        Menu_Process_Special_Value(&app.start.start_flag);
        break;
    case 12:
        Menu_Draw_Start(2 * MENU_ROW_HEIGHT);
        Menu_Process_Special_Value(&app.start.element_enable);
        break;
    case 13:
        Menu_Draw_Start(3 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.start.fuya_xili, 1.0f);
        break;
    case 14:
        Menu_Draw_Start(4 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.angle.gyro_feedback_scale, 0.1f);
        break;
    case 15:
        Menu_Element_Len_Process();
        break;
    default:
        break;
    }
}

/**
 * @brief 处理元素序列长度编辑页。
 *
 * K1/K2 在 1~6 内循环修改 LEN；K3 进入 E1~E6 槽位页；K4 返回 START 根页。
 */
static void Menu_Element_Len_Process(void)
{
    uint8 event_code;
    uint8 changed;

    changed = 0;
    Menu_Draw_Element_Len(MENU_ROW_MIN);

    event_code = Menu_Read_Key_Event();
    if (event_code == 0)
        return;

    switch (keystroke_label)
    {
    case KEYSTROKE_ONE:
    case KEYSTROKE_ONE_LONG:
        app.start.element_len++;
        if (app.start.element_len > TRACK_ELEMENT_SEQUENCE_MAX)
        {
            app.start.element_len = 1;
        }
        changed = 1;
        break;
    case KEYSTROKE_TWO:
    case KEYSTROKE_TWO_LONG:
        app.start.element_len--;
        if (app.start.element_len < 1)
        {
            app.start.element_len = TRACK_ELEMENT_SEQUENCE_MAX;
        }
        changed = 1;
        break;
    case KEYSTROKE_THREE:
        Menu_Save_Page_Position(display_codename, cursor_row);
        display_codename = 160;
        cursor_row = MENU_ROW_MIN;
        previous_cursor_row = -1;
        Menu_Clear_Page();
        Menu_Render_Current_Page();
        Menu_Clear_Pending_Key_Events();
        break;
    case KEYSTROKE_FOUR:
    case KEYSTROKE_FOUR_LONG:
        Menu_Save_Page_Position(display_codename, cursor_row);
        display_codename = 1;
        cursor_row = Menu_Load_Page_Position(display_codename);
        previous_cursor_row = -1;
        Menu_Clear_Page();
        Menu_Render_Current_Page();
        Menu_Clear_Pending_Key_Events();
        break;
    default:
        break;
    }

    if (changed)
    {
        control_apply_config();
    }
}

/**
 * @brief 处理元素序列槽位页。
 *
 * 根页负责 E1~E6 导航；单槽位复用普通 int 编辑，非法元素值由运行期仲裁跳过。
 */
static void Menu_Element_Process(void)
{
    uint8 event_code;

    switch (display_codename)
    {
    case 160:
        Menu_Draw_Element(0);
        Menu_Draw_Navigation_Cursor(6 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(6 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 1601:
        Menu_Draw_Element(1 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.start.element_seq[0], 1);
        break;
    case 1602:
        Menu_Draw_Element(2 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.start.element_seq[1], 1);
        break;
    case 1603:
        Menu_Draw_Element(3 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.start.element_seq[2], 1);
        break;
    case 1604:
        Menu_Draw_Element(4 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.start.element_seq[3], 1);
        break;
    case 1605:
        Menu_Draw_Element(5 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.start.element_seq[4], 1);
        break;
    case 1606:
        Menu_Draw_Element(6 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.start.element_seq[5], 1);
        break;
    default:
        break;
    }
}

static void Menu_Speed_Process(void)
{
    uint8 event_code;

    switch (display_codename)
    {
    case 2:
        Menu_Draw_Speed(0);
        Menu_Draw_Navigation_Cursor(6 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(6 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 21:
        Menu_Draw_Speed(1 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.speed.kp_Err, 0.01f);
        break;
    case 22:
        Menu_Draw_Speed(2 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.speed.kd_Err, 0.01f);
        break;
    case 23:
        Menu_Draw_Speed(3 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.speed.gyro_damp_Err, 0.001f);
        break;
    case 24:
        Menu_Draw_Speed(4 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.speed.speed_run, 1.0f);
        break;
    case 25:
        Menu_Draw_Speed(5 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.speed.limiting_Err, 1.0f);
        break;
    case 26:
        Menu_Draw_Speed(6 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.speed.kp2_Err, 0.001f);
        break;
    default:
        break;
    }
}

static void Menu_Model_Process(void)
{
    uint8 event_code;

    switch (display_codename)
    {
    case 3:
        Menu_Draw_Model(0);
        Menu_Draw_Navigation_Cursor(6 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(6 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 31:
        Menu_Draw_Model(1 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.angle.kp_Angle, 0.01f);
        break;
    case 32:
        Menu_Draw_Model(2 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.angle.kd_Angle, 0.01f);
        break;
    case 33:
        Menu_Draw_Model(3 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.angle.limiting_Angle, 1.0f);
        break;
    case 34:
        Menu_Draw_Model(4 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.angle.A_1, 0.01f);
        break;
    case 35:
        Menu_Draw_Model(5 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.angle.B_1, 0.01f);
        break;
    case 36:
        Menu_Draw_Model(6 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.angle.C_l, 0.01f);
        break;
    default:
        break;
    }
}

static void Menu_Sensor_Process(void)
{
    uint8 event_code;

    Menu_Draw_Sensor();
    event_code = Menu_Read_Key_Event();
    if (event_code == 0)
        return;

    switch (keystroke_label)
    {
    case KEYSTROKE_FOUR:
    case KEYSTROKE_FOUR_LONG:
        menu_next_flag = -1;
        break;
    default:
        break;
    }

    if (menu_next_flag != 0)
        Menu_Next_Back();
}

static void Menu_Yuanshu_Process(void)
{
    uint8 event_code;

    switch (display_codename)
    {
    case 4:
        Menu_Draw_Yuanshu(0);
        Menu_Draw_Navigation_Cursor(5 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(5 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 41:
        Menu_Draw_Ring_Sub(0);
        Menu_Draw_Navigation_Cursor(7 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(7 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 411:
        Menu_Draw_Ring_Sub(1 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.ring_entry_encoder, 1.0f);
        break;
    case 412:
        Menu_Draw_Ring_Sub(2 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.pre_ring_Gyro_target, 1.0f);
        break;
    case 413:
        Menu_Draw_Ring_Sub(3 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.pre_ring_Gyroz, 1.0f);
        break;
    case 414:
        Menu_Draw_Ring_Sub(4 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.in_ring_Gyroz, 1.0f);
        break;
    case 415:
        Menu_Draw_Ring_Sub(5 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.pre_out_ring_Gyro_target, 1.0f);
        break;
    case 416:
        Menu_Draw_Ring_Sub(6 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.pre_out_ring_Gyroz, 1.0f);
        break;
    case 417:
        Menu_Draw_Ring_Sub(7 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.drive_out_ring_encoder, 1.0f);
        break;
    case 42:
        Menu_Draw_Cylinder_Sub(0);
        Menu_Draw_Navigation_Cursor(7 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(7 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 421:
        Menu_Draw_Cylinder_Sub(1 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.cylinder.encoder_target, 10.0f);
        break;
    case 422:
        Menu_Draw_Cylinder_Sub(2 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.cylinder.ad_both_high_threshold, 5);
        break;
    case 423:
        Menu_Draw_Cylinder_Sub(3 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.cylinder.adc_a_1, 0.1f);
        break;
    case 424:
        Menu_Draw_Cylinder_Sub(4 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.cylinder.adc_b_1, 0.1f);
        break;
    case 425:
        Menu_Draw_Cylinder_Sub(5 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.cylinder.adc_c_l, 0.1f);
        break;
    case 426:
        Menu_Draw_Cylinder_Sub(6 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.cylinder.kp_Err, 0.1f);
        break;
    case 427:
        Menu_Draw_Cylinder_Sub(7 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.cylinder.kd_Err, 0.1f);
        break;
    case 43:
        Menu_Draw_Wall_Sub(0);
        Menu_Draw_Navigation_Cursor(3 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(3 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 431:
        Menu_Draw_Wall_Sub(1 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.wall.slow_speed, 5);
        break;
    case 432:
        Menu_Draw_Wall_Sub(2 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.wall.slow_time, 10);
        break;
    case 433:
        Menu_Draw_Wall_Sub(3 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.wall.timing_count, 10);
        break;
    case 44:
        Menu_Draw_Fly_Sub(0);
        Menu_Draw_Navigation_Cursor(6 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(6 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 441:
        Menu_Draw_Fly_Sub(1 * MENU_ROW_HEIGHT);
        Menu_Process_Special_Value(&app.fly.seesaw_mode);
        break;
    case 442:
        Menu_Draw_Fly_Sub(2 * MENU_ROW_HEIGHT);
        if (app.fly.seesaw_mode == 0)
            Menu_Process_Int_Value(&app.fly.fly_speed, 1);
        else
            Menu_Process_Int_Value(&app.fly.seesaw_speed, 1);
        break;
    case 443:
        Menu_Draw_Fly_Sub(3 * MENU_ROW_HEIGHT);
        if (app.fly.seesaw_mode == 0)
            Menu_Process_Int_Value(&app.fly.fly_detect_count, 1);
        else
            Menu_Process_Int_Value(&app.fly.seesaw_detect_count, 1);
        break;
    case 444:
        Menu_Draw_Fly_Sub(4 * MENU_ROW_HEIGHT);
        if (app.fly.seesaw_mode == 0)
            Menu_Process_Int_Value(&app.fly.fly_airborne_th, 1);
        else
            Menu_Process_Int_Value(&app.fly.seesaw_wait_count, 10);
        break;
    case 445:
        Menu_Draw_Fly_Sub(5 * MENU_ROW_HEIGHT);
        if (app.fly.seesaw_mode == 0)
            Menu_Process_Int_Value(&app.fly.fly_recover_speed, 1);
        else
            Menu_Process_Float_Value(&app.fly.seesaw_creep_cm, 0.1f);
        break;
    case 446:
        Menu_Draw_Fly_Sub(6 * MENU_ROW_HEIGHT);
        if (app.fly.seesaw_mode == 0)
            Menu_Process_Float_Value(&app.fly.fly_release_step, 0.01f);
        else
            Menu_Process_Float_Value(&app.fly.seesaw_release_step, 0.01f);
        break;
    case 45:
        Menu_Draw_Cross_Sub(0);
        Menu_Draw_Navigation_Cursor(1 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(1 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 451:
        Menu_Draw_Cross_Sub(1 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.cross.encoder_target, 1.0f);
        break;
    default:
        break;
    }
}

void Keystroke_Menu(void)
{
    if (!menu_service_enabled)
        return;

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
        Menu_Model_Process();
        break;
    case 4:
    case 41:
    case 411:
    case 412:
    case 413:
    case 414:
    case 415:
    case 416:
    case 42:
    case 421:
    case 422:
    case 423:
    case 424:
    case 425:
    case 426:
    case 427:
    case 43:
    case 431:
    case 432:
    case 433:
    case 44:
    case 441:
    case 442:
    case 443:
    case 444:
    case 445:
    case 446:
    case 45:
    case 451:
        Menu_Yuanshu_Process();
        break;
    case 5:
        Menu_Sensor_Process();
        break;
    case 160:
    case 1601:
    case 1602:
    case 1603:
    case 1604:
    case 1605:
    case 1606:
        Menu_Element_Process();
        break;
    default:
        display_codename = 0;
        Menu_Reset_Cursor();
        break;
    }
}

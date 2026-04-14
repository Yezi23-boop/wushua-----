#include "zf_common_headfile.h"
#include "menu.h"

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
#define MENU_HOME_ROW_MAX (6 * MENU_ROW_HEIGHT)
#define MENU_PAGE_COUNT 7
#define MENU_CENTER_X (12 * 8)
#define MENU_STEP_X (14 * 8)
#define MENU_STEP_INT_X (15 * 8)
#define EEPROM_MODE 1

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
    1, 11, 12, 13, 14,
    2, 21, 22, 23, 24, 25, 26,
    3, 31, 32, 33,
    4,
    5, 51, 52, 53, 54, 55, 56,
    6, 61, 62, 63, 64, 65};

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
static void Menu_Next_Back(void);
static void Menu_Handle_Common_Key(int label);
static void Menu_Show_Save_Prompt(void);
static void Menu_Draw_Home(void);
static void Menu_Draw_Start(int edit_line);
static void Menu_Draw_Speed(int edit_line);
static void Menu_Draw_Model(int edit_line);
static void Menu_Draw_Sensor(void);
static void Menu_Draw_Ring(int edit_line);
static void Menu_Draw_Fly(int edit_line);
static void Menu_Process_Special_Value(int16 *parameter);
static void Menu_Process_Int_Value(int *parameter, int change_unit_min);
static void Menu_Process_Float_Value(float *parameter, float change_unit_min);
static void Keystroke_Menu_HOME(void);
static void Menu_Start_Process(void);
static void Menu_Speed_Process(void);
static void Menu_Model_Process(void);
static void Menu_Sensor_Process(void);
static void Menu_Ring_Process(void);
static void Menu_Fly_Process(void);

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
        return 4 * MENU_ROW_HEIGHT;
    case 2:
        return 6 * MENU_ROW_HEIGHT;
    case 3:
        return 3 * MENU_ROW_HEIGHT;
    case 4:
        return MENU_ROW_MIN;
    case 5:
        return 6 * MENU_ROW_HEIGHT;
    case 6:
        return 5 * MENU_ROW_HEIGHT;
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
    ips114_show_string(MENU_CENTER_X - 24, 3 * MENU_ROW_HEIGHT, "SAVED OK!");
    previous_cursor_row = -1;
}

static void Menu_Draw_Home(void)
{
    ips114_show_string(MENU_CENTER_X, 0, "MENU");

    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "START");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "CTRL");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "MODEL");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "SENSOR");
    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "RING");
    ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "FLY");

    ips114_show_string(105, 1 * MENU_ROW_HEIGHT, "Err");
    ips114_show_string(105, 2 * MENU_ROW_HEIGHT, "steer");
    ips114_show_string(105, 3 * MENU_ROW_HEIGHT, "gyro_z");
    ips114_show_string(105, 4 * MENU_ROW_HEIGHT, "V_bat");
    ips114_show_string(105, 5 * MENU_ROW_HEIGHT, "L_tar");
    ips114_show_string(105, 6 * MENU_ROW_HEIGHT, "R_tar");

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
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "circle_flags");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "fuya_ground");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "fuya_wall");

    ips114_show_int32(112, 1 * MENU_ROW_HEIGHT, app.start.start_flag, 3);
    ips114_show_int32(112, 2 * MENU_ROW_HEIGHT, app.start.circle_flags, 3);
    ips114_show_float(112, 3 * MENU_ROW_HEIGHT, app.start.fuya_xili, 4, 1);
    ips114_show_float(112, 4 * MENU_ROW_HEIGHT, app.start.fuya_wall_percent, 4, 1);

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
    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "A_1");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "B_1");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "C_l");

    ips114_show_float(112, 1 * MENU_ROW_HEIGHT, app.angle.A_1, 3, 2);
    ips114_show_float(112, 2 * MENU_ROW_HEIGHT, app.angle.B_1, 3, 2);
    ips114_show_float(112, 3 * MENU_ROW_HEIGHT, app.angle.C_l, 3, 2);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

static void Menu_Draw_Sensor(void)
{
    ips114_show_string(8, 0, "<<SENSOR");
    ips114_show_string(56, 0, "NORM");
    ips114_show_string(112, 0, "RAW");
    ips114_show_string(176, 0, "MAX");

    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "ad1");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "ad2");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "ad3");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "ad4");
    ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "Err");

    ips114_show_int32(56, 1 * MENU_ROW_HEIGHT, ad1, 3);
    ips114_show_int32(56, 2 * MENU_ROW_HEIGHT, ad2, 3);
    ips114_show_int32(56, 3 * MENU_ROW_HEIGHT, ad3, 3);
    ips114_show_int32(56, 4 * MENU_ROW_HEIGHT, ad4, 3);

    ips114_show_int32(112, 1 * MENU_ROW_HEIGHT, RAW[0], 4);
    ips114_show_int32(112, 2 * MENU_ROW_HEIGHT, RAW[1], 4);
    ips114_show_int32(112, 3 * MENU_ROW_HEIGHT, RAW[2], 4);
    ips114_show_int32(112, 4 * MENU_ROW_HEIGHT, RAW[3], 4);

    ips114_show_int32(176, 1 * MENU_ROW_HEIGHT, MA[0], 4);
    ips114_show_int32(176, 2 * MENU_ROW_HEIGHT, MA[1], 4);
    ips114_show_int32(176, 3 * MENU_ROW_HEIGHT, MA[2], 4);
    ips114_show_int32(176, 4 * MENU_ROW_HEIGHT, MA[3], 4);

    ips114_show_float(56, 6 * MENU_ROW_HEIGHT, Err, 4, 1);
}

static void Menu_Draw_Ring(int edit_line)
{
    ips114_show_string(8, 0, "<<RING");
    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "ring_en");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "pre_r_G");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "in_r_G");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "pre_o_G");
    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "pre_o_Gz");
    ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "pre_o_en");

    ips114_show_float(112, 1 * MENU_ROW_HEIGHT, app.ring.ring_encoder, 3, 2);
    ips114_show_float(112, 2 * MENU_ROW_HEIGHT, app.ring.pre_ring_Gyro_set, 3, 2);
    ips114_show_float(112, 3 * MENU_ROW_HEIGHT, app.ring.in_ring_Gyroz, 3, 2);
    ips114_show_float(112, 4 * MENU_ROW_HEIGHT, app.ring.pre_out_ring_Gyro_set, 3, 2);
    ips114_show_float(112, 5 * MENU_ROW_HEIGHT, app.ring.pre_out_ring_Gyroz, 3, 2);
    ips114_show_float(112, 6 * MENU_ROW_HEIGHT, app.ring.pre_out_ring_encoder, 3, 2);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

static void Menu_Draw_Fly(int edit_line)
{
    ips114_show_string(8, 0, "<<FLY");
    ips114_show_string(16, 1 * MENU_ROW_HEIGHT, "fly_speed");
    ips114_show_string(16, 2 * MENU_ROW_HEIGHT, "fly_time_1");
    ips114_show_string(16, 3 * MENU_ROW_HEIGHT, "fly_time_2");
    ips114_show_string(16, 4 * MENU_ROW_HEIGHT, "fly_angle");
    ips114_show_string(16, 5 * MENU_ROW_HEIGHT, "fly_en");

    ips114_show_int32(112, 1 * MENU_ROW_HEIGHT, app.fly.count_fly_speed, 4);
    ips114_show_int32(112, 2 * MENU_ROW_HEIGHT, app.fly.count_fly_time_1, 4);
    ips114_show_int32(112, 3 * MENU_ROW_HEIGHT, app.fly.count_fly_time_2, 4);
    ips114_show_int32(112, 4 * MENU_ROW_HEIGHT, app.fly.count_fly_angle, 4);
    ips114_show_int32(112, 5 * MENU_ROW_HEIGHT, app.fly.fly_ramp_enable, 4);

    if (edit_line >= MENU_ROW_MIN)
        ips114_show_string(0, edit_line, ">>");
}

static void Menu_Process_Special_Value(int16 *parameter)
{
    uint8 event_code;
    uint8 changed;

    changed = 0;
    ips114_show_string(MENU_STEP_X, 0, "SPCL");

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
        *parameter = -1;
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
        Menu_Draw_Navigation_Cursor(4 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(4 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 11:
        Menu_Draw_Start(1 * MENU_ROW_HEIGHT);
        Menu_Process_Special_Value(&app.start.start_flag);
        break;
    case 12:
        Menu_Draw_Start(2 * MENU_ROW_HEIGHT);
        Menu_Process_Special_Value(&app.start.circle_flags);
        break;
    case 13:
        Menu_Draw_Start(3 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.start.fuya_xili, 1.0f);
        break;
    case 14:
        Menu_Draw_Start(4 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.start.fuya_wall_percent, 1.0f);
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
        Menu_Process_Float_Value(&app.speed.limiting_Err, 0.01f);
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
        Menu_Draw_Navigation_Cursor(3 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(3 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 31:
        Menu_Draw_Model(1 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.angle.A_1, 0.01f);
        break;
    case 32:
        Menu_Draw_Model(2 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.angle.B_1, 0.01f);
        break;
    case 33:
        Menu_Draw_Model(3 * MENU_ROW_HEIGHT);
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

static void Menu_Ring_Process(void)
{
    uint8 event_code;

    switch (display_codename)
    {
    case 5:
        Menu_Draw_Ring(0);
        Menu_Draw_Navigation_Cursor(6 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(6 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 51:
        Menu_Draw_Ring(1 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.ring_encoder, 1.0f);
        break;
    case 52:
        Menu_Draw_Ring(2 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.pre_ring_Gyro_set, 10.0f);
        break;
    case 53:
        Menu_Draw_Ring(3 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.in_ring_Gyroz, 10.0f);
        break;
    case 54:
        Menu_Draw_Ring(4 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.pre_out_ring_Gyro_set, 10.0f);
        break;
    case 55:
        Menu_Draw_Ring(5 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.pre_out_ring_Gyroz, 10.0f);
        break;
    case 56:
        Menu_Draw_Ring(6 * MENU_ROW_HEIGHT);
        Menu_Process_Float_Value(&app.ring.pre_out_ring_encoder, 1.0f);
        break;
    default:
        break;
    }
}

static void Menu_Fly_Process(void)
{
    uint8 event_code;

    switch (display_codename)
    {
    case 6:
        Menu_Draw_Fly(0);
        Menu_Draw_Navigation_Cursor(5 * MENU_ROW_HEIGHT);
        event_code = Menu_Read_Key_Event();
        if (event_code == 0)
            return;
        Menu_Cursor_Update(5 * MENU_ROW_HEIGHT);
        if (menu_next_flag != 0)
            Menu_Next_Back();
        break;
    case 61:
        Menu_Draw_Fly(1 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.fly.count_fly_speed, 1);
        break;
    case 62:
        Menu_Draw_Fly(2 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.fly.count_fly_time_1, 1);
        break;
    case 63:
        Menu_Draw_Fly(3 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.fly.count_fly_time_2, 1);
        break;
    case 64:
        Menu_Draw_Fly(4 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.fly.count_fly_angle, 1);
        break;
    case 65:
        Menu_Draw_Fly(5 * MENU_ROW_HEIGHT);
        Menu_Process_Int_Value(&app.fly.fly_ramp_enable, 1);
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
        Menu_Model_Process();
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
        Menu_Ring_Process();
        break;
    case 6:
    case 61:
    case 62:
    case 63:
    case 64:
    case 65:
        Menu_Fly_Process();
        break;
    default:
        display_codename = 0;
        Menu_Reset_Cursor();
        break;
    }
}

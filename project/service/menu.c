#include "zf_common_headfile.h"
#include "menu.h"

/* ??????????? */
#define KEYSTROKE_ONE 1   /* ?? */
#define KEYSTROKE_TWO 2   /* ?? */
#define KEYSTROKE_THREE 3 /* ???/???????? */
#define KEYSTROKE_FOUR 4  /* ????/???? */

/* ??????????????? */
#define KEYSTROKE_ONE_LONG 5
#define KEYSTROKE_TWO_LONG 6
#define KEYSTROKE_THREE_LONG 7
#define KEYSTROKE_FOUR_LONG 8

/* ?????????? */
#define ROWS_MAX (7 * 18)      /* ???????????? */
#define ROWS_MIN (1 * 18)      /* ????????????? */
#define CENTER_COLUMN (12 * 8) /* ???????????? */
#define EEPROM_MODE 1          /* ????? EEPROM ??????? */

/* ???????????? */
int display_codename = 0;       /* ?????? ID */
int cursor_row = 2 * 18;        /* ?????????? */
int previous_cursor_row = -1;   /* ?????????????????????? */
int menu_next_flag = 0;         /* 1:????, -1:????, 0:?????? */
int change_unit_multiplier = 1; /* ??????????????1, 10, 100?? */
int keystroke_three_count = 0;  /* ???????????????????????? */

/* ?????????????? */
static void show_config_saved_prompt(void);

/* ????????????????? ID ?????????????????? */
int menu_have_sub[] = {
    0, 1, 11, 12, 13, 14, 15, 16, 17,
    2, 21, 22, 23, 24, 25,
    3, 31, 32, 33, 34, 35, 36,
    4,
    5, 51, 52, 53, 54, 55, 56,
    6, 61, 62, 63, 64, 65};

/* --- ??????????? --- */

/**
 * @brief ?????????????????????
 * @details ????????????????????????????
 */
void Cursor(void)
{
    menu_next_flag = 0;
    switch (keystroke_label)
    {
    case KEYSTROKE_ONE: /* ?????????? */
        cursor_row = (cursor_row > ROWS_MIN) ? cursor_row - 18 : ROWS_MAX;
        break;
    case KEYSTROKE_TWO: /* ?????????? */
        cursor_row = (cursor_row < ROWS_MAX) ? cursor_row + 18 : ROWS_MIN;
        break;
    case KEYSTROKE_THREE: /* ???????? */
        menu_next_flag = 1;
        break;
    case KEYSTROKE_FOUR: /* ????????? */
        menu_next_flag = -1;
        break;
    }

    /* ????????????? ">" */
    ips114_show_string(0, cursor_row, ">");

    /* ????????????????????????????? */
    if (previous_cursor_row != cursor_row)
    {
        ips114_show_string(0, previous_cursor_row, " ");
        previous_cursor_row = cursor_row;
    }
}

/**
 * @brief ?????????????
 * @details ???? cursor_row ?????????? ID ??????
 */
void Menu_Next_Back(void)
{
    int menu_id = 0;
    switch (menu_next_flag)
    {
    case -1: /* ?????????ID ???? 10 */
        display_codename /= 10;
        cursor_row = ROWS_MIN;
        ips114_clear(RGB565_WHITE);
        break;
    case 1: /* ?????????ID * 10 + ?????? */
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
 * @brief ??????? ID ???????????????
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
 * @brief ???????????????????
 * @details ???????????????????????????
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
        /* ?????????? 1, 10, 100 ?????? */
        change_unit_multiplier = (keystroke_three_count % 3 == 0) ? 1 : (keystroke_three_count % 3 == 1) ? 10
                                                                                                         : 100;
        if (keystroke_three_count >= 3)
            keystroke_three_count = 0;
        break;
    }
}

/* --- ?????????? --- */

/**
 * @brief ????????????
 */
void Keystroke_int(int *parameter, int change_unit_MIN)
{
    int unit = change_unit_MIN * change_unit_multiplier;
    uint8 changed = 0;

    /* ?????????????? */
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

    /* ?????????????????????????????????????????? PID ?????? */
    if (changed)
        control_apply_config();
}

/**
 * @brief ??????????????
 */
void Keystroke_float(float *parameter, float change_unit_MIN)
{
    float unit;
    uint8 changed = 0;

    /* ??????????????????????????? */
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
 * @brief ??????????????????? 1 ?? -1 ??????
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

/* --- ???????????? --- */

void Keystroke_Menu(void)
{
    /*
     * ????????? ID ??????????
     * ????????????????????????????????????
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
 * @brief ???????HOME?????
 * @details ?????????????????????????????????????
 */
void Keystroke_Menu_HOME(void)
{
    while (menu_next_flag == 0)
    {
        /* ??????? */
        ips114_show_string(CENTER_COLUMN, 0, "MENU");

        /* ???????????? */
        ips114_show_string(16, 1 * 18, "STRAT");  /* ???????? */
        ips114_show_string(16, 2 * 18, "PID_1");  /* ??? PID */
        ips114_show_string(16, 3 * 18, "PID_2");  /* ??? PID */
        ips114_show_string(16, 4 * 18, "PRINTF"); /* ??????/??????? */
        ips114_show_string(16, 5 * 18, "RING");   /* ??????? */
        ips114_show_string(16, 6 * 18, "FLY");    /* ??????? */

        /* ????????????? */
        ips114_show_string(105, 1 * 18, "Err");
        ips114_show_string(105, 2 * 18, "steer");
        ips114_show_string(105, 3 * 18, "angle");
        ips114_show_string(105, 4 * 18, "V_bat");

        /* ?????????????? */
        ips114_show_float(184, 1 * 18, Err, 3, 2);
        ips114_show_float(184, 2 * 18, PID.steer.output, 3, 1);
        ips114_show_float(184, 3 * 18, PID.angle.output, 3, 1);
        ips114_show_float(184, 4 * 18, dianya, 4, 2);

        Keystroke_Scan();
        Cursor();
    }

    /* ?????????????????????????? */
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
    /* ??????????????????????????????? EEPROM ??????? */
    else if (menu_next_flag == -1 && EEPROM_MODE == 1)
    {
        config_save();
        show_config_saved_prompt();
    }

    menu_next_flag = 0;
}

/**
 * @brief ??????????????
 */
static void show_config_saved_prompt(void)
{
    ips114_clear(RGB565_WHITE);
    ips114_show_string(CENTER_COLUMN - 24, 3 * 18, "SAVED OK!");
    system_delay_ms(300);
    ips114_clear(RGB565_WHITE);
}

/* --- ?????????????? (ID: 1x) --- */

void Menu_Start_Show(uint8 line)
{
    ips114_show_string(32, 0, "<<STRAT");
    ips114_show_string(8, 1 * 18, "Start_Flag");
    ips114_show_string(8, 2 * 18, "circle_flags");
    ips114_show_string(8, 3 * 18, "fuya_xili");

    ips114_show_int32(112, 1 * 18, app.start.start_flag, 3);
    ips114_show_int32(112, 2 * 18, app.start.circle_flags, 3);
    ips114_show_float(112, 3 * 18, app.start.fuya_xili, 4, 3);

    /* ???????? */
    ips114_show_string(0, line, (line == 1) ? " " : "&");
}

void Menu_Start_Process(void)
{
    switch (display_codename)
    {
    case 1: /* ??????? */
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

/* --- ??? PID ?????? (ID: 2x) --- */

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

/* --- ??? PID ?????? (ID: 3x) --- */

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

/* --- ???????????? (ID: 4) --- */

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

    /* Row 0 is reserved for titles to avoid overlapping data. */
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

/* --- ????????????????????????... --- */
/* (????????????? Menu_Circle_Process ?? Menu_Fly_Process) */
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

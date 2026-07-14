/**
 * @file menu.c
 * @brief IPS114 参数菜单与按键交互实现。
 *
 * 页面、行项目和编辑规则由只读描述表集中定义，运行期只保留当前页面、
 * 光标和编辑状态，避免每个页面各自维护绘制与按键分支。
 */
#include "zf_common_headfile.h"
#include "menu.h"
#include "../user/a_run_mode.h"

#define KEYSTROKE_ONE 1
#define KEYSTROKE_TWO 2
#define KEYSTROKE_THREE 3
#define KEYSTROKE_FOUR 4
#define KEYSTROKE_ONE_LONG 5
#define KEYSTROKE_TWO_LONG 6
#define KEYSTROKE_FOUR_LONG 8

#define MENU_ROW_HEIGHT 18
#define MENU_CENTER_X (12 * 8)
#define MENU_STEP_X (14 * 8)
#define MENU_STEP_INT_X (15 * 8)
#define MENU_SAVE_PROMPT_DELAY_MS 300
#define MENU_PAGE_NONE 0xFFu

#define MENU_TYPE_SHIFT 5
#define MENU_WIDTH_SHIFT 2
#define MENU_TYPE_MASK 0xE0u
#define MENU_WIDTH_MASK 0x1Cu
#define MENU_DECIMAL_MASK 0x03u
#define MENU_META(type, width, decimal) \
    ((uint8)(((type) << MENU_TYPE_SHIFT) | ((width) << MENU_WIDTH_SHIFT) | (decimal)))

/** @brief 菜单行类型。 */
typedef enum
{
    MENU_ITEM_FLOAT = 0,
    MENU_ITEM_INT16,
    MENU_ITEM_BOOL,
    MENU_ITEM_LINK,
    MENU_ITEM_LENGTH
} MenuItemType;

/** @brief 浮点参数步长索引。 */
typedef enum
{
    MENU_FLOAT_STEP_0001 = 0,
    MENU_FLOAT_STEP_001,
    MENU_FLOAT_STEP_01,
    MENU_FLOAT_STEP_1,
    MENU_FLOAT_STEP_5,
    MENU_FLOAT_STEP_10
} MenuFloatStep;

/** @brief 整数参数步长。 */
typedef enum
{
    MENU_INT_STEP_1 = 1,
    MENU_INT_STEP_5 = 5,
    MENU_INT_STEP_10 = 10
} MenuIntStep;

/** @brief 菜单页面编号。 */
typedef enum
{
    MENU_PAGE_HOME = 0,
    MENU_PAGE_START,
    MENU_PAGE_SPEED,
    MENU_PAGE_MODEL,
    MENU_PAGE_DIFF,
    MENU_PAGE_YUANSHU,
    MENU_PAGE_SENSOR,
    MENU_PAGE_RING,
    MENU_PAGE_CYLINDER,
    MENU_PAGE_WALL,
    MENU_PAGE_FLY,
    MENU_PAGE_CROSS,
    MENU_PAGE_ELEMENT_LEN,
    MENU_PAGE_ELEMENT,
    MENU_PAGE_COUNT
} MenuPageId;

/**
 * @brief 菜单行描述。
 *
 * meta 高 3 位为类型、中 3 位为整数宽度、低 2 位为小数位数。
 * action 对浮点项表示步长编号，对整数项表示基础步长，对链接项表示目标页面。
 */
typedef struct
{
    const char *label;
    void *target;
    uint8 meta;
    uint8 action;
} MenuItemDef;

/**
 * @brief 菜单页面描述。
 */
typedef struct
{
    const char *title;
    const MenuItemDef *items;
    uint8 item_count;
    uint8 parent;
} MenuPageDef;

static const float menu_float_steps[] = {0.001f, 0.01f, 0.1f, 1.0f, 5.0f, 10.0f};

static const MenuItemDef menu_home_items[] = {
    {"START", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_START},
    {"CTRL", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_SPEED},
    {"MODEL", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_MODEL},
    {"DIFF", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_DIFF},
    {"YUANSHU", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_YUANSHU},
    {"SENSOR", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_SENSOR}};

static const MenuItemDef menu_start_items[] = {
    {"Start_Flag", &app.start.start_flag, MENU_META(MENU_ITEM_BOOL, 3, 0), 0},
    {"elem_en", &app.start.element_enable, MENU_META(MENU_ITEM_BOOL, 3, 0), 0},
    {"fuya_ground", &app.start.fuya_xili,
     MENU_META(MENU_ITEM_FLOAT, 4, 1), MENU_FLOAT_STEP_1},
    {"gyro_fbN", &app.angle.gyro_feedback_scale,
     MENU_META(MENU_ITEM_FLOAT, 4, 2), MENU_FLOAT_STEP_001},
    {"ELEM", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_ELEMENT_LEN},
    {"stop_cm", &app.start.encoder_stop_distance_cm,
     MENU_META(MENU_ITEM_FLOAT, 4, 0), MENU_FLOAT_STEP_10}};

static const MenuItemDef menu_speed_items[] = {
    {"kp_Err", &app.speed.kp_Err, MENU_META(MENU_ITEM_FLOAT, 3, 3), MENU_FLOAT_STEP_001},
    {"kd_Err", &app.speed.kd_Err, MENU_META(MENU_ITEM_FLOAT, 3, 3), MENU_FLOAT_STEP_001},
    {"gyro_dmp", &app.speed.gyro_damp_Err, MENU_META(MENU_ITEM_FLOAT, 3, 3), MENU_FLOAT_STEP_0001},
    {"speed_run", &app.speed.speed_run, MENU_META(MENU_ITEM_FLOAT, 4, 1), MENU_FLOAT_STEP_1},
    {"limit_Err", &app.speed.limiting_Err, MENU_META(MENU_ITEM_FLOAT, 3, 3), MENU_FLOAT_STEP_1},
    {"kp2_Err", &app.speed.kp2_Err, MENU_META(MENU_ITEM_FLOAT, 3, 3), MENU_FLOAT_STEP_001}};

static const MenuItemDef menu_model_items[] = {
    {"kp_Ang", &app.angle.kp_Angle, MENU_META(MENU_ITEM_FLOAT, 3, 3), MENU_FLOAT_STEP_001},
    {"kd_Ang", &app.angle.kd_Angle, MENU_META(MENU_ITEM_FLOAT, 3, 3), MENU_FLOAT_STEP_001},
    {"lim_Ang", &app.angle.limiting_Angle, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_1},
    {"A_1", &app.angle.A_1, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_001},
    {"B_1", &app.angle.B_1, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_001},
    {"C_l", &app.angle.C_l, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_001}};

static const MenuItemDef menu_diff_items[] = {
    {"diff_en", &app.speed.diff_enable, MENU_META(MENU_ITEM_BOOL, 1, 0), 0},
    {"inner_g", &app.speed.diff_inner_gain,
     MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_01},
    {"outer_g", &app.speed.diff_outer_gain,
     MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_01}};

static const MenuItemDef menu_yuanshu_items[] = {
    {"RING", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_RING},
    {"CYLINDER", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_CYLINDER},
    {"WALL", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_WALL},
    {"FLY", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_FLY},
    {"CROSS", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_CROSS}};

static const MenuItemDef menu_ring_items[] = {
    {"entry_E", &app.ring.ring_entry_encoder,
     MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_1},
    {"pre_r_T", &app.ring.pre_ring_Gyro_target,
     MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_1},
    {"pre_r_Gz", &app.ring.pre_ring_Gyroz, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_1},
    {"in_r_Gz", &app.ring.in_ring_Gyroz, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_1},
    {"pre_o_T", &app.ring.pre_out_ring_Gyro_target,
     MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_1},
    {"pre_o_Gz", &app.ring.pre_out_ring_Gyroz, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_1},
    {"drv_o_E", &app.ring.drive_out_ring_encoder,
     MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_1}};

static const MenuItemDef menu_cylinder_items[] = {
    {"cyl_enc", &app.cylinder.encoder_target,
     MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_10},
    {"cyl_both", &app.cylinder.ad_both_high_threshold,
     MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_1},
    {"adc_a_1", &app.cylinder.adc_a_1, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_01},
    {"adc_b_1", &app.cylinder.adc_b_1, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_01},
    {"adc_c_l", &app.cylinder.adc_c_l, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_01},
    {"exit_spd", &app.cylinder.exit_slow_speed,
     MENU_META(MENU_ITEM_INT16, 4, 0), MENU_INT_STEP_1}};

static const MenuItemDef menu_wall_items[] = {
    {"wall_spd", &app.wall.slow_speed,
     MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_1},
    {"wall_slow_t", &app.wall.slow_time,
     MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_10},
    {"wall_timing", &app.wall.timing_count,
     MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_10},
    {"wall_enc", &app.wall.encoder_target, MENU_META(MENU_ITEM_FLOAT, 4, 1), MENU_FLOAT_STEP_1}};

static const MenuItemDef menu_fly_items[] = {
    {"seesaw_mode", &app.fly.seesaw_mode, MENU_META(MENU_ITEM_BOOL, 1, 0), 0},
    {"fly_speed", &app.fly.fly_speed,
     MENU_META(MENU_ITEM_INT16, 4, 0), MENU_INT_STEP_1},
    {"detect_cnt", &app.fly.fly_detect_count,
     MENU_META(MENU_ITEM_INT16, 4, 0), MENU_INT_STEP_1},
    {"recover_spd", &app.fly.fly_recover_speed,
     MENU_META(MENU_ITEM_INT16, 4, 0), MENU_INT_STEP_1},
    {"release_stp", &app.fly.fly_release_step,
     MENU_META(MENU_ITEM_FLOAT, 4, 2), MENU_FLOAT_STEP_01},
    {"land_cnt", &app.fly.fly_land_confirm_count,
     MENU_META(MENU_ITEM_INT16, 4, 0), MENU_INT_STEP_1}};

static const MenuItemDef menu_seesaw_items[] = {
    {"seesaw_mode", &app.fly.seesaw_mode, MENU_META(MENU_ITEM_BOOL, 1, 0), 0},
    {"seesaw_spd", &app.fly.seesaw_speed,
     MENU_META(MENU_ITEM_INT16, 4, 0), MENU_INT_STEP_1},
    {"detect_cnt", &app.fly.seesaw_detect_count,
     MENU_META(MENU_ITEM_INT16, 4, 0), MENU_INT_STEP_1},
    {"wait_cnt", &app.fly.seesaw_wait_count,
     MENU_META(MENU_ITEM_INT16, 4, 0), MENU_INT_STEP_1},
    {"creep_cm", &app.fly.seesaw_creep_cm, MENU_META(MENU_ITEM_FLOAT, 4, 2), MENU_FLOAT_STEP_01},
    {"release_stp", &app.fly.seesaw_release_step,
     MENU_META(MENU_ITEM_FLOAT, 4, 2), MENU_FLOAT_STEP_01}};

static const MenuItemDef menu_cross_items[] = {
    {"enc_target", &app.cross.encoder_target, MENU_META(MENU_ITEM_FLOAT, 4, 1), MENU_FLOAT_STEP_1},
    {"adc_a_1", &app.cross.adc_a_1, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_01},
    {"adc_b_1", &app.cross.adc_b_1, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_01},
    {"adc_c_l", &app.cross.adc_c_l, MENU_META(MENU_ITEM_FLOAT, 3, 2), MENU_FLOAT_STEP_01}};

static const MenuItemDef menu_element_len_items[] = {
    {"LEN", &app.start.element_len, MENU_META(MENU_ITEM_LENGTH, 3, 0), MENU_PAGE_ELEMENT}};

static const MenuItemDef menu_element_items[] = {
    {"E1", &app.start.element_seq[0], MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_1},
    {"E2", &app.start.element_seq[1], MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_1},
    {"E3", &app.start.element_seq[2], MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_1},
    {"E4", &app.start.element_seq[3], MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_1},
    {"E5", &app.start.element_seq[4], MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_1},
    {"E6", &app.start.element_seq[5], MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_1}};

#define MENU_ITEM_COUNT(items) ((uint8)(sizeof(items) / sizeof((items)[0])))

static const MenuPageDef menu_pages[] = {
    {"MENU", menu_home_items, MENU_ITEM_COUNT(menu_home_items), MENU_PAGE_NONE},
    {"<<START", menu_start_items, MENU_ITEM_COUNT(menu_start_items), MENU_PAGE_HOME},
    {"<<CTRL", menu_speed_items, MENU_ITEM_COUNT(menu_speed_items), MENU_PAGE_HOME},
    {"<<MODEL", menu_model_items, MENU_ITEM_COUNT(menu_model_items), MENU_PAGE_HOME},
    {"<<DIFF", menu_diff_items, MENU_ITEM_COUNT(menu_diff_items), MENU_PAGE_HOME},
    {"<<YUANSHU", menu_yuanshu_items, MENU_ITEM_COUNT(menu_yuanshu_items), MENU_PAGE_HOME},
    {"<<SENSOR", 0, 0, MENU_PAGE_HOME},
    {"<<RING", menu_ring_items, MENU_ITEM_COUNT(menu_ring_items), MENU_PAGE_YUANSHU},
    {"<<CYLINDER", menu_cylinder_items,
     MENU_ITEM_COUNT(menu_cylinder_items), MENU_PAGE_YUANSHU},
    {"<<WALL", menu_wall_items, MENU_ITEM_COUNT(menu_wall_items), MENU_PAGE_YUANSHU},
    {"<<SEESAW", menu_fly_items, MENU_ITEM_COUNT(menu_fly_items), MENU_PAGE_YUANSHU},
    {"<<CROSS", menu_cross_items, MENU_ITEM_COUNT(menu_cross_items), MENU_PAGE_YUANSHU},
    {"<<ELEM", menu_element_len_items, MENU_ITEM_COUNT(menu_element_len_items), MENU_PAGE_START},
    {"<<ELEM", menu_element_items,
     MENU_ITEM_COUNT(menu_element_items), MENU_PAGE_ELEMENT_LEN}};

static uint8 menu_service_enabled = 0;
static uint8 menu_page = MENU_PAGE_HOME;
static uint8 menu_selected[MENU_PAGE_COUNT];
static uint8 menu_editing = 0;
static uint8 menu_change_multiplier = 1;
static int8 menu_previous_row = -1;

static void Menu_Clear_Pending_Key_Events(void);
static void Menu_Clear_Page(void);
static uint8 Menu_Read_Key_Event(void);
static const MenuItemDef *Menu_Get_Items(void);
static uint8 Menu_Get_Item_Type(const MenuItemDef *item);
static void Menu_Show_Int32(uint16 x, uint16 y, int16 dat, uint8 num);
static void Menu_Show_Float(uint16 x, uint16 y, float dat, uint8 num, uint8 pointnum);
static void Menu_Draw_Item_Value(const MenuItemDef *item, uint16 y);
static void Menu_Draw_Sensor(void);
static void Menu_Draw_Page_Extras(void);
static void Menu_Draw_Edit_Step(const MenuItemDef *item);
static void Menu_Render_Page(void);
static void Menu_Change_Page(uint8 page);
static void Menu_Apply_Item_Change(const MenuItemDef *item, uint8 increase);
static void Menu_Handle_Edit(uint8 event_code);
static void Menu_Handle_Navigation(uint8 event_code);
static void Menu_Show_Save_Prompt(void);

/**
 * @brief 设置菜单服务使能状态并复位菜单会话。
 * @param enabled 非零表示使能。
 * @return 无。
 */
void Menu_Set_Service_Enable(uint8 enabled)
{
    uint8 i;

    menu_service_enabled = enabled ? 1u : 0u;
    menu_page = MENU_PAGE_HOME;
    menu_editing = 0;
    menu_change_multiplier = 1;
    menu_previous_row = -1;
    for (i = 0; i < MENU_PAGE_COUNT; i++)
        menu_selected[i] = 0;
    Menu_Clear_Pending_Key_Events();
}

/**
 * @brief 查询菜单服务是否使能。
 * @return uint8 1-使能，0-关闭。
 */
uint8 Menu_Is_Service_Enabled(void)
{
    return menu_service_enabled;
}

/**
 * @brief 保留给 10ms 中断链路的兼容入口。
 * @return 无。
 */
void Menu_Tick_10ms(void)
{
}

/**
 * @brief 清空尚未消费的按键事件。
 * @return 无。
 */
static void Menu_Clear_Pending_Key_Events(void)
{
    while (Keystroke_Get_Event() != 0)
    {
    }
}

/**
 * @brief 清空屏幕并重置光标擦除状态。
 * @return 无。
 */
static void Menu_Clear_Page(void)
{
    ips114_clear(RGB565_WHITE);
    menu_previous_row = -1;
}

/**
 * @brief 读取一次按键事件。
 * @return uint8 按键事件编码，0 表示无事件。
 */
static uint8 Menu_Read_Key_Event(void)
{
    return Keystroke_Get_Event();
}

/**
 * @brief 获取当前页面使用的行描述。
 * @return const MenuItemDef * 当前行描述首地址。
 *
 * FLY 页面根据 seesaw_mode 动态选择飞坡或停止等待参数集。
 */
static const MenuItemDef *Menu_Get_Items(void)
{
    if (menu_page == MENU_PAGE_FLY && app.fly.seesaw_mode != 0)
        return menu_seesaw_items;

    return menu_pages[menu_page].items;
}

/**
 * @brief 解码菜单行类型。
 * @param item 菜单行描述。
 * @return uint8 MenuItemType。
 */
static uint8 Menu_Get_Item_Type(const MenuItemDef *item)
{
    return (uint8)((item->meta & MENU_TYPE_MASK) >> MENU_TYPE_SHIFT);
}

/**
 * @brief 菜单专用 16 位整数显示。
 * @param x 显示起始横坐标。
 * @param y 显示起始纵坐标。
 * @param dat 待显示整数，菜单约束为绝对值不超过 9999。
 * @param num 整数显示宽度，最大 4 位。
 * @return 无。
 */
static void Menu_Show_Int32(uint16 x, uint16 y, int16 dat, uint8 num)
{
    char buff[6];
    uint8 pos;
    uint8 length;
    uint16 value;
    uint16 div;

    pos = 0;
    length = num + 1;
    if (dat < 0)
    {
        buff[pos++] = '-';
        value = (uint16)(-dat);
    }
    else
    {
        buff[pos++] = ' ';
        value = (uint16)dat;
    }

    div = 1;
    while (div < 1000u && div <= value / 10)
        div *= 10;
    while (div > 0)
    {
        buff[pos++] = (char)('0' + (value / div));
        value %= div;
        div /= 10;
    }

    while (pos < length)
        buff[pos++] = ' ';
    buff[length] = '\0';
    ips114_show_string(x, y, buff);
}

/**
 * @brief 菜单专用固定小数位浮点显示。
 * @param x 显示起始横坐标。
 * @param y 显示起始纵坐标。
 * @param dat 待显示浮点数。
 * @param num 整数显示宽度，最大 4 位。
 * @param pointnum 小数位数，最大 3 位。
 * @return 无。
 */
static void Menu_Show_Float(uint16 x, uint16 y, float dat, uint8 num, uint8 pointnum)
{
    char buff[13];
    uint8 pos;
    uint8 i;
    uint8 length;
    uint16 div;
    uint16 scale;
    uint16 integer_part;
    uint16 fraction_part;
    float value;

    scale = (pointnum == 3)
                ? 1000u
                : ((pointnum == 2) ? 100u : ((pointnum == 1) ? 10u : 1u));
    pos = 0;
    if (dat < 0.0f)
    {
        buff[pos++] = '-';
        value = -dat;
    }
    else
    {
        buff[pos++] = ' ';
        value = dat;
    }

    integer_part = (uint16)value;
    fraction_part = (uint16)((value - (float)integer_part) * (float)scale + 0.5f);
    if (fraction_part >= scale)
    {
        integer_part++;
        fraction_part = 0;
    }

    div = 1;
    while (div < 1000u && div <= integer_part / 10)
        div *= 10;
    while (div > 0)
    {
        buff[pos++] = (char)('0' + (integer_part / div));
        integer_part %= div;
        div /= 10;
    }

    if (pointnum != 0)
    {
        buff[pos++] = '.';
        div = scale / 10;
        for (i = 0; i < pointnum; i++)
        {
            buff[pos++] = (char)('0' + (fraction_part / div));
            fraction_part %= div;
            div /= 10;
        }
    }

    length = num + pointnum + 1;
    if (pointnum != 0)
        length++;
    while (pos < length)
        buff[pos++] = ' ';
    buff[pos] = '\0';
    ips114_show_string(x, y, buff);
}

/**
 * @brief 绘制一个菜单行的当前值。
 * @param item 菜单行描述。
 * @param y 显示纵坐标。
 * @return 无。
 */
static void Menu_Draw_Item_Value(const MenuItemDef *item, uint16 y)
{
    uint8 type;
    uint8 width;
    uint8 decimal;

    type = Menu_Get_Item_Type(item);
    if (type == MENU_ITEM_LINK)
        return;

    width = (uint8)((item->meta & MENU_WIDTH_MASK) >> MENU_WIDTH_SHIFT);
    decimal = item->meta & MENU_DECIMAL_MASK;
    if (type == MENU_ITEM_FLOAT)
        Menu_Show_Float(112, y, *((float *)item->target), width, decimal);
    else
        Menu_Show_Int32(112, y, *((int16 *)item->target), width);
}

/**
 * @brief 绘制传感器只读页面。
 * @return 无。
 */
static void Menu_Draw_Sensor(void)
{
    char label[4];
    uint8 i;

    ips114_show_string(8, 0, "<<SENSOR");
    ips114_show_string(64, 0, "NORM");
    ips114_show_string(112, 0, "RAW");

    Menu_Show_Int32(64, 1 * MENU_ROW_HEIGHT, ad1, 3);
    Menu_Show_Int32(64, 2 * MENU_ROW_HEIGHT, ad2, 3);
    Menu_Show_Int32(64, 3 * MENU_ROW_HEIGHT, ad3, 3);
    Menu_Show_Int32(64, 4 * MENU_ROW_HEIGHT, ad4, 3);
    Menu_Show_Int32(64, 5 * MENU_ROW_HEIGHT, ad5, 3);

    label[0] = 'a';
    label[1] = 'd';
    label[3] = '\0';
    for (i = 0; i < NUM; i++)
    {
        label[2] = (char)('1' + i);
        ips114_show_string(16, (i + 1) * MENU_ROW_HEIGHT, label);
        Menu_Show_Int32(112, (i + 1) * MENU_ROW_HEIGHT, RAW[i], 4);
    }

    ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "Err");
    Menu_Show_Float(48, 6 * MENU_ROW_HEIGHT, Err, 4, 1);
    ips114_show_string(112, 6 * MENU_ROW_HEIGHT, "cyl");
    Menu_Show_Int32(144, 6 * MENU_ROW_HEIGHT, a_run_cylinder_get_state(), 1);
}

/**
 * @brief 绘制页面右侧实时状态。
 * @return 无。
 */
static void Menu_Draw_Page_Extras(void)
{
    switch (menu_page)
    {
    case MENU_PAGE_HOME:
        ips114_show_string(105, 1 * MENU_ROW_HEIGHT, "Err");
        ips114_show_string(105, 2 * MENU_ROW_HEIGHT, "steer");
        ips114_show_string(105, 3 * MENU_ROW_HEIGHT, "gyro_z");
        ips114_show_string(105, 4 * MENU_ROW_HEIGHT, "V_bat");
        ips114_show_string(105, 5 * MENU_ROW_HEIGHT, "L_pwm");
        ips114_show_string(105, 6 * MENU_ROW_HEIGHT, "R_pwm");
        Menu_Show_Float(184, 1 * MENU_ROW_HEIGHT, Err, 3, 2);
        Menu_Show_Float(184, 2 * MENU_ROW_HEIGHT, PID.steer.output, 3, 1);
        Menu_Show_Float(184, 3 * MENU_ROW_HEIGHT, gyro_z, 3, 2);
        Menu_Show_Float(184, 4 * MENU_ROW_HEIGHT, dianya, 4, 2);
        Menu_Show_Float(184, 5 * MENU_ROW_HEIGHT, left_target, 4, 1);
        Menu_Show_Float(184, 6 * MENU_ROW_HEIGHT, right_target, 4, 1);
        break;
    case MENU_PAGE_YUANSHU:
        ips114_show_string(105, 1 * MENU_ROW_HEIGHT, "S");
        Menu_Show_Int32(120, 1 * MENU_ROW_HEIGHT, a_run_ring_get_state(), 1);
        ips114_show_string(105, 2 * MENU_ROW_HEIGHT, "C");
        Menu_Show_Int32(120, 2 * MENU_ROW_HEIGHT, a_run_cylinder_get_state(), 1);
        ips114_show_string(105, 3 * MENU_ROW_HEIGHT, "W");
        Menu_Show_Int32(120, 3 * MENU_ROW_HEIGHT, a_run_wall_get_state(), 1);
        ips114_show_string(105, 4 * MENU_ROW_HEIGHT, "X");
        Menu_Show_Int32(120, 4 * MENU_ROW_HEIGHT, a_run_track_element_get_expected_element(), 1);
        ips114_show_string(105, 5 * MENU_ROW_HEIGHT, "R");
        Menu_Show_Int32(120, 5 * MENU_ROW_HEIGHT, a_run_cross_get_state(), 1);
        break;
    case MENU_PAGE_RING:
        ips114_show_string(168, 1 * MENU_ROW_HEIGHT, "S");
        Menu_Show_Int32(184, 1 * MENU_ROW_HEIGHT, a_run_ring_get_state(), 1);
        ips114_show_string(168, 2 * MENU_ROW_HEIGHT, "Yd");
        Menu_Show_Float(184, 2 * MENU_ROW_HEIGHT, ring_data.yaw_delta_sum, 4, 0);
        ips114_show_string(168, 3 * MENU_ROW_HEIGHT, "E");
        Menu_Show_Float(184, 3 * MENU_ROW_HEIGHT, ring_data.encoder, 4, 0);
        break;
    case MENU_PAGE_CYLINDER:
        ips114_show_string(168, 1 * MENU_ROW_HEIGHT, "C");
        Menu_Show_Int32(184, 1 * MENU_ROW_HEIGHT, a_run_cylinder_get_state(), 1);
        break;
    case MENU_PAGE_WALL:
        ips114_show_string(168, 1 * MENU_ROW_HEIGHT, "W");
        Menu_Show_Int32(184, 1 * MENU_ROW_HEIGHT, a_run_wall_get_state(), 1);
        break;
    default:
        break;
    }
}

/**
 * @brief 绘制当前编辑步长。
 * @param item 当前编辑行。
 * @return 无。
 */
static void Menu_Draw_Edit_Step(const MenuItemDef *item)
{
    uint8 type;
    float float_unit;
    int16 int_unit;

    type = Menu_Get_Item_Type(item);
    if (type == MENU_ITEM_BOOL)
    {
        ips114_show_string(MENU_STEP_X, 0, "0/1");
    }
    else if (type == MENU_ITEM_FLOAT)
    {
        float_unit = menu_float_steps[item->action] * (float)menu_change_multiplier;
        Menu_Show_Float(MENU_STEP_X, 0, float_unit, 4, 3);
    }
    else if (type == MENU_ITEM_INT16)
    {
        int_unit = (int16)item->action * (int16)menu_change_multiplier;
        Menu_Show_Int32(MENU_STEP_INT_X, 0, int_unit, 4);
    }
}

/**
 * @brief 绘制当前页面、实时状态和光标。
 * @return 无。
 */
static void Menu_Render_Page(void)
{
    const MenuPageDef *page;
    const MenuItemDef *items;
    const MenuItemDef *item;
    uint8 i;
    uint8 row;
    uint16 y;

    page = &menu_pages[menu_page];
    if (menu_page == MENU_PAGE_SENSOR)
    {
        Menu_Draw_Sensor();
        return;
    }

    items = Menu_Get_Items();
    if (menu_page == MENU_PAGE_HOME)
        ips114_show_string(MENU_CENTER_X, 0, page->title);
    else
        ips114_show_string(8, 0, page->title);

    for (i = 0; i < page->item_count; i++)
    {
        item = &items[i];
        y = (uint16)(i + 1) * MENU_ROW_HEIGHT;
        ips114_show_string(16, y, item->label);
        Menu_Draw_Item_Value(item, y);
    }

    Menu_Draw_Page_Extras();
    row = menu_selected[menu_page];
    if (menu_previous_row >= 0 && menu_previous_row != (int8)row)
        ips114_show_string(0, (uint16)(menu_previous_row + 1) * MENU_ROW_HEIGHT, "  ");
    ips114_show_string(0,
                       (uint16)(row + 1) * MENU_ROW_HEIGHT,
                       menu_editing ? ">>" : "> ");
    menu_previous_row = (int8)row;

    if (menu_editing)
        Menu_Draw_Edit_Step(&items[row]);
}

/**
 * @brief 切换页面并恢复该页记忆的光标。
 * @param page 目标 MenuPageId。
 * @return 无。
 */
static void Menu_Change_Page(uint8 page)
{
    menu_page = page;
    menu_editing = (page == MENU_PAGE_ELEMENT_LEN) ? 1u : 0u;
    menu_previous_row = -1;
    Menu_Clear_Page();
    Menu_Render_Page();
    Menu_Clear_Pending_Key_Events();
}

/**
 * @brief 按当前倍率修改一个参数。
 * @param item 当前编辑行。
 * @param increase 1-增加，0-减少。
 * @return 无。
 */
static void Menu_Apply_Item_Change(const MenuItemDef *item, uint8 increase)
{
    uint8 type;
    int16 *int_value;
    float *float_value;
    int16 int_unit;
    float float_unit;

    type = Menu_Get_Item_Type(item);
    if (type == MENU_ITEM_FLOAT)
    {
        float_value = (float *)item->target;
        float_unit = menu_float_steps[item->action] * (float)menu_change_multiplier;
        if (increase)
            *float_value += float_unit;
        else
            *float_value -= float_unit;
    }
    else
    {
        int_value = (int16 *)item->target;
        if (type == MENU_ITEM_BOOL)
        {
            *int_value = increase ? 1 : 0;
        }
        else if (type == MENU_ITEM_LENGTH)
        {
            if (increase)
                *int_value = (*int_value >= TRACK_ELEMENT_SEQUENCE_MAX)
                                 ? 1
                                 : (int16)(*int_value + 1);
            else
                *int_value = (*int_value <= 1)
                                 ? TRACK_ELEMENT_SEQUENCE_MAX
                                 : (int16)(*int_value - 1);
        }
        else
        {
            int_unit = (int16)item->action * (int16)menu_change_multiplier;
            if (increase)
                *int_value += int_unit;
            else
                *int_value -= int_unit;
        }
    }

    control_apply_config();
}

/**
 * @brief 处理参数编辑状态下的按键。
 * @param event_code 按键事件编码。
 * @return 无。
 */
static void Menu_Handle_Edit(uint8 event_code)
{
    const MenuItemDef *items;
    const MenuItemDef *item;
    uint8 type;

    items = Menu_Get_Items();
    item = &items[menu_selected[menu_page]];
    type = Menu_Get_Item_Type(item);

    switch (event_code)
    {
    case KEYSTROKE_ONE:
    case KEYSTROKE_ONE_LONG:
        Menu_Apply_Item_Change(item, 1);
        if (menu_page == MENU_PAGE_FLY && menu_selected[menu_page] == 0)
            Menu_Clear_Page();
        break;
    case KEYSTROKE_TWO:
    case KEYSTROKE_TWO_LONG:
        Menu_Apply_Item_Change(item, 0);
        if (menu_page == MENU_PAGE_FLY && menu_selected[menu_page] == 0)
            Menu_Clear_Page();
        break;
    case KEYSTROKE_THREE:
        if (type == MENU_ITEM_LENGTH)
        {
            Menu_Change_Page(item->action);
            return;
        }
        menu_change_multiplier = (menu_change_multiplier == 1) ? 10u : ((menu_change_multiplier == 10) ? 100u : 1u);
        break;
    case KEYSTROKE_FOUR:
    case KEYSTROKE_FOUR_LONG:
        if (type == MENU_ITEM_LENGTH)
        {
            Menu_Change_Page(menu_pages[menu_page].parent);
            return;
        }
        menu_editing = 0;
        Menu_Clear_Page();
        break;
    default:
        return;
    }

    Menu_Render_Page();
}

/**
 * @brief 处理页面导航状态下的按键。
 * @param event_code 按键事件编码。
 * @return 无。
 */
static void Menu_Handle_Navigation(uint8 event_code)
{
    const MenuPageDef *page;
    const MenuItemDef *items;
    const MenuItemDef *item;
    uint8 row;

    page = &menu_pages[menu_page];
    row = menu_selected[menu_page];
    if (page->item_count == 0 &&
        event_code != KEYSTROKE_FOUR &&
        event_code != KEYSTROKE_FOUR_LONG)
    {
        return;
    }

    switch (event_code)
    {
    case KEYSTROKE_ONE:
    case KEYSTROKE_ONE_LONG:
        menu_selected[menu_page] = (row == 0) ? (uint8)(page->item_count - 1) : (uint8)(row - 1);
        break;
    case KEYSTROKE_TWO:
    case KEYSTROKE_TWO_LONG:
        menu_selected[menu_page] = (row + 1 >= page->item_count) ? 0 : (uint8)(row + 1);
        break;
    case KEYSTROKE_THREE:
        if (page->item_count == 0)
            return;
        items = Menu_Get_Items();
        item = &items[row];
        if (Menu_Get_Item_Type(item) == MENU_ITEM_LINK)
        {
            Menu_Change_Page(item->action);
            return;
        }
        menu_editing = 1;
        Menu_Clear_Page();
        break;
    case KEYSTROKE_FOUR:
    case KEYSTROKE_FOUR_LONG:
        if (menu_page == MENU_PAGE_HOME)
        {
            config_save();
            Menu_Show_Save_Prompt();
            Menu_Render_Page();
            return;
        }
        Menu_Change_Page(page->parent);
        return;
    default:
        return;
    }

    Menu_Render_Page();
}

/**
 * @brief 显示保存完成提示。
 * @return 无。
 */
static void Menu_Show_Save_Prompt(void)
{
    Menu_Clear_Page();
    ips114_show_string(MENU_CENTER_X - 16, 3 * MENU_ROW_HEIGHT, "save");
    Menu_Clear_Pending_Key_Events();
    system_delay_ms(MENU_SAVE_PROMPT_DELAY_MS);
    Menu_Clear_Page();
    Menu_Clear_Pending_Key_Events();
}

/**
 * @brief 菜单主处理函数。
 * @return 无。
 */
void Keystroke_Menu(void)
{
    uint8 event_code;

    if (!menu_service_enabled)
        return;

    Menu_Render_Page();
    event_code = Menu_Read_Key_Event();
    if (event_code == 0)
        return;

    if (menu_editing)
        Menu_Handle_Edit(event_code);
    else
        Menu_Handle_Navigation(event_code);
}

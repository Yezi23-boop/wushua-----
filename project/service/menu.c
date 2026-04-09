#include "zf_common_headfile.h"
#include "menu.h"
#include "key.h"

#define KEYSTROKE_ONE 1
#define KEYSTROKE_TWO 2
#define KEYSTROKE_THREE 3
#define KEYSTROKE_FOUR 4
#define KEYSTROKE_ONE_LONG 5
#define KEYSTROKE_TWO_LONG 6
#define KEYSTROKE_FOUR_LONG 8

#define MENU_HOME_REFRESH_MS 50
#define MENU_SENSOR_REFRESH_MS 20
#define MENU_PROMPT_DURATION_MS 300
#define MENU_STEP_AREA_X (14 * 8)
#define MENU_STEP_AREA_Y 0
#define MENU_CURSOR_X 0
#define EEPROM_MODE 1

static void Menu_Draw_Home_Static(void);
static void Menu_Draw_Home_Cursor(void);
static void Menu_Draw_Home_Dynamic(void);
static void Menu_Draw_Sensor_Static(void);
static void Menu_Draw_Sensor_Dynamic(void);

static const char menu_blank_text[] = "                                ";
static const menu_item_t menu_home_items[] = {
    {"STRAT", MENU_ITEM_SUBMENU, 0, 0.0f, 1, 0, 0},
    {"PID_1", MENU_ITEM_SUBMENU, 0, 0.0f, 2, 0, 0},
    {"PID_2", MENU_ITEM_SUBMENU, 0, 0.0f, 3, 0, 0},
    {"PRINTF", MENU_ITEM_SUBMENU, 0, 0.0f, 4, 0, 0},
    {"RING", MENU_ITEM_SUBMENU, 0, 0.0f, 5, 0, 0},
    {"FLY", MENU_ITEM_SUBMENU, 0, 0.0f, 6, 0, 0}};

static const menu_layout_t menu_home_layout = {96, 0, 18, 18, 16, 112, 6};

static const menu_item_t menu_start_items[] = {
    {"Start_Flag", MENU_ITEM_SPECIAL, &app.start.start_flag, 0.0f, 11, 3, 0},
    {"circle_flags", MENU_ITEM_SPECIAL, &app.start.circle_flags, 0.0f, 12, 3, 0},
    {"fuya_xili", MENU_ITEM_FLOAT, &app.start.fuya_xili, 100.0f, 13, 4, 3}};

static const menu_item_t menu_speed_items[] = {
    {"kp_Err", MENU_ITEM_FLOAT, &app.speed.kp_Err, 0.01f, 21, 3, 3},
    {"kd_Err", MENU_ITEM_FLOAT, &app.speed.kd_Err, 0.01f, 22, 3, 3},
    {"speed_run", MENU_ITEM_FLOAT, &app.speed.speed_run, 1.0f, 23, 3, 3},
    {"limit_Err", MENU_ITEM_FLOAT, &app.speed.limiting_Err, 0.01f, 24, 3, 3},
    {"kp2_Err", MENU_ITEM_FLOAT, &app.speed.kp2_Err, 0.001f, 25, 3, 3}};

static const menu_item_t menu_angle_items[] = {
    {"kp_Angle", MENU_ITEM_FLOAT, &app.angle.kp_Angle, 0.01f, 31, 3, 2},
    {"kd_Angle", MENU_ITEM_FLOAT, &app.angle.kd_Angle, 0.01f, 32, 3, 2},
    {"limit_Angle", MENU_ITEM_FLOAT, &app.angle.limiting_Angle, 10.0f, 33, 3, 2},
    {"A_1", MENU_ITEM_FLOAT, &app.angle.A_1, 0.01f, 34, 3, 2},
    {"B_1", MENU_ITEM_FLOAT, &app.angle.B_1, 0.01f, 35, 3, 2},
    {"C_l", MENU_ITEM_FLOAT, &app.angle.C_l, 0.01f, 36, 3, 2}};

static const menu_item_t menu_ring_items[] = {
    {"ring_en", MENU_ITEM_FLOAT, &app.ring.ring_encoder, 1.0f, 51, 3, 2},
    {"pre_r_G", MENU_ITEM_FLOAT, &app.ring.pre_ring_Gyro_set, 10.0f, 52, 3, 2},
    {"in_r_G", MENU_ITEM_FLOAT, &app.ring.in_ring_Gyroz, 10.0f, 53, 3, 2},
    {"pre_o_G", MENU_ITEM_FLOAT, &app.ring.pre_out_ring_Gyro_set, 10.0f, 54, 3, 2},
    {"pre_o_Gz", MENU_ITEM_FLOAT, &app.ring.pre_out_ring_Gyroz, 10.0f, 55, 3, 2},
    {"pre_o_en", MENU_ITEM_FLOAT, &app.ring.pre_out_ring_encoder, 1.0f, 56, 3, 2}};

static const menu_item_t menu_fly_items[] = {
    {"fly_speed", MENU_ITEM_INT, &app.fly.count_fly_speed, 1.0f, 61, 4, 0},
    {"fly_time_1", MENU_ITEM_INT, &app.fly.count_fly_time_1, 1.0f, 62, 4, 0},
    {"fly_time_2", MENU_ITEM_INT, &app.fly.count_fly_time_2, 1.0f, 63, 4, 0},
    {"fly_angle", MENU_ITEM_INT, &app.fly.count_fly_angle, 1.0f, 64, 4, 0},
    {"fly_en", MENU_ITEM_INT16, &app.fly.fly_ramp_enable, 1.0f, 65, 4, 0}};

static const menu_page_t menu_pages[] = {
    {0, 0, "MENU", MENU_PAGE_HOME, MENU_HOME_REFRESH_MS, {96, 0, 18, 18, 16, 112, 6}, 0, 0, Menu_Draw_Home_Static, Menu_Draw_Home_Dynamic},
    {1, 0, "<<STRAT", MENU_PAGE_LIST, 0, {8, 0, 18, 18, 8, 112, 6}, menu_start_items, 3, 0, 0},
    {2, 0, "<<PID_SPEED", MENU_PAGE_LIST, 0, {8, 0, 18, 18, 8, 112, 6}, menu_speed_items, 5, 0, 0},
    {3, 0, "<<PID_ANGLE", MENU_PAGE_LIST, 0, {8, 0, 18, 18, 8, 112, 6}, menu_angle_items, 6, 0, 0},
    {4, 0, "<<SENSOR", MENU_PAGE_SENSOR, MENU_SENSOR_REFRESH_MS, {8, 0, 18, 18, 8, 112, 6}, 0, 0, Menu_Draw_Sensor_Static, Menu_Draw_Sensor_Dynamic},
    {5, 0, "<<RING_CTRL", MENU_PAGE_LIST, 0, {8, 0, 18, 18, 8, 112, 6}, menu_ring_items, 6, 0, 0},
    {6, 0, "<<FLY_CTRL", MENU_PAGE_LIST, 0, {8, 0, 18, 18, 8, 112, 6}, menu_fly_items, 5, 0, 0}};

int display_codename = 0;

static uint8 menu_cursor_index = 0;
static uint8 menu_previous_cursor_index = 0;
static uint8 menu_scroll_offset = 0;
static uint8 menu_previous_scroll_offset = 0;
static int change_unit_multiplier = 1;
static uint8 keystroke_three_count = 0;

static uint8 page_changed = 1;
static uint8 list_dirty = 0;
static uint8 cursor_dirty = 0;
static uint8 value_dirty = 0;
static uint8 step_dirty = 0;
static uint8 realtime_due = 0;
static uint8 prompt_active = 0;
static uint8 prompt_dirty = 0;
static uint16 prompt_remaining_ms = 0;
static uint16 page_elapsed_ms = 0;
static volatile uint16 menu_tick_10ms_pending = 0;
static uint8 menu_service_enabled = 0;

static const menu_page_t *Menu_Find_Page_By_Id(int page_id);
static const menu_page_t *Menu_Find_List_Page_By_Item(int item_page_id, uint8 *item_index);
static const menu_page_t *Menu_Get_Current_Page(uint8 *is_edit_mode, uint8 *item_index);
static uint8 Menu_Get_Home_Item_Count(void);
static void Menu_Clear_Field(uint8 x, uint8 y);
static void Menu_Clear_Cursor_Cell(uint8 y);
static void Menu_Clear_List_Row(const menu_page_t *page, uint8 visible_index);
static void Menu_Clamp_Window(const menu_page_t *page);
static void Menu_Set_Display_Page(int new_page_id, uint8 preferred_index);
static void Menu_Draw_Item_Value(const menu_item_t *item, uint8 x, uint8 y);
static void Menu_Draw_List_Row(const menu_page_t *page, uint8 item_index, uint8 visible_index, uint8 selected);
static void Menu_Draw_List_Window(const menu_page_t *page);
static void Menu_Draw_List_Title(const menu_page_t *page);
static void Menu_Draw_Step_Area(const menu_item_t *item, uint8 is_edit_mode);
static uint8 Menu_Adjust_Item_Value(const menu_item_t *item, int direction);
static void Menu_Move_Cursor(const menu_page_t *page, int direction);
static void Menu_Process_Home_Key(uint8 event_code);
static void Menu_Process_Sensor_Key(uint8 event_code);
static void Menu_Process_List_Key(const menu_page_t *page, uint8 is_edit_mode, uint8 item_index, uint8 event_code);
static void Menu_Update_Timebase(uint16 tick_count);
static uint16 Menu_Consume_Ticks_10ms(void);
static void Menu_Render_Current_Page(void);
static void Menu_Activate_Save_Prompt(void);
static void Menu_Cycle_Change_Unit(void);
static void Menu_Clear_Pending_Key_Events(void);
static void Menu_Return_To_Parent_Page(const menu_page_t *page);

void Menu_Set_Service_Enable(uint8 enabled)
{
    menu_service_enabled = enabled ? 1 : 0;

    if (!menu_service_enabled)
    {
        menu_tick_10ms_pending = 0;
        page_elapsed_ms = 0;
        prompt_active = 0;
        prompt_dirty = 0;
        prompt_remaining_ms = 0;
        realtime_due = 0;
        Menu_Clear_Pending_Key_Events();
    }
}

uint8 Menu_Is_Service_Enabled(void)
{
    return menu_service_enabled;
}

void Menu_Tick_10ms(void)
{
    if (!menu_service_enabled)
        return;

    if (menu_tick_10ms_pending < 60000)
        menu_tick_10ms_pending++;
}

static uint8 Menu_Get_Home_Item_Count(void)
{
    return (uint8)(sizeof(menu_home_items) / sizeof(menu_home_items[0]));
}

static const menu_page_t *Menu_Find_Page_By_Id(int page_id)
{
    uint8 i;
    uint8 page_count;

    page_count = (uint8)(sizeof(menu_pages) / sizeof(menu_pages[0]));
    for (i = 0; i < page_count; i++)
    {
        if (menu_pages[i].page_id == page_id)
            return &menu_pages[i];
    }

    return 0;
}

static const menu_page_t *Menu_Find_List_Page_By_Item(int item_page_id, uint8 *item_index)
{
    uint8 i;
    uint8 j;
    uint8 page_count;

    page_count = (uint8)(sizeof(menu_pages) / sizeof(menu_pages[0]));
    for (i = 0; i < page_count; i++)
    {
        if (menu_pages[i].page_type != MENU_PAGE_LIST)
            continue;

        for (j = 0; j < menu_pages[i].item_count; j++)
        {
            if (menu_pages[i].items[j].child_page == item_page_id)
            {
                if (item_index != 0)
                    *item_index = j;
                return &menu_pages[i];
            }
        }
    }

    return 0;
}

static const menu_page_t *Menu_Get_Current_Page(uint8 *is_edit_mode, uint8 *item_index)
{
    const menu_page_t *page;
    uint8 local_item_index;

    if (is_edit_mode != 0)
        *is_edit_mode = 0;
    if (item_index != 0)
        *item_index = 0;

    page = Menu_Find_Page_By_Id(display_codename);
    if (page != 0)
        return page;

    local_item_index = 0;
    page = Menu_Find_List_Page_By_Item(display_codename, &local_item_index);
    if (page != 0)
    {
        if (is_edit_mode != 0)
            *is_edit_mode = 1;
        if (item_index != 0)
            *item_index = local_item_index;
    }

    return page;
}

static void Menu_Clear_Field(uint8 x, uint8 y)
{
    ips114_show_string(x, y, menu_blank_text);
}

static void Menu_Clear_Cursor_Cell(uint8 y)
{
    ips114_show_string(MENU_CURSOR_X, y, " ");
}

static void Menu_Clear_List_Row(const menu_page_t *page, uint8 visible_index)
{
    uint8 y;

    y = (uint8)(page->layout.first_row_y + visible_index * page->layout.row_height);
    Menu_Clear_Cursor_Cell(y);
    Menu_Clear_Field(page->layout.label_x, y);
    Menu_Clear_Field(page->layout.value_x, y);
}

static void Menu_Clamp_Window(const menu_page_t *page)
{
    uint8 visible_count;

    visible_count = page->layout.max_visible_rows;
    if (page->item_count == 0)
    {
        menu_cursor_index = 0;
        menu_scroll_offset = 0;
        return;
    }

    if (menu_cursor_index >= page->item_count)
        menu_cursor_index = 0;

    if (page->item_count <= visible_count)
    {
        menu_scroll_offset = 0;
        return;
    }

    if (menu_cursor_index < menu_scroll_offset)
        menu_scroll_offset = menu_cursor_index;
    else if (menu_cursor_index >= (uint8)(menu_scroll_offset + visible_count))
        menu_scroll_offset = (uint8)(menu_cursor_index - visible_count + 1);
}

static void Menu_Set_Display_Page(int new_page_id, uint8 preferred_index)
{
    const menu_page_t *page;
    uint8 is_edit_mode;
    uint8 item_index;

    display_codename = new_page_id;
    menu_cursor_index = preferred_index;
    menu_previous_cursor_index = preferred_index;
    menu_scroll_offset = 0;
    menu_previous_scroll_offset = 0;
    page_elapsed_ms = 0;
    page_changed = 1;
    list_dirty = 0;
    cursor_dirty = 0;
    value_dirty = 0;
    step_dirty = 0;
    realtime_due = 0;

    is_edit_mode = 0;
    item_index = 0;
    page = Menu_Get_Current_Page(&is_edit_mode, &item_index);
    if (page != 0 && page->page_type == MENU_PAGE_LIST)
    {
        if (is_edit_mode)
            menu_cursor_index = item_index;
        Menu_Clamp_Window(page);
    }
    else
    {
        menu_scroll_offset = 0;
    }

    menu_previous_cursor_index = menu_cursor_index;
    menu_previous_scroll_offset = menu_scroll_offset;
}

static void Menu_Return_To_Parent_Page(const menu_page_t *page)
{
    uint8 preferred_index;
    const menu_page_t *parent_page;
    uint8 i;

    if (page == 0)
        return;

    preferred_index = 0;
    if (page->parent_page_id == 0)
    {
        for (i = 0; i < Menu_Get_Home_Item_Count(); i++)
        {
            if (menu_home_items[i].child_page == page->page_id)
            {
                preferred_index = i;
                break;
            }
        }
        Menu_Set_Display_Page(0, preferred_index);
        return;
    }

    parent_page = Menu_Find_Page_By_Id(page->parent_page_id);
    if (parent_page != 0 && parent_page->page_type == MENU_PAGE_LIST)
    {
        for (i = 0; i < parent_page->item_count; i++)
        {
            if (parent_page->items[i].child_page == page->page_id)
            {
                preferred_index = i;
                break;
            }
        }
    }

    Menu_Set_Display_Page(page->parent_page_id, preferred_index);
}

static void Menu_Draw_Item_Value(const menu_item_t *item, uint8 x, uint8 y)
{
    switch (item->type)
    {
    case MENU_ITEM_FLOAT:
        ips114_show_float(x, y, *(float *)item->data_ptr, item->value_width, item->value_decimals);
        break;
    case MENU_ITEM_INT:
        ips114_show_int32(x, y, *(int *)item->data_ptr, item->value_width);
        break;
    case MENU_ITEM_INT16:
    case MENU_ITEM_SPECIAL:
        ips114_show_int32(x, y, *(int16 *)item->data_ptr, item->value_width);
        break;
    default:
        break;
    }
}

static void Menu_Draw_List_Row(const menu_page_t *page, uint8 item_index, uint8 visible_index, uint8 selected)
{
    uint8 y;
    const menu_item_t *item;

    y = (uint8)(page->layout.first_row_y + visible_index * page->layout.row_height);
    Menu_Clear_List_Row(page, visible_index);

    if (item_index >= page->item_count)
        return;

    item = &page->items[item_index];
    ips114_show_string(MENU_CURSOR_X, y, selected ? "&" : " ");
    ips114_show_string(page->layout.label_x, y, item->label);

    if (item->type != MENU_ITEM_SUBMENU)
        Menu_Draw_Item_Value(item, page->layout.value_x, y);
}

static void Menu_Draw_List_Window(const menu_page_t *page)
{
    uint8 i;
    uint8 item_index;
    uint8 visible_count;

    visible_count = page->layout.max_visible_rows;
    for (i = 0; i < visible_count; i++)
    {
        item_index = (uint8)(menu_scroll_offset + i);
        if (item_index < page->item_count)
            Menu_Draw_List_Row(page, item_index, i, (uint8)(item_index == menu_cursor_index));
        else
            Menu_Clear_List_Row(page, i);
    }
}

static void Menu_Draw_List_Title(const menu_page_t *page)
{
    Menu_Clear_Field(0, page->layout.title_y);
    ips114_show_string(page->layout.title_x, page->layout.title_y, page->title);
}

static void Menu_Draw_Step_Area(const menu_item_t *item, uint8 is_edit_mode)
{
    int int_step;
    float float_step;

    Menu_Clear_Field(MENU_STEP_AREA_X, MENU_STEP_AREA_Y);
    if (!is_edit_mode || item == 0)
        return;

    if (item->type == MENU_ITEM_FLOAT)
    {
        float_step = item->step * (float)change_unit_multiplier;
        ips114_show_float(MENU_STEP_AREA_X, MENU_STEP_AREA_Y, float_step, 4, 3);
    }
    else if (item->type == MENU_ITEM_INT || item->type == MENU_ITEM_INT16)
    {
        int_step = (int)(item->step * (float)change_unit_multiplier);
        ips114_show_int32(MENU_STEP_AREA_X + 8, MENU_STEP_AREA_Y, int_step, 4);
    }
}

static uint8 Menu_Adjust_Item_Value(const menu_item_t *item, int direction)
{
    int int_step;
    float float_step;
    int16 special_value;

    if (direction == 0 || item == 0)
        return 0;

    switch (item->type)
    {
    case MENU_ITEM_FLOAT:
        float_step = item->step * (float)change_unit_multiplier;
        *(float *)item->data_ptr += float_step * (float)direction;
        break;
    case MENU_ITEM_INT:
        int_step = (int)(item->step * (float)change_unit_multiplier);
        *(int *)item->data_ptr += int_step * direction;
        break;
    case MENU_ITEM_INT16:
        int_step = (int)(item->step * (float)change_unit_multiplier);
        *(int16 *)item->data_ptr += (int16)(int_step * direction);
        break;
    case MENU_ITEM_SPECIAL:
        special_value = (direction > 0) ? 1 : -1;
        *(int16 *)item->data_ptr = special_value;
        break;
    default:
        return 0;
    }

    control_apply_config();
    return 1;
}

static void Menu_Move_Cursor(const menu_page_t *page, int direction)
{
    int next_index;
    uint8 visible_count;

    if (page == 0 || page->item_count == 0 || direction == 0)
        return;

    menu_previous_cursor_index = menu_cursor_index;
    menu_previous_scroll_offset = menu_scroll_offset;
    next_index = (int)menu_cursor_index + direction;
    if (next_index < 0)
        next_index = (int)page->item_count - 1;
    else if (next_index >= page->item_count)
        next_index = 0;
    menu_cursor_index = (uint8)next_index;

    visible_count = page->layout.max_visible_rows;
    if (page->item_count <= visible_count)
        menu_scroll_offset = 0;
    else if (menu_cursor_index < menu_scroll_offset)
        menu_scroll_offset = menu_cursor_index;
    else if (menu_cursor_index >= (uint8)(menu_scroll_offset + visible_count))
        menu_scroll_offset = (uint8)(menu_cursor_index - visible_count + 1);

    if (menu_scroll_offset != menu_previous_scroll_offset)
        list_dirty = 1;
    else if (menu_cursor_index != menu_previous_cursor_index)
        cursor_dirty = 1;
}

static void Menu_Cycle_Change_Unit(void)
{
    keystroke_three_count++;
    if (keystroke_three_count >= 3)
        keystroke_three_count = 0;

    if (keystroke_three_count == 0)
        change_unit_multiplier = 1;
    else if (keystroke_three_count == 1)
        change_unit_multiplier = 10;
    else
        change_unit_multiplier = 100;

    step_dirty = 1;
}

static void Menu_Process_Home_Key(uint8 event_code)
{
    switch (event_code)
    {
    case KEYSTROKE_ONE:
    case KEYSTROKE_ONE_LONG:
        menu_previous_cursor_index = menu_cursor_index;
        if (menu_cursor_index == 0)
            menu_cursor_index = (uint8)(Menu_Get_Home_Item_Count() - 1);
        else
            menu_cursor_index--;
        cursor_dirty = 1;
        break;
    case KEYSTROKE_TWO:
    case KEYSTROKE_TWO_LONG:
        menu_previous_cursor_index = menu_cursor_index;
        if (menu_cursor_index >= (uint8)(Menu_Get_Home_Item_Count() - 1))
            menu_cursor_index = 0;
        else
            menu_cursor_index++;
        cursor_dirty = 1;
        break;
    case KEYSTROKE_THREE:
        if (menu_cursor_index < Menu_Get_Home_Item_Count())
            Menu_Set_Display_Page(menu_home_items[menu_cursor_index].child_page, 0);
        break;
    case KEYSTROKE_FOUR:
    case KEYSTROKE_FOUR_LONG:
        if (EEPROM_MODE == 1)
            Menu_Activate_Save_Prompt();
        break;
    default:
        break;
    }
}

static void Menu_Process_Sensor_Key(uint8 event_code)
{
    const menu_page_t *page;

    switch (event_code)
    {
    case KEYSTROKE_FOUR:
    case KEYSTROKE_FOUR_LONG:
        page = Menu_Find_Page_By_Id(display_codename);
        Menu_Return_To_Parent_Page(page);
        break;
    default:
        break;
    }
}

static void Menu_Process_List_Key(const menu_page_t *page, uint8 is_edit_mode, uint8 item_index, uint8 event_code)
{
    const menu_item_t *item;

    if (page == 0)
        return;

    if (!is_edit_mode)
    {
        switch (event_code)
        {
        case KEYSTROKE_ONE:
        case KEYSTROKE_ONE_LONG:
            Menu_Move_Cursor(page, -1);
            break;
        case KEYSTROKE_TWO:
        case KEYSTROKE_TWO_LONG:
            Menu_Move_Cursor(page, 1);
            break;
        case KEYSTROKE_THREE:
            if (menu_cursor_index < page->item_count)
                Menu_Set_Display_Page(page->items[menu_cursor_index].child_page, menu_cursor_index);
            break;
        case KEYSTROKE_FOUR:
        case KEYSTROKE_FOUR_LONG:
            Menu_Return_To_Parent_Page(page);
            break;
        default:
            break;
        }
        return;
    }

    if (item_index >= page->item_count)
        return;

    item = &page->items[item_index];
    switch (event_code)
    {
    case KEYSTROKE_ONE:
    case KEYSTROKE_ONE_LONG:
        if (Menu_Adjust_Item_Value(item, 1))
            value_dirty = 1;
        break;
    case KEYSTROKE_TWO:
    case KEYSTROKE_TWO_LONG:
        if (Menu_Adjust_Item_Value(item, -1))
            value_dirty = 1;
        break;
    case KEYSTROKE_THREE:
        if (item->type == MENU_ITEM_FLOAT || item->type == MENU_ITEM_INT || item->type == MENU_ITEM_INT16)
            Menu_Cycle_Change_Unit();
        break;
    case KEYSTROKE_FOUR:
    case KEYSTROKE_FOUR_LONG:
        Menu_Set_Display_Page(page->page_id, item_index);
        break;
    default:
        break;
    }
}

static uint16 Menu_Consume_Ticks_10ms(void)
{
    uint16 tick_count;
    uint8 irq_state;

    irq_state = EA;
    EA = 0;
    tick_count = menu_tick_10ms_pending;
    menu_tick_10ms_pending = 0;
    EA = irq_state;

    return tick_count;
}

static void Menu_Update_Timebase(uint16 tick_count)
{
    const menu_page_t *page;
    uint8 is_edit_mode;
    uint8 item_index;
    uint16 elapsed_ms;

    if (tick_count == 0)
        return;

    elapsed_ms = (uint16)(tick_count * 10);

    if (prompt_active)
    {
        if (elapsed_ms >= prompt_remaining_ms)
        {
            prompt_active = 0;
            prompt_dirty = 0;
            prompt_remaining_ms = 0;
            page_changed = 1;
        }
        else
        {
            prompt_remaining_ms = (uint16)(prompt_remaining_ms - elapsed_ms);
        }
        return;
    }

    is_edit_mode = 0;
    item_index = 0;
    page = Menu_Get_Current_Page(&is_edit_mode, &item_index);
    if (page == 0 || page->refresh_period_ms == 0 || is_edit_mode)
        return;

    page_elapsed_ms = (uint16)(page_elapsed_ms + elapsed_ms);
    if (page_elapsed_ms >= page->refresh_period_ms)
    {
        page_elapsed_ms = (uint16)(page_elapsed_ms % page->refresh_period_ms);
        realtime_due = 1;
    }
}

static void Menu_Activate_Save_Prompt(void)
{
    config_save();
    prompt_active = 1;
    prompt_dirty = 1;
    prompt_remaining_ms = MENU_PROMPT_DURATION_MS;
    page_elapsed_ms = 0;
    Menu_Clear_Pending_Key_Events();
}

static void Menu_Clear_Pending_Key_Events(void)
{
    while (Keystroke_Get_Event() != 0)
    {
    }
}

static void Menu_Draw_Home_Static(void)
{
    uint8 i;
    uint8 y;

    ips114_show_string(menu_home_layout.title_x, menu_home_layout.title_y, "MENU");
    for (i = 0; i < Menu_Get_Home_Item_Count(); i++)
    {
        y = (uint8)(menu_home_layout.first_row_y + i * menu_home_layout.row_height);
        ips114_show_string(menu_home_layout.label_x, y, menu_home_items[i].label);
    }

    ips114_show_string(105, 1 * 18, "Err");
    ips114_show_string(105, 2 * 18, "steer");
    ips114_show_string(105, 3 * 18, "angle");
    ips114_show_string(105, 4 * 18, "V_bat");
}

static void Menu_Draw_Home_Cursor(void)
{
    uint8 i;
    uint8 y;

    for (i = 0; i < Menu_Get_Home_Item_Count(); i++)
    {
        y = (uint8)(menu_home_layout.first_row_y + i * menu_home_layout.row_height);
        Menu_Clear_Cursor_Cell(y);
    }
    y = (uint8)(menu_home_layout.first_row_y + menu_cursor_index * menu_home_layout.row_height);
    ips114_show_string(MENU_CURSOR_X, y, ">");
}

static void Menu_Draw_Home_Dynamic(void)
{
    ips114_show_float(184, 1 * 18, Err, 3, 2);
    ips114_show_float(184, 2 * 18, PID.steer.output, 3, 1);
    ips114_show_float(184, 3 * 18, PID.angle.output, 3, 1);
    ips114_show_float(184, 4 * 18, dianya, 4, 2);
}

static void Menu_Draw_Sensor_Static(void)
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
}

static void Menu_Draw_Sensor_Dynamic(void)
{
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

static void Menu_Render_Current_Page(void)
{
    const menu_page_t *page;
    uint8 is_edit_mode;
    uint8 item_index;
    const menu_item_t *current_item;
    uint8 old_visible_index;
    uint8 new_visible_index;

    if (prompt_active)
    {
        if (prompt_dirty)
        {
            ips114_clear(RGB565_WHITE);
            ips114_show_string((12 * 8) - 24, 3 * 18, "SAVED OK!");
            prompt_dirty = 0;
        }
        return;
    }

    if (!page_changed && !list_dirty && !cursor_dirty && !value_dirty && !step_dirty && !realtime_due)
        return;

    is_edit_mode = 0;
    item_index = 0;
    current_item = 0;
    page = Menu_Get_Current_Page(&is_edit_mode, &item_index);
    if (page == 0)
        return;

    if (is_edit_mode && item_index < page->item_count)
        current_item = &page->items[item_index];

    if (page_changed)
    {
        ips114_clear(RGB565_WHITE);
        if (page->draw_static_hook != 0)
            page->draw_static_hook();
        else
            Menu_Draw_List_Title(page);

        if (page->page_type == MENU_PAGE_LIST)
        {
            Menu_Draw_List_Window(page);
            Menu_Draw_Step_Area(current_item, is_edit_mode);
        }
        else if (page->page_type == MENU_PAGE_HOME)
        {
            if (page->draw_dynamic_hook != 0)
                page->draw_dynamic_hook();
            Menu_Draw_Home_Cursor();
        }
        else if (page->draw_dynamic_hook != 0)
        {
            page->draw_dynamic_hook();
        }

        page_changed = 0;
        list_dirty = 0;
        cursor_dirty = 0;
        value_dirty = 0;
        step_dirty = 0;
        realtime_due = 0;
        return;
    }

    if (page->page_type == MENU_PAGE_HOME)
    {
        if (realtime_due)
            Menu_Draw_Home_Dynamic();
        if (cursor_dirty)
            Menu_Draw_Home_Cursor();
        cursor_dirty = 0;
        realtime_due = 0;
        return;
    }

    if (page->page_type == MENU_PAGE_SENSOR)
    {
        if (realtime_due && page->draw_dynamic_hook != 0)
            page->draw_dynamic_hook();
        realtime_due = 0;
        return;
    }

    if (list_dirty)
    {
        Menu_Draw_List_Window(page);
        list_dirty = 0;
        cursor_dirty = 0;
        value_dirty = 0;
    }
    else if (cursor_dirty)
    {
        old_visible_index = (uint8)(menu_previous_cursor_index - menu_scroll_offset);
        new_visible_index = (uint8)(menu_cursor_index - menu_scroll_offset);
        Menu_Draw_List_Row(page, menu_previous_cursor_index, old_visible_index, 0);
        Menu_Draw_List_Row(page, menu_cursor_index, new_visible_index, 1);
        cursor_dirty = 0;
    }

    if (value_dirty)
    {
        Menu_Draw_List_Row(page, menu_cursor_index, (uint8)(menu_cursor_index - menu_scroll_offset), 1);
        value_dirty = 0;
    }

    if (step_dirty)
    {
        Menu_Draw_Step_Area(current_item, is_edit_mode);
        step_dirty = 0;
    }
}

void Keystroke_Menu(void)
{
    const menu_page_t *page;
    uint8 event_code;
    uint8 is_edit_mode;
    uint8 item_index;
    uint16 tick_count;

    if (!menu_service_enabled)
        return;

    tick_count = Menu_Consume_Ticks_10ms();
    Menu_Update_Timebase(tick_count);

    if (prompt_active)
    {
        Menu_Clear_Pending_Key_Events();
        Menu_Render_Current_Page();
        return;
    }

    event_code = Keystroke_Get_Event();
    if (event_code != 0)
    {
        is_edit_mode = 0;
        item_index = 0;
        page = Menu_Get_Current_Page(&is_edit_mode, &item_index);
        if (page != 0)
        {
            if (page->page_type == MENU_PAGE_HOME)
                Menu_Process_Home_Key(event_code);
            else if (page->page_type == MENU_PAGE_SENSOR)
                Menu_Process_Sensor_Key(event_code);
            else
                Menu_Process_List_Key(page, is_edit_mode, item_index, event_code);
        }
    }

    Menu_Render_Current_Page();
}

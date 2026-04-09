#ifndef _MENU_H_
#define _MENU_H_

#include "zf_common_typedef.h"

typedef enum
{
    MENU_PAGE_HOME = 0,
    MENU_PAGE_LIST = 1,
    MENU_PAGE_SENSOR = 2
} menu_page_type_t;

typedef enum
{
    MENU_ITEM_SUBMENU = 0,
    MENU_ITEM_INT = 1,
    MENU_ITEM_INT16 = 2,
    MENU_ITEM_FLOAT = 3,
    MENU_ITEM_SPECIAL = 4
} menu_item_type_t;

typedef struct
{
    uint8 title_x;
    uint8 title_y;
    uint8 first_row_y;
    uint8 row_height;
    uint8 label_x;
    uint8 value_x;
    uint8 max_visible_rows;
} menu_layout_t;

typedef struct
{
    const char *label;
    menu_item_type_t type;
    void *data_ptr;
    float step;
    int child_page;
    uint8 value_width;
    uint8 value_decimals;
} menu_item_t;

typedef struct
{
    int page_id;
    int parent_page_id;
    const char *title;
    menu_page_type_t page_type;
    uint16 refresh_period_ms;
    menu_layout_t layout;
    const menu_item_t *items;
    uint8 item_count;
    void (*draw_static_hook)(void);
    void (*draw_dynamic_hook)(void);
} menu_page_t;

/**
 * @brief 当前显示页面 ID
 * @details 0 为 HOME；普通目录页使用根页面 ID；参数编辑页使用项表中定义的 child_page
 */
extern int display_codename;

/**
 * @brief 设置菜单服务使能状态
 * @details 关闭时，中断节拍不再执行扫键和 UI tick，主循环入口也直接返回
 */
void Menu_Set_Service_Enable(uint8 enabled);

/**
 * @brief 查询菜单服务是否使能
 */
uint8 Menu_Is_Service_Enabled(void);

/**
 * @brief 菜单 10ms 节拍入口
 * @details 在 TM1 10ms 中断中调用，只累计 UI 时基，不直接刷屏
 */
void Menu_Tick_10ms(void);

/**
 * @brief 菜单主循环服务函数
 * @details 消费按键事件、推进状态、按需刷新，然后立即返回
 */
void Keystroke_Menu(void);

#endif /* _MENU_H_ */

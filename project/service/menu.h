#ifndef _MENU_H_
#define _MENU_H_

#include "zf_common_typedef.h"

/**
 * @file menu.h
 * @brief 菜单系统对外类型与接口声明
 * @details
 * 菜单采用静态页面描述方式：页面、布局、条目均在编译期定义，
 * 运行时仅根据按键事件与时基更新显示状态，避免动态内存开销。
 */

typedef enum
{
    MENU_PAGE_HOME = 0,
    MENU_PAGE_LIST = 1,
    MENU_PAGE_SENSOR = 2
} menu_page_type_t;

typedef enum
{
    MENU_ITEM_SUBMENU = 0, /* 目录项：仅跳转，不直接显示数值 */
    MENU_ITEM_INT = 1,     /* int 参数项 */
    MENU_ITEM_INT16 = 2,   /* int16 参数项 */
    MENU_ITEM_FLOAT = 3,   /* float 参数项 */
    MENU_ITEM_SPECIAL = 4  /* 特殊整型项（按业务规则处理） */
} menu_item_type_t;

typedef struct
{
    uint8 title_x;          /* 页面标题 X 坐标 */
    uint8 title_y;          /* 页面标题 Y 坐标 */
    uint8 first_row_y;      /* 第一行条目 Y 坐标 */
    uint8 row_height;       /* 条目行高 */
    uint8 label_x;          /* 条目名称列 X 坐标 */
    uint8 value_x;          /* 条目数值列 X 坐标 */
    uint8 max_visible_rows; /* 当前页面最多可见行数 */
} menu_layout_t;

typedef struct
{
    const char *label;     /* 条目文本 */
    menu_item_type_t type; /* 条目类型 */
    void *data_ptr;        /* 参数地址（目录项可为空） */
    float step;            /* 调节步进 */
    int child_page;        /* 子页面 ID（目录项有效） */
    uint8 value_width;     /* 数值显示宽度 */
    uint8 value_decimals;  /* 小数位数 */
} menu_item_t;

typedef struct
{
    int page_id;                     /* 页面唯一 ID */
    int parent_page_id;              /* 父页面 ID，0 表示 HOME */
    const char *title;               /* 页面标题 */
    menu_page_type_t page_type;      /* 页面类型 */
    uint16 refresh_period_ms;        /* 动态刷新周期，0 表示按需刷新 */
    menu_layout_t layout;            /* 页面布局配置 */
    const menu_item_t *items;        /* 条目数组指针 */
    uint8 item_count;                /* 条目数量 */
    void (*draw_static_hook)(void);  /* 页面静态绘制钩子 */
    void (*draw_dynamic_hook)(void); /* 页面动态绘制钩子 */
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

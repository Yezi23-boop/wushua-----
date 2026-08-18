# 赛道元素可配置顺序 Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把当前硬编码的赛道元素顺序改成代码默认序列 + 菜单/EEPROM 可配置序列。

**Architecture:** 新增一个轻量的元素序列常量头文件，供 EEPROM 配置结构、元素仲裁和菜单共享元素编号与默认序列。`a_run_track_element.c` 保留各元素自己的状态机，只把“完成后去哪”改为固定 6 槽序列表驱动，并用有界扫描避免 5ms 链路死循环。

**Tech Stack:** C89, Keil C251, PowerShell, 现有 EEPROM 4 字节槽位模型, IPS114 菜单系统。

---

## Chunk 1: 配置结构、默认序列与 EEPROM 校验

### Task 1: 新增共享元素序列常量

**Files:**
- Create: `project/user/track_element_config.h`
- Modify: `project/user/a_run_track_element.h`
- Modify: `project/service/eeprom.h`

- [ ] **Step 1: 创建共享常量头文件**

在 `project/user/track_element_config.h` 新增：

```c
#ifndef __TRACK_ELEMENT_CONFIG_H__
#define __TRACK_ELEMENT_CONFIG_H__

#include "zf_common_typedef.h"

#define TRACK_ELEMENT_NONE 0
#define TRACK_ELEMENT_LEFT_RING 1
#define TRACK_ELEMENT_RIGHT_RING 2
#define TRACK_ELEMENT_CYLINDER 3
#define TRACK_ELEMENT_WALL 4
#define TRACK_ELEMENT_SEESAW 5

#define TRACK_ELEMENT_SEQUENCE_MAX 6
#define TRACK_ELEMENT_DEFAULT_LEN 4
#define TRACK_ELEMENT_DEFAULT_0 TRACK_ELEMENT_LEFT_RING
#define TRACK_ELEMENT_DEFAULT_1 TRACK_ELEMENT_CYLINDER
#define TRACK_ELEMENT_DEFAULT_2 TRACK_ELEMENT_SEESAW
#define TRACK_ELEMENT_DEFAULT_3 TRACK_ELEMENT_WALL
#define TRACK_ELEMENT_DEFAULT_4 TRACK_ELEMENT_NONE
#define TRACK_ELEMENT_DEFAULT_5 TRACK_ELEMENT_NONE

#endif /* __TRACK_ELEMENT_CONFIG_H__ */
```

- [ ] **Step 2: 改 `a_run_track_element.h` 使用共享头**

在 `project/user/a_run_track_element.h` 中包含：

```c
#include "track_element_config.h"
```

删除该文件里原本的 `TRACK_ELEMENT_*` 重复宏定义，避免双定义。

- [ ] **Step 3: 改 `eeprom.h` 使用共享头并扩展配置**

在 `project/service/eeprom.h` 中包含：

```c
#include "track_element_config.h"
```

在 `AppStartConfig` 末尾新增：

```c
int16 element_len; /**< 元素序列有效长度，范围 1~6。 */
int16 element_seq[TRACK_ELEMENT_SEQUENCE_MAX]; /**< 元素序列槽位：0空、1左环、2右环预留、3圆桶、4墙面、5跷跷板。 */
```

- [ ] **Step 4: 单文件预编译检查头文件包含关系**

运行：

```powershell
& 'D:\keil_5\C251\BIN\C251.EXE' '..\service\eeprom.c' 'LARGE' 'NOALIAS' 'WARNINGLEVEL(3)' 'OPTIMIZE(0,SIZE)' 'BROWSE' 'INCDIR(..\..\libraries\zf_common;..\..\libraries\zf_components;..\..\libraries\zf_device;..\..\libraries\zf_driver;..\user;..\service;..\code)' 'DEBUG' 'PRINT(.\out_file\eeprom.lst)' 'OBJECT(.\out_file\eeprom.obj)'
```

工作目录：`project/mdk`

Expected: 必须 `0 Error(s)`，且不应报找不到 `track_element_config.h`。若此时还未完成后续 EEPROM 字段读写，允许出现与未完成字段无关的既有 warning。

### Task 2: EEPROM 默认值、读写和 RAM 校验

**Files:**
- Modify: `project/service/eeprom.c`
- Modify: `project/user/int_user.c`

- [ ] **Step 1: 在 `eeprom.c` 增加默认序列填充 helper**

在 `eeprom.c` 顶部静态声明区新增声明：

```c
static void eeprom_set_default_element_sequence(AppConfig *config);
```

在 `eeprom_load_defaults()` 之前实现，避免 C89 隐式声明；函数必须带中文 Doxygen 头：

```c
/**
 * @brief 填充默认赛道元素序列。
 * @param config 待写入默认序列的配置对象。
 *
 * 默认序列只在初始化和非法 EEPROM 数据回退时使用，避免多处手写导致比赛现场配置不一致。
 */
static void eeprom_set_default_element_sequence(AppConfig *config)
{
    config->start.element_len = TRACK_ELEMENT_DEFAULT_LEN;
    config->start.element_seq[0] = TRACK_ELEMENT_DEFAULT_0;
    config->start.element_seq[1] = TRACK_ELEMENT_DEFAULT_1;
    config->start.element_seq[2] = TRACK_ELEMENT_DEFAULT_2;
    config->start.element_seq[3] = TRACK_ELEMENT_DEFAULT_3;
    config->start.element_seq[4] = TRACK_ELEMENT_DEFAULT_4;
    config->start.element_seq[5] = TRACK_ELEMENT_DEFAULT_5;
}
```

在 `eeprom_load_defaults()` 末尾调用它。

- [ ] **Step 2: EEPROM 读写槽位 30~36**

在 `eeprom_read_config()` 末尾读取：

```c
config->start.element_len = (int16)read_int(30);
config->start.element_seq[0] = (int16)read_int(31);
config->start.element_seq[1] = (int16)read_int(32);
config->start.element_seq[2] = (int16)read_int(33);
config->start.element_seq[3] = (int16)read_int(34);
config->start.element_seq[4] = (int16)read_int(35);
config->start.element_seq[5] = (int16)read_int(36);
```

在 `eeprom_write_config()` 末尾保存对应槽位。

- [ ] **Step 3: 在 `int_user.c` 增加配置校验函数**

在 `int_user.c` 静态函数声明区新增：

```c
static int8 control_is_executable_element(int16 element);
static void control_set_default_element_sequence(void);
static void control_validate_element_sequence(void);
```

在 `control_apply_config()` 之前实现以下 helper，全部补中文 Doxygen 函数头：

```c
/**
 * @brief 判断元素编号当前是否可执行。
 * @param element 元素编号：0空、1左环、2右环预留、3圆桶、4墙面、5跷跷板。
 * @return int8 1-当前可执行，0-需要跳过。
 */
static int8 control_is_executable_element(int16 element)
{
    if (element == TRACK_ELEMENT_LEFT_RING ||
        element == TRACK_ELEMENT_CYLINDER ||
        element == TRACK_ELEMENT_WALL)
    {
        return 1;
    }
    if (element == TRACK_ELEMENT_SEESAW && app.fly.fly_ramp_enable == 1)
    {
        return 1;
    }
    return 0;
}

/**
 * @brief 将运行参数中的赛道元素序列恢复为默认值。
 *
 * 只修改 RAM 中的 app 配置；是否写入 EEPROM 由菜单保存流程决定。
 */
static void control_set_default_element_sequence(void)
{
    app.start.element_len = TRACK_ELEMENT_DEFAULT_LEN;
    app.start.element_seq[0] = TRACK_ELEMENT_DEFAULT_0;
    app.start.element_seq[1] = TRACK_ELEMENT_DEFAULT_1;
    app.start.element_seq[2] = TRACK_ELEMENT_DEFAULT_2;
    app.start.element_seq[3] = TRACK_ELEMENT_DEFAULT_3;
    app.start.element_seq[4] = TRACK_ELEMENT_DEFAULT_4;
    app.start.element_seq[5] = TRACK_ELEMENT_DEFAULT_5;
}

/**
 * @brief 校验 EEPROM 读入的元素序列并在 RAM 中兜底。
 *
 * 旧 EEPROM 新槽位可能含随机值；该函数保证 5ms 仲裁只会读到有界长度和合法元素编号。
 */
static void control_validate_element_sequence(void)
{
    uint8 i;
    uint8 has_executable;

    if (app.start.element_len < 1 || app.start.element_len > TRACK_ELEMENT_SEQUENCE_MAX)
    {
        control_set_default_element_sequence();
        return;
    }

    has_executable = 0;
    for (i = 0; i < TRACK_ELEMENT_SEQUENCE_MAX; i++)
    {
        if (app.start.element_seq[i] < TRACK_ELEMENT_NONE ||
            app.start.element_seq[i] > TRACK_ELEMENT_SEESAW)
        {
            app.start.element_seq[i] = TRACK_ELEMENT_NONE;
        }
        if (i < (uint8)app.start.element_len &&
            control_is_executable_element(app.start.element_seq[i]) != 0)
        {
            has_executable = 1;
        }
    }

    if (has_executable == 0)
    {
        control_set_default_element_sequence();
    }
}
```

在 `control_apply_config()` 中调用 `control_validate_element_sequence()`。

- [ ] **Step 4: 单文件编译**

运行：

```powershell
& 'D:\keil_5\C251\BIN\C251.EXE' '..\service\eeprom.c' 'LARGE' 'NOALIAS' 'WARNINGLEVEL(3)' 'OPTIMIZE(0,SIZE)' 'BROWSE' 'INCDIR(..\..\libraries\zf_common;..\..\libraries\zf_components;..\..\libraries\zf_device;..\..\libraries\zf_driver;..\user;..\service;..\code)' 'DEBUG' 'PRINT(.\out_file\eeprom.lst)' 'OBJECT(.\out_file\eeprom.obj)'
& 'D:\keil_5\C251\BIN\C251.EXE' '..\user\int_user.c' 'LARGE' 'NOALIAS' 'WARNINGLEVEL(3)' 'OPTIMIZE(0,SIZE)' 'BROWSE' 'INCDIR(..\..\libraries\zf_common;..\..\libraries\zf_components;..\..\libraries\zf_device;..\..\libraries\zf_driver;..\user;..\service;..\code)' 'DEBUG' 'PRINT(.\out_file\int_user.lst)' 'OBJECT(.\out_file\int_user.obj)'
```

Expected: `0 WARNING(S), 0 ERROR(S)` for both files.

- [ ] **Step 5: Commit chunk 1**

```powershell
git add project/user/track_element_config.h project/user/a_run_track_element.h project/service/eeprom.h project/service/eeprom.c project/user/int_user.c
git commit -m "增加赛道元素序列配置"
```

---

## Chunk 2: 元素仲裁改为序列表驱动

### Task 3: 增加运行时序列下标和有界前进

**Files:**
- Modify: `project/user/a_run_track_element.c`

- [ ] **Step 1: 新增运行时下标和 helper 声明**

在静态变量区新增：

```c
static uint8 element_index = 0; /**< 当前元素序列下标，只在 5ms 元素仲裁中更新。 */
```

在 `enum TrackElement` 定义之后新增静态声明。不要放到文件最顶部，因为这些原型使用 `enum TrackElement`，在 C89/Keil C251 下必须等枚举类型已经声明后再使用：

```c
static int8 track_element_is_executable(int16 element);
static enum TrackElement track_element_default_first(void);
static void track_element_enter(enum TrackElement element);
static void track_element_advance_to_next(void);
static void track_element_enter_first_valid(void);
```

- [ ] **Step 2: 实现可执行判断**

所有新增静态函数必须补中文 Doxygen 头，说明 5ms 调用上下文、参数和返回值。

```c
/**
 * @brief 判断元素编号当前是否可由仲裁状态机执行。
 * @param element 元素编号，来源于 app.start.element_seq。
 * @return int8 1-可执行，0-应跳过。
 *
 * @note 由 5ms 元素仲裁调用，仅做常量比较和飞坡开关判断。
 */
static int8 track_element_is_executable(int16 element)
{
    if (element == ELEMENT_LEFT_RING ||
        element == ELEMENT_CYLINDER ||
        element == ELEMENT_WALL)
    {
        return 1;
    }
    if (element == ELEMENT_SEESAW && app.fly.fly_ramp_enable == 1)
    {
        return 1;
    }
    return 0;
}
```

`ELEMENT_RIGHT_RING` 当前返回 0，让它被跳过。

- [ ] **Step 3: 实现元素进入清理**

`track_element_enter()` 必须按目标元素清理残留：

```text
左圆环：ring_reset_state(); cylinder_reset_state(); wall_reset_state(); a_run_fly_reset();
圆桶：ring_take_finish_event(); ring_reset_state(); wall_reset_state(); a_run_fly_reset(); cylinder_start_wait_top();
墙面：ring_take_finish_event(); ring_reset_state(); cylinder_reset_state(); a_run_fly_reset(); wall_start_wait_signal();
跷跷板：ring_take_finish_event(); ring_reset_state(); cylinder_reset_state(); wall_reset_state(); a_run_fly_reset();
其他：回到默认第一个可执行元素
```

设置：

```c
expected_element = element;
```

- [ ] **Step 4: 实现有界前进**

`track_element_advance_to_next()` 逻辑：

```c
static void track_element_advance_to_next(void)
{
    uint8 scan_count;
    int16 next_element;

    if (app.start.element_len < 1 || app.start.element_len > TRACK_ELEMENT_SEQUENCE_MAX)
    {
        element_index = 0;
        track_element_enter(track_element_default_first());
        return;
    }

    for (scan_count = 0; scan_count < TRACK_ELEMENT_SEQUENCE_MAX; scan_count++)
    {
        element_index++;
        if (element_index >= (uint8)app.start.element_len)
        {
            element_index = 0;
        }

        next_element = app.start.element_seq[element_index];
        if (track_element_is_executable(next_element) != 0)
        {
            track_element_enter((enum TrackElement)next_element);
            return;
        }
    }

    element_index = 0;
    track_element_enter(track_element_default_first());
}
```

`track_element_default_first()` 返回 `TRACK_ELEMENT_DEFAULT_0`。

- [ ] **Step 5: 复位时进入第一个有效元素**

`track_element_reset_state()` 不再直接写死 `expected_element = ELEMENT_LEFT_RING`，改为：

```text
先清所有元素状态和 ADC 历史
element_index = 0
调用 track_element_enter_first_valid()
```

`track_element_enter_first_valid()` 必须：

```text
1. 若 element_len < 1 或 > 6，直接进入默认第一个元素，并设置 element_index = 0。
2. 在 0..element_len-1 内最多扫描 6 次。
3. 找到可执行元素时，同步设置 element_index = 该槽位下标，并调用 track_element_enter()。
4. 找不到时设置 element_index = 0，并进入默认第一个元素。
```

### Task 4: 改写 `a_run_track_element_update_gate()`

**Files:**
- Modify: `project/user/a_run_track_element.c`

- [ ] **Step 1: 保留电感历史刷新和 circle_flags 复位**

函数开头的 `cylinder_vz`、`ring_adc_rising`、`ring_last_ad*` 刷新逻辑保持不变。

- [ ] **Step 2: 用 `switch (expected_element)` 代替硬编码链**

重写主体：

```text
ELEMENT_LEFT_RING:
  circle_check_l(1)
  如果 ring_take_finish_event() != 0，track_element_advance_to_next()

ELEMENT_CYLINDER:
  circle_check_l(0)
  清 ring event
  如果 cylinder_update_5ms() 完成，track_element_advance_to_next()

ELEMENT_SEESAW:
  circle_check_l(0)
  清 ring event
  如果 fly_ramp_enable != 1，track_element_advance_to_next()
  否则如果 a_run_fly_take_finish_event()，track_element_advance_to_next()

ELEMENT_WALL:
  circle_check_l(0)
  清 ring event
  如果 wall_update_5ms() 完成，track_element_advance_to_next()

ELEMENT_RIGHT_RING:
  track_element_advance_to_next()

default:
  track_element_advance_to_next()
```

- [ ] **Step 3: 更新函数注释并做有界扫描检查**

更新 `a_run_track_element_update_gate()` 的 Doxygen 注释，去掉硬编码“左环->圆桶->跷跷板->墙面”的描述，改为说明顺序由 `app.start.element_len` 和 `app.start.element_seq[]` 决定。

人工检查：

```text
所有序列扫描都以 TRACK_ELEMENT_SEQUENCE_MAX 为上界。
track_element_enter() 会清理非当前元素的完成事件和状态。
右圆环不会进入左圆环逻辑。
```

- [ ] **Step 4: 单文件编译**

运行：

```powershell
& 'D:\keil_5\C251\BIN\C251.EXE' '..\user\a_run_track_element.c' 'LARGE' 'NOALIAS' 'WARNINGLEVEL(3)' 'OPTIMIZE(0,SIZE)' 'BROWSE' 'INCDIR(..\..\libraries\zf_common;..\..\libraries\zf_components;..\..\libraries\zf_device;..\..\libraries\zf_driver;..\user;..\service;..\code)' 'DEBUG' 'PRINT(.\out_file\a_run_track_element.lst)' 'OBJECT(.\out_file\a_run_track_element.obj)'
```

Expected: `0 WARNING(S), 0 ERROR(S)`.

- [ ] **Step 5: Commit chunk 2**

```powershell
git add project/user/a_run_track_element.c
git commit -m "改为按元素序列表仲裁"
```

---

## Chunk 3: 菜单接入 `elem_len` 和 `E1~E6`

### Task 5: 增加 ELEM 菜单页和有界 int16 编辑

**Files:**
- Modify: `project/service/menu.c`

- [ ] **Step 1: 复用 START 页第 6 行作为 ELEM 入口**

不要增加首页第 7 行，IPS114 高度不足，`7 * 18 = 126` 会让 16px 高字符越界。保持首页 6 行不变，把 `START` 页第 6 行从 `trk_mode` 改成 `ELEM` 入口。

菜单层级固定为：

```text
1      START 根页
16     ELEM_LEN 页：编辑 LEN，K3 进入槽位页
160    ELEM 槽位根页：显示 E1~E6
1601   编辑 E1
1602   编辑 E2
1603   编辑 E3
1604   编辑 E4
1605   编辑 E5
1606   编辑 E6
```

修改：

```c
#define MENU_PAGE_COUNT 8
```

保持 `MENU_HOME_ROW_MAX` 为 `6 * MENU_ROW_HEIGHT`。`menu_have_sub[]` 增加：

```c
16, 160, 1601, 1602, 1603, 1604, 1605, 1606
```

`Menu_Draw_Home()` 不新增第 7 行，避免显示越界。

- [ ] **Step 2: START 页第 6 行改为 ELEM 入口并同步导航上限**

`Menu_Draw_Start()`：

```text
第 6 行 label 从 trk_mode 改为 ELEM
右侧显示 app.start.element_len，便于在 START 页快速看到有效长度
```

`Menu_Start_Process()` 的 case 16 不直接编辑 `element_len`，而是进入 `ELEM_LEN` 页。沿用现有菜单逻辑：START 根页按 K3 进入 display_codename 16。

新增 `Menu_Draw_Element_Len()` 和 `Menu_Element_Len_Process()`：

```c
static void Menu_Draw_Element_Len(int edit_line);
static void Menu_Element_Len_Process(void);
```

`Menu_Element_Len_Process()` 规则：

```text
display_codename == 16 时显示 LEN。
K1/K1_LONG：element_len 加 1，超过 6 回到 1。
K2/K2_LONG：element_len 减 1，小于 1 回到 6。
K3：进入 display_codename = 160，cursor_row = MENU_ROW_MIN，清屏并绘制 E1~E6。
K4/K4_LONG：返回 START 根页。
改动 LEN 后调用 control_apply_config()。
```

同时确认：

```text
Menu_Get_Page_Row_Max() case 1 返回 6 * MENU_ROW_HEIGHT。
Menu_Render_Current_Page() 中 START 根页导航上限使用 6 * MENU_ROW_HEIGHT。
```

保留 `track_mode` 字段和 EEPROM 槽位，不在菜单显示它。

- [ ] **Step 3: 新增有界 int16 编辑函数**

新增静态声明：

```c
static void Menu_Process_Int16_Bounded_Value(int16 *parameter, int16 min_value, int16 max_value);
```

实现规则：

```text
先调用 Menu_Read_Key_Event()，无事件直接 return。
调用 Menu_Handle_Common_Key(keystroke_label)，保留返回键和通用菜单行为。
K1/K1_LONG 加 1，超过 max 回到 min
K2/K2_LONG 减 1，小于 min 回到 max
改动后调用 control_apply_config()
```

函数内部使用 `int16 value`，不要把 `int16 *` 强转给现有 `int *` 函数。

- [ ] **Step 4: 增加 ELEM 页绘制和处理**

新增：

```c
static void Menu_Draw_Element(int edit_line);
static void Menu_Element_Process(void);
```

`Menu_Draw_Element()` 显示：

```text
E1 app.start.element_seq[0]
E2 app.start.element_seq[1]
E3 app.start.element_seq[2]
E4 app.start.element_seq[3]
E5 app.start.element_seq[4]
E6 app.start.element_seq[5]
```

每行值用 `ips114_show_int32(112, row, app.start.element_seq[i], 3)`。

所有新增函数补中文 Doxygen 函数头，并遵守 C89 声明在代码块开头。

`Menu_Element_Process()`：

```text
case 160: 导航 6 行
case 1601~1606: 用 Menu_Process_Int16_Bounded_Value(&app.start.element_seq[i], 0, TRACK_ELEMENT_SEESAW)
```

- [ ] **Step 5: 接入渲染和按键入口**

在 `Menu_Get_Page_Row_Max()`：

```c
case 1:
    return 6 * MENU_ROW_HEIGHT;
case 7:
    return 6 * MENU_ROW_HEIGHT;
```

同时修改 `Menu_Get_Page_Root()`，让 `160` 和 `1601~1606` 返回一个独立 root，例如 `7`，避免它们被现有逻辑折叠成 START root `1`：

```c
if (page_id == 160 || (page_id >= 1601 && page_id <= 1606))
{
    return 7;
}
```

`16` 仍归属于 START root，用于从 START 返回。

在 `Menu_Render_Current_Page()` 增加：

```text
case 16: 绘制 LEN 编辑页
case 160: 绘制 E1~E6 根页
case 1601~1606: 绘制对应槽位编辑页
```

在 `Keystroke_Menu()` 增加：

```text
display_codename == 16 时调用 Menu_Element_Len_Process()
display_codename == 160 或 1601~1606 时调用 Menu_Element_Process()
```

删除或停用不再使用的 `Menu_Process_Track_Mode()` 静态声明和实现，避免死代码。

- [ ] **Step 6: 菜单 smoke check 清单**

实机或代码走读确认：

```text
首页仍只有 1~6 行，不越界。
START 第 6 行显示 elem_len，并可进入 ELEM 子页。
ELEM_LEN 页可用 K1/K2 修改 LEN，K3 进入 E1~E6。
ELEM 页 E1~E6 可进入、可返回。
elem_len 只在 1~6 循环。
E1~E6 只在 0~5 循环。
```

- [ ] **Step 7: 单文件编译**

运行：

```powershell
& 'D:\keil_5\C251\BIN\C251.EXE' '..\service\menu.c' 'LARGE' 'NOALIAS' 'WARNINGLEVEL(3)' 'OPTIMIZE(0,SIZE)' 'BROWSE' 'INCDIR(..\..\libraries\zf_common;..\..\libraries\zf_components;..\..\libraries\zf_device;..\..\libraries\zf_driver;..\user;..\service;..\code)' 'DEBUG' 'PRINT(.\out_file\menu.lst)' 'OBJECT(.\out_file\menu.obj)'
```

Expected: `0 WARNING(S), 0 ERROR(S)`.

- [ ] **Step 8: Commit chunk 3**

```powershell
git add project/service/menu.c
git commit -m "增加元素顺序菜单"
```

---

## Chunk 4: 文档、全工程验证和现场用例

### Task 6: 更新项目文档

**Files:**
- Modify: `docs/03-模块说明/a_run_track_element.md`
- Modify: `docs/06-参考资料/EEPROM参数表.md`
- Modify: `docs/04-调参与策略/参数与调参指南.md`

- [ ] **Step 1: 更新元素仲裁文档**

把固定顺序说明改为：

```text
默认序列为 1 -> 3 -> 5 -> 4。
实际顺序由 element_len 和 element_seq[0..5] 决定。
```

补充编号表和右环跳过规则。

同时写清：

```text
fly_ramp_enable=0 时跳过跷跷板。
全空/全非法/当前不可执行元素序列会在 RAM 中回退默认序列。
菜单运行中修改序列不打断当前元素，当前元素完成后生效。
circle_flags 关闭时，元素运行状态复位到序列中的第一个有效元素。
```

- [ ] **Step 2: 更新 EEPROM 参数表**

新增：

```text
30 element_len
31 element_seq[0]
32 element_seq[1]
33 element_seq[2]
34 element_seq[3]
35 element_seq[4]
36 element_seq[5]
```

- [ ] **Step 3: 更新调参指南**

增加现场示例：

```text
默认：LEN=4, E1=1, E2=3, E3=5, E4=4
无跷跷板：LEN=3, E1=1, E2=3, E3=4
右环预留：填 2 会跳过
运行中改序列：当前元素完成后生效
异常兜底：全 0、全 2、非法长度会回到默认序列
```

### Task 7: 完整验证

**Files:**
- Verify build output only.

- [ ] **Step 1: 全工程 Keil 构建**

运行：

```powershell
& 'D:\keil_5\UV4\UV4.exe' -b 'project\mdk\seekfree.uvproj'
```

Expected: 命令退出后读取 `project/mdk/out_file/SEEKFREE.build_log.htm`。

- [ ] **Step 2: 检查构建日志**

运行：

```powershell
Get-Content -Path 'project\mdk\out_file\SEEKFREE.build_log.htm' | Select-String -Pattern 'Error\(s\)|Warning\(s\)|compiling|linking'
```

工作目录：仓库根目录 `C:\Users\ye\Desktop\wushua - 双串`。

Expected: `0 Error(s), 0 Warning(s)`。

- [ ] **Step 3: 菜单和序列现场检查**

在车上检查：

```text
START 第 6 行显示 elem_len，可在 1~6 循环。
首页能进入 ELEM 页。
ELEM 页 E1~E6 每项只在 0~5 循环。
默认 X 顺序为 1 -> 3 -> 5 -> 4 -> 1。
LEN=3, E1=1, E2=3, E3=4 时，X 顺序为 1 -> 3 -> 4 -> 1。
序列含 2 时，右圆环跳过，不停车不卡住。
fly_ramp_enable=0 且序列含 5 时，跷跷板跳过。
运行中把 elem_len 缩短后，当前元素完成后不越界。
菜单修改 element_len / E1~E6 后保存 EEPROM，复位或重新上电仍按新序列运行。
构造非法 len，例如 0 或 7，上电 RAM 回退默认序列。
构造全 0 序列，上电 RAM 回退默认序列。
构造全 2 序列，右环全不可执行，上电 RAM 回退默认序列。
构造 element_seq[] 含 6、99 或负值，确认 RAM 中对应槽位清为 0，且不会立即重写 EEPROM。
fly_ramp_enable=0 且有效范围内只有 5 时，上电 RAM 回退默认序列。
确认 RAM 回退默认序列不会立刻写 EEPROM；只有用户在菜单保存后才持久化。
```

- [ ] **Step 4: Commit chunk 4**

```powershell
git add docs/03-模块说明/a_run_track_element.md docs/06-参考资料/EEPROM参数表.md docs/04-调参与策略/参数与调参指南.md
git commit -m "更新元素顺序配置文档"
```

### Task 8: 最终整理

**Files:**
- Verify git status and build log.

- [ ] **Step 1: 查看工作区**

```powershell
git status --short
```

Expected: 只允许存在用户明确保留的本地文件，例如 `project/mdk/seekfree.uvgui.ye`。

若 Keil 构建更新了工程 UI 或输出文件，不要把无关构建产物混进文档提交；只提交本计划列出的源码和文档文件。

- [ ] **Step 2: 汇总提交**

```powershell
git log --oneline -4
```

Expected: 能看到本计划中的 3~4 个中文提交。

- [ ] **Step 3: 向用户汇报**

说明：

```text
改了哪些文件
默认序列是什么
菜单怎么填
右环/跷跷板关闭时怎么处理
编译结果
仍需现场验证的项目
```

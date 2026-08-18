# 跷跷板停止等待模式实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为跷跷板新增"停止等待"模式，与现有"飞坡"模式并存，通过菜单切换

**Architecture:** 在 `a_run_fly.c/h` 中新增独立的停止等待状态机，复用飞坡的入口检测和 COOLDOWN 逻辑，通过 `app.fly.seesaw_mode` 参数切换模式

**Tech Stack:** C89, Keil5 C251, STC AI8051U

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `project/service/eeprom.h` | 定义 `seesaw_mode` 参数 |
| `project/service/eeprom.c` | 添加默认值、EEPROM 读写逻辑 |
| `project/service/menu.c` | 添加菜单项 |
| `project/user/a_run_fly.h` | 定义 `SeesawState` 枚举和函数声明 |
| `project/user/a_run_fly.c` | 实现停止等待状态机 |
| `project/user/a_run_track_element.c` | 集成模式切换逻辑 |

---

### Task 1: EEPROM 参数定义

**Files:**
- Modify: `project/service/eeprom.h`
- Modify: `project/service/eeprom.c`

- [ ] **Step 1: 在 AppFlyConfig 中新增 seesaw_mode 参数**

在 `project/service/eeprom.h` 中找到 `AppFlyConfig` 结构体，在 `count_fly_time_2` 后面添加：
```c
int seesaw_mode;  /**< 跷跷板模式：0=飞坡，1=停止等待 */
```

- [ ] **Step 2: 设置默认值**

在 `project/service/eeprom.c` 中找到 `eeprom_load_defaults()` 函数，在 fly 参数区域添加：
```c
app.fly.seesaw_mode = 0;  /* 默认飞坡模式 */
```

- [ ] **Step 3: 添加 EEPROM 读取逻辑**

在 `project/service/eeprom.c` 中找到 `eeprom_read_config()` 函数，在 fly 参数读取区域添加：
```c
app.fly.seesaw_mode = read_int(25);  /* 槽位 25：跷跷板模式 */
```

- [ ] **Step 4: 添加 EEPROM 写入逻辑**

在 `project/service/eeprom.c` 中找到 `eeprom_write_config()` 函数，在 fly 参数写入区域添加：
```c
save_int(25, app.fly.seesaw_mode);  /* 槽位 25：跷跷板模式 */
```

- [ ] **Step 5: 编译验证**

运行 Keil5 编译，检查是否有语法错误

---

### Task 2: 菜单配置

**Files:**
- Modify: `project/service/menu.c`

- [ ] **Step 1: 在 Menu_Fly_Process 中新增菜单项**

在 `project/service/menu.c` 中找到 `Menu_Fly_Process()` 函数，在现有 fly 参数菜单项后面添加：

```c
/* 跷跷板模式选择 */
lcd_show_string(0, row, "seesaw_mode:");
lcd_show_int(100, row, app.fly.seesaw_mode, 1);
if (cursor_row == row) {
    Menu_Process_Special_Value(&app.fly.seesaw_mode);
}
row++;
```

注意：`row` 变量需要根据现有菜单项的行号递增

- [ ] **Step 2: 编译验证**

运行 Keil5 编译，检查是否有语法错误

---

### Task 3: 状态机头文件

**Files:**
- Modify: `project/user/a_run_fly.h`

- [ ] **Step 1: 新增 SeesawState 枚举**

在 `project/user/a_run_fly.h` 中，在 `FlyState` 枚举后面添加：

```c
/**
 * @brief 跷跷板停止等待控制阶段。
 * @details seesaw_state 使用该枚举值保存当前阶段。
 */
typedef enum {
    SEESAW_IDLE = 0,      /**< 等待入口检测 */
    SEESAW_STOP = 1,      /**< 停车，目标速度为 0 */
    SEESAW_WAIT = 2,      /**< 等待跷跷板倾斜（1 秒） */
    SEESAW_CHECK = 3,     /**< 检查电感信号恢复 */
    SEESAW_RECOVER = 4,   /**< 阶梯增速恢复 */
    SEESAW_COOLDOWN = 5   /**< 复用飞坡 COOLDOWN */
} SeesawState;
```

- [ ] **Step 2: 新增函数声明**

在 `project/user/a_run_fly.h` 中，在现有函数声明后面添加：

```c
/**
 * @brief 更新跷跷板停止等待模式速度状态机。
 *
 * 检测到跷跷板后停车等待 1 秒，利用重力让跷跷板倾斜，
 * 电感信号恢复后出发，阶梯增速恢复到巡线速度。
 *
 * @param speed 输出目标速度指针。
 * @param allow_entry 1-当前期望元素为跷跷板，允许从空闲态检测入口；0-禁止新入口。
 */
void a_run_seesaw_update_speed(float *speed, uint8 allow_entry);

/**
 * @brief 复位跷跷板停止等待状态机。
 */
void a_run_seesaw_reset(void);
```

- [ ] **Step 3: 编译验证**

运行 Keil5 编译，检查是否有语法错误

---

### Task 4: 状态机实现

**Files:**
- Modify: `project/user/a_run_fly.c`

- [ ] **Step 1: 新增静态变量**

在 `project/user/a_run_fly.c` 中，在现有静态变量后面添加：

```c
/* --- 跷跷板停止等待状态内部变量 --- */
static SeesawState seesaw_state = SEESAW_IDLE; /**< 停止等待状态机阶段 */
static int seesaw_wait_count = 0;              /**< 等待倾斜计数，单位为 2ms 周期 */
```

- [ ] **Step 2: 实现 a_run_seesaw_reset 函数**

在 `project/user/a_run_fly.c` 中，在 `a_run_fly_reset()` 函数后面添加：

```c
/**
 * @brief 复位跷跷板停止等待状态机内部计数并回到普通巡线。
 *
 * 元素仲裁关闭或重新进入跷跷板阶段前调用，清掉计数和阶段。
 */
void a_run_seesaw_reset(void)
{
    seesaw_state = SEESAW_IDLE;
    seesaw_wait_count = 0;
}
```

- [ ] **Step 3: 实现 a_run_seesaw_update_speed 函数**

在 `project/user/a_run_fly.c` 中，在 `a_run_seesaw_reset()` 函数后面添加：

```c
/**
 * @brief 跷跷板停止等待模式速度状态机。
 *
 * 检测到跷跷板后停车等待 1 秒，利用重力让跷跷板倾斜，
 * 电感信号恢复后出发，阶梯增速恢复到巡线速度。
 *
 * @param speed 输出目标速度指针。
 * @param allow_entry 1-当前期望元素为跷跷板，允许从空闲态检测入口；0-禁止新入口。
 */
void a_run_seesaw_update_speed(float *speed, uint8 allow_entry)
{
    switch (seesaw_state)
    {
    case SEESAW_IDLE:
        /* 入口检测逻辑与飞坡模式相同 */
        if (allow_entry != 0 &&
            ad1 < FLY_AD_SIDE_LOST_TH &&
            ad2 < FLY_AD_CENTER_LOST_TH &&
            ad3 < FLY_AD_CENTER_LOST_TH &&
            ad4 < FLY_AD_SIDE_LOST_TH)
        {
            seesaw_state = SEESAW_STOP;
        }
        break;

    case SEESAW_STOP:
        /* 停车，目标速度为 0 */
        *speed = 0.0f;
        fly_lost_line_blocked = 1;
        seesaw_wait_count = 0;
        seesaw_state = SEESAW_WAIT;
        break;

    case SEESAW_WAIT:
        /* 等待 1 秒（500 * 2ms = 1000ms） */
        *speed = 0.0f;
        seesaw_wait_count++;
        if (seesaw_wait_count >= 500)
        {
            seesaw_state = SEESAW_CHECK;
        }
        break;

    case SEESAW_CHECK:
        /* 检查电感信号恢复，与飞坡 LANDING 阈值相同 */
        *speed = 0.0f;
        if (ad1 > FLY_LANDING_SIDE_TH &&
            ad4 > FLY_LANDING_SIDE_TH &&
            ad2 > FLY_LANDING_CENTER_TH &&
            ad3 > FLY_LANDING_CENTER_TH)
        {
            seesaw_state = SEESAW_RECOVER;
        }
        break;

    case SEESAW_RECOVER:
        /* 阶梯增速恢复，复用 COOLDOWN 逻辑 */
        fly_release_speed = (float)FLY_RECOVER_SEARCH_SPEED;
        fly_finish_event = 1;
        seesaw_state = SEESAW_COOLDOWN;
        break;

    case SEESAW_COOLDOWN:
        /* 释放阶段由 a_run_fly_update_release_speed() 执行 */
        break;

    default:
        a_run_seesaw_reset();
        break;
    }
}
```

- [ ] **Step 4: 编译验证**

运行 Keil5 编译，检查是否有语法错误

---

### Task 5: 元素仲裁集成

**Files:**
- Modify: `project/user/a_run_track_element.c`

- [ ] **Step 1: 修改 ELEMENT_SEESAW case**

在 `project/user/a_run_track_element.c` 中找到 `a_run_track_element_update_gate()` 函数中的 `ELEMENT_SEESAW` case，修改为：

```c
case ELEMENT_SEESAW:
    if (app.fly.seesaw_mode == 0)
    {
        /* 飞坡模式 */
        a_run_fly_update_speed(speed, 1);
    }
    else
    {
        /* 停止等待模式 */
        a_run_seesaw_update_speed(speed, 1);
    }
    if (a_run_fly_take_finish_event() != 0)
    {
        track_element_enter_from_index((uint8)(element_index + 1));
    }
    break;
```

- [ ] **Step 2: 修改 track_element_enter 函数**

在 `project/user/a_run_track_element.c` 中找到 `track_element_enter()` 函数，修改跷跷板复位逻辑：

```c
/*
 * 跷跷板完成事件只推进元素序列，COOLDOWN 还要继续释放速度。
 * 切到任意后续元素时都不能清掉 fly_release_speed，否则会一拍回到巡线速度。
 */
if (element == ELEMENT_NONE || flat_fly != FLY_STATE_COOLDOWN)
{
    if (app.fly.seesaw_mode == 0)
    {
        a_run_fly_reset();
    }
    else
    {
        a_run_seesaw_reset();
    }
}
```

- [ ] **Step 3: 编译验证**

运行 Keil5 编译，检查是否有语法错误

---

### Task 6: 整体编译验证

**Files:**
- 无新增/修改文件

- [ ] **Step 1: 清理编译**

在 Keil5 中执行 Clean Build，确保无错误和警告

- [ ] **Step 2: 检查编译输出**

检查 `project/mdk/out_file/SEEKFREE.build_log.htm`，确认：
- Error(s): 0
- Warning(s): 0（或仅有已知无害警告）

---

### Task 7: 功能测试

**Files:**
- 无新增/修改文件

- [ ] **Step 1: 菜单切换测试**

1. 进入菜单的 Fly 页面
2. 找到 seesaw_mode 菜单项
3. 按 K1/K2 切换 0/1
4. 确认显示正确

- [ ] **Step 2: EEPROM 存储测试**

1. 设置 seesaw_mode = 1
2. 在菜单首页按 K4 保存
3. 重启系统
4. 进入菜单确认 seesaw_mode 仍为 1

- [ ] **Step 3: 飞坡模式测试**

1. 设置 seesaw_mode = 0
2. 在包含跷跷板的赛道上运行
3. 确认行为与原来相同（检测弱磁后继续前进）

- [ ] **Step 4: 停止等待模式测试**

1. 设置 seesaw_mode = 1
2. 在包含跷跷板的赛道上运行
3. 确认行为：
   - 检测到弱磁后停车
   - 等待约 1 秒
   - 电感信号恢复后出发
   - 阶梯增速恢复到巡线速度

- [ ] **Step 5: 元素序列测试**

1. 配置元素序列包含跷跷板（如：1,3,5,4）
2. 运行完整序列
3. 确认跷跷板完成后正确推进到下一个元素

---

## 验证清单

- [ ] 编译通过，无错误
- [ ] 菜单切换正常
- [ ] EEPROM 存储正常
- [ ] 飞坡模式行为不变
- [ ] 停止等待模式行为正确
- [ ] 元素序列推进正常

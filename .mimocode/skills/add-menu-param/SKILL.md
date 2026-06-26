---
name: add-menu-param
description: 添加菜单可调参数 — 从 AppConfig 到 EEPROM 到菜单的完整流程
---

# 添加菜单可调参数

智能车项目中将硬编码参数改为菜单可调的标准流程。适用于 STC AI8051U 项目。

## 流程概览

```
AppConfig 结构体 → EEPROM 默认值/读写 → 菜单子页面显示/编辑 → 编译验证
```

## 详细步骤

### 1. 修改 AppConfig 结构体（eeprom.h）

在对应的 Config 结构体中新增字段：

```c
typedef struct {
    float encoder_target;
    int   ad_both_high_threshold;  // 用 int，不用 uint16
    float adc_a_1;
    float kp_Err;
    float kd_Err;
    // ... 新增参数
} AppXxxConfig;
```

**类型选择规则：**
- `float`：浮点参数（PID 增益、阈值比例等）
- `int`：整数参数（阈值、计数、速度等）— **不要用 uint16**，因为 `Menu_Process_Int_Value` 签名是 `(int*, int)`

### 2. 设置默认值（eeprom.c）

在 `eeprom_default()` 中设置默认值：

```c
app.xxx.new_param = DEFAULT_VALUE;
```

### 3. 分配 EEPROM 槽位（eeprom.c）

从当前最高槽位+1 开始连续分配。当前槽位布局（2026-06-23）：

| 范围 | 用途 |
|------|------|
| 0 | eeprom_init_time 标志 |
| 1-12 | start 参数 |
| 13-18 | speed 参数 |
| 19-25 | angle + fly 早期参数 |
| 26-31 | ring 参数 |
| 37-40 | fly 后期参数 |
| 40-44 | cylinder 参数 |
| 45-47 | wall 参数 |
| 48-49 | cylinder kp/kd |
| 51 | ring drive_out_ring |
| 52-55 | fly 扩展参数 |
| 56 | cross encoder_target |

读写函数中添加对应槽位：
```c
config->xxx.new_param = read_float(SLOT_N);  // 或 read_int
// ...
write_float(SLOT_N, config->xxx.new_param);  // 或 write_int
```

**注意：** `date_buff[250]` 容纳槽位 0-62，槽位 63+ 会越界。

### 4. 添加菜单子页面（menu.c）

在对应子页面的 draw 和 process 函数中添加行：

```c
// Draw 函数：显示参数值
case NEW_ROW_N:
    sprintf(buf, "param:%6.2f", app.xxx.new_param);
    // 或整数：sprintf(buf, "param:%d", app.xxx.new_param);
    break;

// Process 函数：编辑参数
case NEW_ROW_N:
    Menu_Process_Float_Value(&app.xxx.new_param, step, min, max);
    // 或整数：Menu_Process_Int_Value(&app.xxx.new_param, step);
    break;
```

更新 `Menu_Get_Page_Row_Max()` 和 `menu_have_sub[]` 数组。

### 5. 编译验证

运行 keil-build-verify skill 的编译流程。

## 常见陷阱

- **C98 warning**：传 `uint16*` 给 `Menu_Process_Int_Value(int*, int)` → 改用 `int`
- **C173 warning**：`int` 与 `uint16` 比较 → 添加 `(uint16)` cast
- **EEPROM 越界**：新增槽位超过 `date_buff` 大小 → 检查最高槽位
- **首次上电**：新槽位读到 `0xFFFFFFFF`（float 为 NaN）→ 在 read 后加边界校验
- **参数类型临时切换**：圆桶/墙面等元素期间需临时覆盖 PID 参数 → 用"局部变量覆盖"模式，不修改全局 app 结构体

## 已有子页面

| 页面 | 编号 | 行范围 | 元素 |
|------|------|--------|------|
| RING | 41 | 411-417 | 圆环 |
| CYLINDER | 42 | 421-427 | 圆桶 |
| WALL | 43 | 431-433 | 墙面 |
| FLY | 44 | 441-447 | 飞坡/跷跷板 |
| CROSS | 45 | 451 | 双十字 |

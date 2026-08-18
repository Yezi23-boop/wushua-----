# 跷跷板停止等待模式设计文档

## 背景

当前跷跷板只有"飞坡"模式：检测到弱磁后继续前进飞过跷跷板。这种方式在简单赛道上节省时间，但在复杂赛道上存在不确定性和适应性问题。

需要新增"停止等待"模式：检测到跷跷板后停车等待，利用重力让跷跷板倾斜，然后再出发。两种模式通过菜单切换，用户可根据赛道复杂度选择。

## 功能目标

1. 新增"停止等待"模式的跷跷板处理方式
2. 保留现有"飞坡"模式
3. 通过菜单切换模式，参数存储到 EEPROM
4. 两种模式复用同一入口检测逻辑

## 设计方案

### 1. 新增参数

**文件：`project/service/eeprom.h`**

在 `AppFlyConfig` 结构体中新增：
```c
int seesaw_mode;  // 0=飞坡模式，1=停止等待模式
```

**文件：`project/service/eeprom.c`**

- 默认值：`seesaw_mode = 0`（飞坡模式）
- EEPROM 槽位：使用空闲槽位（当前 fly 参数在 22-24，可用 25 或 26）
- 在 `eeprom_load_defaults()` 中设置默认值
- 在 `eeprom_read_config()` 和 `eeprom_write_config()` 中添加读写逻辑

### 2. 菜单配置

**文件：`project/service/menu.c`**

在 `Menu_Fly_Process()` 中新增菜单项：
- 显示 "seesaw_mode" 和当前值
- 使用 `Menu_Process_Special_Value(&app.fly.seesaw_mode)` 切换 0/1
- 0 = 飞坡模式，1 = 停止等待模式

### 3. 状态机设计

**文件：`project/user/a_run_fly.h`**

新增停止等待状态枚举：
```c
typedef enum {
    SEESAW_IDLE = 0,      // 等待入口检测
    SEESAW_STOP = 1,      // 停车
    SEESAW_WAIT = 2,      // 等待倾斜（1秒）
    SEESAW_CHECK = 3,     // 检查电感信号恢复
    SEESAW_RECOVER = 4,   // 阶梯增速恢复
    SEESAW_COOLDOWN = 5   // 复用飞坡 COOLDOWN
} SeesawState;
```

新增函数声明：
```c
void a_run_seesaw_update_speed(float *speed, uint8 allow_entry);
void a_run_seesaw_reset(void);
```

**文件：`project/user/a_run_fly.c`**

新增静态变量：
```c
static SeesawState seesaw_state = SEESAW_IDLE;
static int seesaw_wait_count = 0;  // 等待计数（单位 2ms）
```

新增函数实现：
```c
void a_run_seesaw_update_speed(float *speed, uint8 allow_entry) {
    switch (seesaw_state) {
        case SEESAW_IDLE:
            // 入口检测（与飞坡相同）
            if (allow_entry && ad1 < 14 && ad2 < 5 && ad3 < 5 && ad4 < 14) {
                seesaw_state = SEESAW_STOP;
            }
            break;
            
        case SEESAW_STOP:
            // 停车
            *speed = 0;
            fly_lost_line_blocked = 1;
            seesaw_state = SEESAW_WAIT;
            seesaw_wait_count = 0;
            break;
            
        case SEESAW_WAIT:
            // 等待1秒（500 * 2ms = 1000ms）
            *speed = 0;
            seesaw_wait_count++;
            if (seesaw_wait_count >= 500) {
                seesaw_state = SEESAW_CHECK;
            }
            break;
            
        case SEESAW_CHECK:
            // 检查电感信号恢复
            if (ad1 > 15 && ad4 > 15 && ad2 > 10 && ad3 > 10) {
                seesaw_state = SEESAW_RECOVER;
            }
            break;
            
        case SEESAW_RECOVER:
            // 阶梯增速恢复
            seesaw_state = SEESAW_COOLDOWN;
            break;
            
        case SEESAW_COOLDOWN:
            // 复用 a_run_fly_update_release_speed()
            break;
    }
}

void a_run_seesaw_reset(void) {
    seesaw_state = SEESAW_IDLE;
    seesaw_wait_count = 0;
}
```

### 4. 元素仲裁集成

**文件：`project/user/a_run_track_element.c`**

修改 `ELEMENT_SEESAW` case：
```c
case ELEMENT_SEESAW:
    if (app.fly.seesaw_mode == 0) {
        // 飞坡模式
        a_run_fly_update_speed(speed, 1);
    } else {
        // 停止等待模式
        a_run_seesaw_update_speed(speed, 1);
    }
    if (a_run_fly_take_finish_event() != 0) {
        track_element_enter_from_index((uint8)(element_index + 1));
    }
    break;
```

### 5. 状态复位

在 `track_element_enter()` 中，根据 `seesaw_mode` 调用不同的复位函数：
```c
if (app.fly.seesaw_mode == 0) {
    a_run_fly_reset();
} else {
    a_run_seesaw_reset();
}
```

## 状态机流程

```
IDLE
  ↓ (检测到四路电感弱磁)
STOP
  ↓ (目标速度=0，屏蔽丢线保护)
WAIT
  ↓ (等待1秒，500 * 2ms)
CHECK
  ↓ (电感信号恢复：ad1/ad4 > 15，ad2/ad3 > 10)
RECOVER
  ↓ (阶梯增速恢复)
COOLDOWN
  ↓ (复用 a_run_fly_update_release_speed())
IDLE
```

## 关键参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 入口阈值 | ad1/ad4 < 14，ad2/ad3 < 5 | 与飞坡模式相同 |
| 等待时间 | 1秒（500 * 2ms） | 固定值，不可配置 |
| 倾斜确认阈值 | ad1/ad4 > 15，ad2/ad3 > 10 | 与飞坡 LANDING 阈值相同 |
| seesaw_mode | 0=飞坡，1=停止等待 | 菜单可切换，EEPROM 存储 |

## 复用逻辑

- **完成事件**：触发 `fly_finish_event` 推进元素序列
- **COOLDOWN 阶段**：复用 `a_run_fly_update_release_speed()`
- **丢线保护**：使用 `fly_lost_line_blocked` 屏蔽丢线保护

## 文件修改清单

| 文件 | 修改内容 |
|------|----------|
| `project/service/eeprom.h` | 在 AppFlyConfig 中新增 seesaw_mode |
| `project/service/eeprom.c` | 添加 seesaw_mode 的默认值、读写逻辑 |
| `project/service/menu.c` | 在 Menu_Fly_Process 中新增菜单项 |
| `project/user/a_run_fly.h` | 新增 SeesawState 枚举和函数声明 |
| `project/user/a_run_fly.c` | 新增 seesaw 状态机实现 |
| `project/user/a_run_track_element.c` | 集成 seesaw_mode 判断逻辑 |

## 验证方法

1. **菜单切换**：在 Menu_Fly_Process 中切换 seesaw_mode，确认显示正确
2. **EEPROM 存储**：保存后重启，确认 seesaw_mode 保持
3. **飞坡模式**：seesaw_mode=0 时，跷跷板行为与原来相同
4. **停止等待模式**：seesaw_mode=1 时：
   - 检测到弱磁后停车
   - 等待1秒
   - 电感信号恢复后出发
   - 阶梯增速恢复到巡线速度
5. **元素序列**：完成后正确推进到下一个元素

## 风险与注意事项

1. **等待时间固定**：1秒是经验值，可能需要根据实际跷跷板调整
2. **倾斜确认**：依赖电感信号恢复，如果跷跷板倾斜不到位可能卡在 CHECK 状态
3. **丢线保护**：在 WAIT 阶段屏蔽丢线保护，如果真实丢线会被误屏蔽
4. **状态复位**：切换模式时需要确保状态机正确复位

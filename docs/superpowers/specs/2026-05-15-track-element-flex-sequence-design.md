# 赛道元素可配置顺序设计

## 背景

当前 `project/user/a_run_track_element.c` 中的元素仲裁顺序是硬编码的：

```text
左圆环 -> 圆桶 -> 跷跷板 -> 墙面 -> 左圆环
```

这套顺序适合当前调试路线，但比赛现场如果元素顺序变化，需要改代码并重新编译。历史文档里已经预留过 `track_mode`，但它只适合少量固定模式，不能手动填完整元素顺序。

## 目标

- 保留代码默认顺序，保证上电无配置时仍可按当前路线运行。
- 支持通过菜单和 EEPROM 手动填写元素顺序，方便比赛现场调整。
- 保持 5ms 主控制链路轻量，不做动态分配、不做复杂搜索。
- 保留现有 `expected_element` 调试显示语义。
- 右圆环先只预留编号，本轮不实现右环状态机。

## 元素编号

```text
0 = 空槽/跳过
1 = 左圆环
2 = 右圆环，当前只预留，运行时跳过
3 = 圆桶/圆筒
4 = 墙面
5 = 跷跷板/飞坡
```

## 配置结构

在 `AppStartConfig` 中新增固定长度元素序列：

```c
#define TRACK_ELEMENT_SEQUENCE_MAX 6

int16 element_len;
int16 element_seq[TRACK_ELEMENT_SEQUENCE_MAX];
```

默认配置：

```text
element_len = 4
element_seq = {1, 3, 5, 4, 0, 0}
```

对应路线：

```text
左圆环 -> 圆桶 -> 跷跷板 -> 墙面 -> 循环
```

`element_len` 的有效范围为 `1~6`。`element_seq[i]` 允许 `0~5`，非法值运行时跳过。

默认序列只定义一份常量表，供 EEPROM 默认值、读入校验失败回退和运行时兜底共同使用，避免多处手写不一致。

## 配置校验

新增一个轻量校验函数，建议放在 `int_user.c` 的配置归一化流程中，并在 EEPROM 读取后执行：

```text
1. element_len < 1 或 > 6 时，恢复默认 len+seq。
2. element_seq[i] 小于 0 或大于 5 时，只在 RAM 中改为 0。
3. 在 element_len 范围内扫描最多 6 次，如果没有任何可执行元素，则恢复默认 len+seq。
```

“可执行元素”定义为：

```text
1 = 左圆环，可执行
2 = 右圆环，当前不可执行
3 = 圆桶，可执行
4 = 墙面，可执行
5 = 跷跷板，仅 fly_ramp_enable == 1 时可执行
0 或非法值 = 不可执行
```

EEPROM 旧数据兼容规则：旧车已经初始化过 EEPROM 时，新槽位可能是随机残留。只要新槽位读入后不能通过上述“至少一个可执行元素”的校验，就只在 RAM 中恢复默认序列；不立即刷写 EEPROM，避免上电阶段额外 Flash 写入。用户后续在菜单中保存时再写入新序列。

## 运行时仲裁

新增运行时下标：

```c
static uint8 element_index;
```

`expected_element` 保留，用于显示和对外读取，但它由序列表推导：

```text
expected_element = element_seq[element_index]
```

当前元素完成后统一调用“前进到下一个有效元素”的内部流程：

```text
element_index++
如果 element_index >= element_len，则回到 0
跳过 0、非法元素、未实现右圆环
若元素为跷跷板但 fly_ramp_enable == 0，也跳过
```

该流程必须最多扫描 `TRACK_ELEMENT_SEQUENCE_MAX` 次，不能使用无界循环。如果 6 次都找不到可执行元素，运行时使用默认序列的第一个可执行元素，并把 `element_index` 置 0。

菜单运行中修改序列时不直接改 `expected_element`，当前元素继续跑到完成；完成后再按新的 `element_len` 和 `element_seq[]` 前进。若完成时 `element_index >= element_len`，先把 `element_index` 归零再找下一个可执行元素。

进入不同元素时执行对应初始化：

```text
左圆环：完整调用 ring_reset_state()，清除 ring_finish_event、diff_set、里程和角度累计，再开放入口
圆桶：cylinder_start_wait_top()
墙面：wall_start_wait_signal()
跷跷板：a_run_fly_reset()
右圆环：本轮跳过
```

元素切换统一走一个内部入口函数，负责清理非当前元素的残留完成事件和临时状态：

```text
进入左圆环：清理圆桶、墙面、跷跷板残留，重置左环
进入圆桶：消费/清除左环完成事件，重置墙面和跷跷板，启动圆桶
进入墙面：清理圆桶计数和跷跷板事件，启动墙面
进入跷跷板：清理圆桶和墙面状态，复位飞坡状态机
```

这样避免新序列下上一个元素的完成事件在下一拍被重复消费。

每个元素自己的完成条件保持不变：

```text
左圆环：ring_finish_event
圆桶：cylinder_update_5ms() 返回完成
墙面：wall_update_5ms() 返回完成
跷跷板：a_run_fly_take_finish_event()
```

## 菜单

菜单分两处显示，避免单页 7 行挤出 IPS114 显示范围：

1. `START` 页第 6 行从 `trk_mode` 改为 `elem_len`，用于设置有效长度 `1~6`。
2. 新增 `ELEM` 页，只显示 6 个槽位：

```text
E1  1
E2  3
E3  5
E4  4
E5  0
E6  0
```

菜单结构需要明确修改：

```text
MENU_PAGE_COUNT 从 7 改为 8
首页增加 ELEM 入口
menu_have_sub 增加 7, 71, 72, 73, 74, 75, 76
Menu_Get_Page_Row_Max() 为 ELEM root 返回 6 * MENU_ROW_HEIGHT
Menu_Render_Current_Page() 增加 ELEM 页绘制分支
Keystroke_Menu() 增加 ELEM 页处理分支
```

首页增加第 7 行时需要实机确认文字不越界；如果屏幕底部显示拥挤，优先缩短右侧调试文字或把 `ELEM` 放到原 `SENSOR` 入口之后，不改变数据页 6 行限制。

`elem_len` 和 `E1~E6` 都使用新的有界 `int16` 编辑流程：

```text
elem_len: 1~6
E1~E6:   0~5
```

不要直接复用当前处理 `int *` 的通用编辑函数，避免类型不匹配，也避免现场把元素编号调到非法大值。

## EEPROM

EEPROM 继续使用当前 4 字节槽位模型。新增槽位建议从现有最高索引后继续分配：

```text
30: element_len
31: element_seq[0]
32: element_seq[1]
33: element_seq[2]
34: element_seq[3]
35: element_seq[4]
36: element_seq[5]
```

这样不改变已有参数位置，避免破坏旧参数读取。

## 安全策略

- 关闭 `circle_flags` 时，元素序列状态复位到第一个有效元素。
- 右圆环编号 `2` 可填写但当前跳过，不会卡住也不会误跑左环。
- 跷跷板编号 `5` 在 `fly_ramp_enable == 0` 时跳过。
- 如果用户把所有槽位都填成 `0` 或非法值，配置校验恢复完整默认序列；运行时兜底也使用默认序列的第一个可执行元素。
- 菜单只改配置值，不直接改 `expected_element`，避免高频状态机被异步打断。

## 测试计划

1. 单文件 C251 编译：
   - `a_run_track_element.c`
   - `eeprom.c`
   - `menu.c`
   - `int_user.c`
2. 执行完整 Keil 构建并检查 `0 Error(s), 0 Warning(s)`。
3. 默认 EEPROM 清空或首次上电时确认 `X` 按 `1 -> 3 -> 5 -> 4 -> 1` 循环。
4. 菜单改为 `LEN=3, E1=1, E2=3, E3=4`，确认 `X` 按 `1 -> 3 -> 4 -> 1` 循环。
5. 菜单填入 `2`，确认右圆环被跳过，不停车不卡住。
6. 关闭 `fly_ramp_enable` 后，序列含 `5` 时确认跷跷板被跳过。

## 非目标

- 本设计不实现右圆环内部状态机。
- 本设计不改变左圆环、圆桶、墙面、跷跷板各自的识别阈值和完成条件。
- 本设计不引入串口命令改序列；现场主要通过菜单和 EEPROM 修改。

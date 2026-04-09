# Nonblocking Menu With 10ms Key Scan Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将菜单系统改造成“10ms 中断扫键、主循环推进状态、按需局部刷新”的非阻塞 UI 服务，避免菜单占住主循环，同时保留实时页面观测能力，并让新增普通菜单项尽量只改描述表而不是到处补 `switch-case`。

**Architecture:** TM1 10ms 中断只负责产生稳定时间基准和按键事件，不直接改菜单页面状态，也不直接刷屏。主循环中的 `Keystroke_Menu()` 改成单步状态机：消费按键事件、处理页面跳转与参数修改、根据页面类型决定是否首绘整页或局部刷新，然后立刻返回。参数页与目录页采用轻量表驱动描述，位置由统一布局参数根据项索引计算，避免手写每一行坐标；`HOME`、`SENSOR` 保留少量专用动态刷新逻辑。实时页通过 10ms 节拍累计到目标周期后触发动态区局刷，参数页只在事件或回页时刷新；整页首绘只发生在进页、提示层退出或显式要求重绘时。

**Tech Stack:** STC AI8051U, Keil5 C251, C89, IPS114 显示驱动, 现有 PIT 定时器回调链

---

## File Structure

**Modify:**
- `project/service/key.h`
  - 对外声明新的 10ms 扫键入口、事件读取接口、必要的 `volatile` 事件变量
- `project/service/key.c`
  - 将按键扫描改成中断节拍驱动
  - 引入事件锁存/读取机制，避免主循环漏掉中断中的短时事件
  - 按 10ms 节拍重算长按与连发阈值
- `project/service/menu.h`
  - 暴露菜单 10ms 节拍入口与主循环服务入口
  - 声明必要的页面状态/提示状态接口
  - 声明轻量页面/菜单项/布局描述结构
- `project/service/menu.c`
  - 去掉所有阻塞 `while`
  - 去掉 `system_delay_ms(300)`
  - 引入轻量页面表、菜单项表、布局描述和通用坐标计算
  - 引入页面状态机、脏标志、页面切换全刷与局部刷新逻辑
  - 按页面类型区分实时刷新与事件刷新
- `project/user/int_user.c`
  - 注册 TM1 10ms 回调
  - 让 TM1 回调只做“扫键 + UI 时基累加”

**Keep unchanged if possible:**
- `project/user/main.c`
  - 仍然在主循环里调用 `Keystroke_Menu()`
- `project/user/isr.c`
  - 尽量不直接改 ISR 主体，复用现有 `tim1_irq_handler`

**Do not use for this feature unless blocked:**
- `project/service/soft_timer.c`
  - 本次优先不复用，避免把菜单节拍和全局软件定时器耦合在一起

## Key Decisions

- 按键扫描放到 10ms 中断节拍，解决当前长按/连发依赖主循环速度的问题。
- 中断只写“事件”和“时基”，不直接改 `display_codename`、`cursor_row`，也不调用任何 `ips114_show_*`。
- `Keystroke_Menu()` 每次调用只处理一次事件和一次必要刷新。
- 目录页与普通参数页改成轻量表驱动，新增普通项时目标是只补一个菜单项描述，而不是再加新的页面处理分支。
- `HOME`、`SENSOR` 为实时页，走定时局刷；其他参数页和目录页默认只按事件刷新。
- `HOME 50ms` 与 `SENSOR 20ms/50ms` 指的是“动态区允许刷新一次的周期”，不是整页重画周期，也不是 ISR 内的刷屏周期。
- 实时刷新由 10ms tick 累计到页面阈值后触发，例如 `HOME` 每 5 个 tick 触发一次、`SENSOR` 每 2 个 tick 触发一次。
- 每个页面都要预先定义“静态区”和“动态区”，触发刷新后只写当前页面对应的动态区，而不是运行时猜测要刷哪里。
- 菜单项显示位置不由单个项自己决定，而由页面布局统一计算：`y = first_row_y + visible_index * row_height`。
- 如果菜单项超过单页可见行数，使用 `scroll_offset` 控制可视窗口，而不是继续向屏幕外堆叠。
- 新增普通参数项时，需要一起校验：项数是否超出 `max_visible_rows`、标签长度是否侵入数值列、滚动窗口是否需要启用。
- 保存成功提示改成状态覆盖页，显示 300ms 后自动退出，不阻塞主循环。

## Recommended Timing Defaults

- 按键扫描周期：`10ms`
- 长按阈值：`300ms`
- 长按连发间隔：`80ms` 或 `100ms`
- `HOME` 动态区刷新：`50ms`
- `SENSOR` 动态区刷新：`20ms` 起步，若写屏负载偏大则切到 `50ms`
- 保存提示显示：`300ms`

## Refresh Trigger Model

- **10ms 中断职责**
  - 调用按键扫描
  - 递增菜单时基计数
  - 不刷屏，不切页，不直接改光标

- **主循环职责**
  - 读取并清除锁存按键事件
  - 消费累计 tick，换算为当前页面的经过时间
  - 当累计时间达到页面阈值时置位 `realtime_due`
  - 根据脏标志执行首绘、局刷或提示层绘制

- **推荐触发条件**
  - `page_changed`
    - 页面切换后整页首绘一次
  - `cursor_dirty`
    - 光标移动后只刷新旧光标和新光标
  - `value_dirty`
    - 参数值变化后只刷新当前参数值
  - `step_dirty`
    - 步进倍率变化后只刷新倍率显示区
  - `realtime_due`
    - 实时页到达设定周期后，只刷新动态区
  - `prompt_active/prompt_dirty`
    - 提示层显示或关闭时刷新提示层，并在退出后触发当前页重绘

## Page Region Inventory

- **HOME 页面**
  - 静态区
    - 标题 `MENU`
    - 一级菜单文字 `STRAT / PID_1 / PID_2 / PRINTF / RING / FLY`
    - 右侧标签 `Err / steer / angle / V_bat`
  - 动态区
    - `Err`
    - `PID.steer.output`
    - `PID.angle.output`
    - `dianya`
    - 光标
  - 触发策略
    - 进页整页首绘
    - 每 `50ms` 刷新一次动态数值区
    - 光标变化时单独局刷

- **SENSOR 页面**
  - 静态区
    - 标题 `<<SENSOR`
    - 顶部标签 `NORM / RAW / MAX`
    - 左侧标签 `ad1 / ad2 / ad3 / ad4 / Err`
  - 动态区
    - `ad1 ~ ad4`
    - `RAW[0] ~ RAW[3]`
    - `MA[0] ~ MA[3]`
    - `Err`
    - 光标
  - 触发策略
    - 进页整页首绘
    - 每 `20ms` 刷新一次动态数值区
    - 若现场验证发现写屏负载偏大、菜单响应变肉或主循环压力明显升高，则降到 `50ms`

- **目录页（1 / 2 / 3 / 5 / 6）**
  - 静态区
    - 标题和参数名
  - 动态区
    - 当前参数值总览
    - 光标或选中标记
  - 触发策略
    - 进页整页首绘
    - 光标移动时局刷光标
    - 从参数子页返回时，按需刷新值区一次
  - 扩展规则
    - 项位置由统一布局按索引计算，不再手写 `1 * 18 / 2 * 18 ...`
    - 新增普通项时优先只补该页项表

- **参数编辑页（11~13 / 21~25 / 31~36 / 51~56 / 61~65）**
  - 静态区
    - 标题和参数名
  - 动态区
    - 当前参数值
    - 当前步进倍率
    - 光标或选中标记
  - 触发策略
    - 进页整页首绘
    - 只在按键修改值、切换倍率或返回时局刷
  - 扩展规则
    - 普通 `int/float/special` 参数使用统一处理逻辑
    - 新增普通参数时目标是不再补新的专用 `case`

## Extensibility And Layout Rules

- **推荐的轻量描述结构**
  - `menu_layout_t`
    - `title_y`
    - `first_row_y`
    - `row_height`
    - `label_x`
    - `value_x`
    - `max_visible_rows`
  - `menu_item_t`
    - `label`
    - `type`
    - `data_ptr`
    - `step`
    - `child_page`
  - `menu_page_t`
    - `page_id`
    - `title`
    - `page_type`
    - `refresh_period_ms`
    - `layout`
    - `items`
    - `item_count`
    - `draw_dynamic_hook`（仅特殊页需要）

- **位置计算规则**
  - 页面标题固定在 `title_y`
  - 列表首项从 `first_row_y` 开始
  - 每项位置由 `visible_index` 计算：
    - `y = first_row_y + visible_index * row_height`
  - 名称固定显示在 `label_x`
  - 数值固定显示在 `value_x`
  - 任何新增项都不应再手写单独坐标

- **可视窗口规则**
  - `cursor_index` 表示当前选中第几项
  - `scroll_offset` 表示当前屏幕从第几项开始显示
  - `visible_index = cursor_index - scroll_offset`
  - 当 `item_count <= max_visible_rows` 时不滚动
  - 当 `item_count > max_visible_rows` 时，光标超出可视窗口后滚动窗口
  - 滚动修正规则建议统一为：
    - 若 `cursor_index < scroll_offset`，则 `scroll_offset = cursor_index`
    - 若 `cursor_index >= scroll_offset + max_visible_rows`，则 `scroll_offset = cursor_index - max_visible_rows + 1`
  - 支持回绕时：
    - 若光标从首项再向上回绕到末项，则窗口跳到最后一屏
    - 若光标从末项再向下回绕到首项，则窗口回到第一页

- **新增普通菜单项的目标成本**
  - 新增普通参数项：
    - 优先只改一个页面项表
    - 如变量已存在，不再额外改页面处理分支
  - 新增新参数页：
    - 新增一个页面描述
    - 新增该页项表
    - 不再复制整套 `Show/Process/switch-case`

- **标签长度规则**
  - 标签长度应受控，避免侵入 `value_x`
  - 过长标签优先缩写，不依赖运行时自动挤压布局

- **特殊页边界**
  - `HOME`、`SENSOR` 可保留少量专用绘制逻辑
  - `STRAT / PID_1 / PID_2 / RING / FLY` 优先迁移到通用表驱动模式

- **滚动刷新规则**
  - 当 `cursor_index` 变化但 `scroll_offset` 不变时：
    - 只刷新旧光标和新光标所在区域
    - 不重画整块列表
  - 当 `scroll_offset` 变化时：
    - 说明可视窗口内容发生了整体平移
    - 不再只刷光标，而是重画“列表区”
    - 列表区重画时只覆盖名称列、数值列、光标列，不动标题和其他静态区
  - 列表区重画时：
    - 仅遍历 `scroll_offset ~ scroll_offset + max_visible_rows - 1`
    - 对超出 `item_count` 的可视行执行清空，避免残影
  - 结论：
    - “窗口没变”走 `cursor_dirty`
    - “窗口变了”走 `list_dirty`

### Task 1: Rebuild Key Scan As 10ms ISR-Driven Producer

**Files:**
- Modify: `project/service/key.h`
- Modify: `project/service/key.c`

- [ ] **Step 1: 明确事件模型**
  - 保留现有短按 `1~4`、长按/连发 `5~8` 编码
  - 新增“锁存事件”变量，避免 ISR 下一次扫描把事件清零后主循环读不到

- [ ] **Step 2: 调整按键扫描接口**
  - 将 `Keystroke_Scan()` 语义改为“10ms 节拍调用一次”
  - 新增 `Keystroke_Get_Event()` 或等价接口，由主循环读取并清除锁存事件

- [ ] **Step 3: 按 10ms 重新标定阈值**
  - 将 `LONG_PRESS_THRESHOLD` 从“扫描次数”改为“对应毫秒的计数”
  - 将 `REPEAT_INTERVAL` 调整到合理的人手手感，避免 10ms 连发过快

- [ ] **Step 4: 处理中断与主循环共享变量**
  - 共享状态标记为 `volatile`
  - 读取并清零事件时，保证读清过程不会丢事件

- [ ] **Step 5: 检查修改到位**
  - 确认菜单代码中不再直接依赖“同周期即时扫描+即时消费”的旧行为
  - 确认短按、长按、连发的时间含义已从“CPU 跑多快”变成“真实 10ms 节拍”

### Task 2: Introduce Table-Driven Menu Metadata And Layout

**Files:**
- Modify: `project/service/menu.h`
- Modify: `project/service/menu.c`

- [ ] **Step 1: 定义轻量页面/菜单项/布局描述结构**
  - 保持 C89 兼容，不引入过重抽象
  - 优先支持 `submenu / int / float / special` 四类普通项

- [ ] **Step 2: 实现统一位置计算**
  - 用 `first_row_y + visible_index * row_height` 统一计算项位置
  - 用 `label_x / value_x` 统一约束标签列和值列

- [ ] **Step 3: 为普通目录页和参数页建立描述表**
  - `STRAT / PID_1 / PID_2 / RING / FLY` 优先迁移为表驱动
  - `HOME / SENSOR` 暂保留专用绘制钩子

- [ ] **Step 4: 增加可视窗口能力**
  - 引入 `cursor_index` 与 `scroll_offset`
  - 当项数超出单页行数时保持显示位置仍然合理

- [ ] **Step 5: 明确滚动与回绕规则**
  - 固化 `cursor_index / scroll_offset / visible_index` 的更新顺序
  - 明确窗口未变化时只刷光标，窗口变化时重画列表区
  - 明确首尾回绕时窗口应跳到第一页或最后一页

- [ ] **Step 6: 检查新增项成本是否真的下降**
  - 验证新增一个普通参数项时，目标是只补一个项表条目
  - 验证不再需要到处补专用 `Show/Process` 分支

### Task 3: Add A 10ms UI Tick Callback

**Files:**
- Modify: `project/user/int_user.c`
- Modify: `project/service/menu.h`
- Modify: `project/service/menu.c`

- [ ] **Step 1: 新增 UI 10ms 回调入口**
  - 在菜单模块中提供 `Menu_Tick_10ms()` 或等价入口
  - 该入口只累加 UI 时基，不直接刷新画面

- [ ] **Step 2: 在 `int_user.c` 注册 TM1 回调**
  - 通过 `tim1_irq_handler` 注册一个轻量回调
  - 回调中只调用“按键扫描”和“UI 时基更新”

- [ ] **Step 3: 保持 ISR 轻量**
  - 不在回调里调用 `ips114_clear()`、`ips114_show_*()`
  - 不在回调里直接切页或保存配置

- [ ] **Step 4: 检查修改到位**
  - 确认 TM1 10ms 节拍已真正驱动按键扫描
  - 确认 ISR 工作量稳定且固定

### Task 4: Refactor `Keystroke_Menu()` Into A Nonblocking State Machine

**Files:**
- Modify: `project/service/menu.c`
- Modify: `project/service/menu.h`

- [ ] **Step 1: 去掉阻塞循环**
  - 删除所有页面处理函数中的 `while (menu_next_flag == 0)`
  - 保证每个页面函数执行一次就返回

- [ ] **Step 2: 把“扫描输入”和“页面处理”解耦**
  - 菜单入口先读取锁存按键事件
  - 再更新光标、页面跳转、参数修改

- [ ] **Step 3: 去掉页面函数里的直接清屏副作用**
  - `Cursor()` 改成只更新光标状态并设置脏标志
  - `Menu_Next_Back()` 与 `HandleKeystroke()` 改成只改状态，不直接大面积写屏

- [ ] **Step 4: 统一入口行为**
  - `Keystroke_Menu()` 负责：
    - 消费按键事件
    - 处理页面状态推进
    - 根据脏标志决定全刷或局刷
    - 立刻返回

- [ ] **Step 5: 检查修改到位**
  - 确认开启菜单后，主循环不会因为菜单停留在任何页面而被卡住

### Task 5: Split Full Refresh And Partial Refresh

**Files:**
- Modify: `project/service/menu.c`

- [ ] **Step 1: 增加脏标志**
  - 至少区分：`page_changed`、`cursor_dirty`、`value_dirty`、`realtime_due`、`prompt_active`

- [ ] **Step 2: 按页面类型规划刷新策略**
  - `HOME`：静态区进页首绘，动态区定时局刷
  - `SENSOR`：静态区进页首绘，数据区定时局刷
  - 目录页：进页首绘，光标移动局刷，回页时按需重刷数据
  - 参数编辑页：进页首绘，仅在参数值或步进倍率变化时局刷

- [ ] **Step 3: 给实时页设默认刷新周期**
  - `HOME` 从 `50ms` 起步
  - `SENSOR` 从 `20ms` 起步，必要时降频到 `50ms`

- [ ] **Step 4: 建立“动态区清单”与触发逻辑**
  - 为 `HOME`、`SENSOR`、目录页、参数编辑页分别明确静态区与动态区
  - 把 `10ms` tick 累计到页面阈值后映射为 `realtime_due`
  - 明确 `50ms`/`20ms` 代表“动态区刷新周期”而不是整页重画周期

- [ ] **Step 5: 约束刷屏范围**
  - 只重写需要变化的数字区域、光标区域、提示区域
  - 页面标题和固定标签尽量不重复绘制

- [ ] **Step 6: 落实滚动时的刷新分流**
  - 记录 `old_cursor_index` 与 `old_scroll_offset`
  - 当 `scroll_offset` 未变时，仅置位 `cursor_dirty`
  - 当 `scroll_offset` 变化时，置位 `list_dirty` 并重画列表区

- [ ] **Step 7: 验证布局规则确实生效**
  - 检查新增项位置是否由统一布局计算得出
  - 检查标签列和值列没有互相覆盖
  - 检查项数超出单页时滚动窗口仍能保持坐标正确
  - 检查滚动发生时列表区内容不会残留旧项

- [ ] **Step 8: 检查修改到位**
  - 确认静止不操作的参数页不再频繁整页重画
  - 确认实时页仍有足够的人眼实时性

### Task 6: Replace Blocking Save Prompt With Timed Overlay State

**Files:**
- Modify: `project/service/menu.c`

- [ ] **Step 1: 删除 `system_delay_ms(300)`**
  - 保存成功提示改为菜单内部状态

- [ ] **Step 2: 用 10ms UI 时基实现 300ms 提示窗口**
  - 提示激活时，主循环优先渲染提示层
  - 时间到后退出提示层，并把当前页标记为需要重绘

- [ ] **Step 3: 明确提示期间的输入策略**
  - 建议提示期间忽略菜单输入，但不阻塞主循环

- [ ] **Step 4: 检查修改到位**
  - 确认保存动作不再带来 300ms 硬阻塞

### Task 7: Verification And Tuning

**Files:**
- Verify: `project/service/key.c`
- Verify: `project/service/menu.c`
- Verify: `project/user/int_user.c`
- Build: `project/mdk/seekfree.uvproj`

- [ ] **Step 1: 静态检查**
  - 确认 `menu.c` 中不再存在菜单页内部阻塞 `while`
  - 确认 `system_delay_ms(300)` 已移除
  - 确认所有 ISR 相关共享变量都已按设计处理

- [ ] **Step 2: 编译验证**
  - 在 Keil5 C251 下完整编译
  - 重点检查 C89 兼容性、声明位置、`volatile` 共享变量和函数声明一致性

- [ ] **Step 3: 菜单功能验证**
  - 短按上下移动光标
  - 长按上下连续调参
  - `KEY3` 切换步进倍率
  - `KEY4` 返回上级
  - 返回首页后触发保存提示但不阻塞

- [ ] **Step 4: 刷新行为验证**
  - `HOME` 页面动态值正常更新
  - `SENSOR` 页面实时数据正常更新
  - `HOME 50ms` 表现为每 5 个 `10ms` tick 刷新一次动态区，而不是每 5 个 tick 整页重画
  - `SENSOR 20ms` 表现为每 2 个 `10ms` tick 刷新一次动态区；如果现场评估负载过高，可切到 `50ms`
  - 参数页静止时不整页狂刷

- [ ] **Step 5: 扩展性验证**
  - 试着以“新增一个普通参数项”的思路检查代码路径
  - 确认新增项不需要再补多处 `switch-case`
  - 确认新增项能落在合理行列位置
  - 确认当项数超出单页时滚动窗口逻辑正确

- [ ] **Step 6: 滚动刷新验证**
  - 当 `cursor_index` 变化且 `scroll_offset` 不变时，只刷新光标相关区域
  - 当 `scroll_offset` 变化时，仅重画列表区，不重画整页
  - 检查回绕到首项/末项时窗口位置正确
  - 检查最后一屏不足整页时不会保留旧内容

- [ ] **Step 7: 现场调参**
  - 根据实际手感微调长按阈值、连发间隔、`HOME` 刷新周期、`SENSOR` 刷新周期

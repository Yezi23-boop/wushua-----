# `speed_loop_autotune` 整体修改评审与分析框架

## 1. 评审范围

本次评审覆盖以下几层：

- `project/speed_loop_autotune/firmware`
- `project/service/vofa.c`
- `project/service/debug_view.c`
- `project/service/speed_loop_autotune_adapter.c`
- `project/user/int_user.c`
- `project/user/isr.c`

目标是确认三件事：

- `speed_loop_autotune` 组件化、外部注册和 VOFA 解耦后的主链路是否闭合
- 当前“开关”是否真的能控制专题组件启停
- host 命令、5ms 控制执行和 legacy 回退是否存在逻辑漏洞

## 2. 当前结构框架

### 2.1 分层

当前代码已经基本形成 5 层：

1. 板级适配层
   - `project/service/speed_loop_autotune_adapter.c`
   - 负责把外部 `PID.left_speed/right_speed` 六个成员和板级端口注册给组件

2. 组件核心层
   - `project/speed_loop_autotune/firmware/autotune_component.c`
   - `autotune_pid_core.c`
   - `autotune_runtime.c`
   - `air_dual_mode.c`
   - `ground_dual_mode.c`
   - `pwm_identify_mode.c`

3. host transport / host service 层
   - `host_autotune_command.c`
   - `host_transport.c`
   - `host_service.c`
   - 负责文本命令消费、解析统计和 telemetry 输出

4. service 集成层
   - `project/service/vofa.c`
   - `project/service/debug_view.c`
   - 负责 VOFA FIFO 解析、legacy 回退和上位机服务开关

5. 系统调度层
   - `project/user/int_user.c`
   - `project/user/isr.c`
   - 负责初始化和 5ms 中断调度

### 2.2 启动链

当前启动顺序是：

1. `int_user()` 调 `control_init()`
2. `control_init()` 无条件调用 `speed_loop_autotune_project_init()`
3. `speed_loop_autotune_project_init()` 把外部参数和端口注册进组件
4. `TM0_IRQHandler()` 每 5ms 调 `run_test_speed()`

关键位置：

- `project/user/int_user.c:72`
- `project/user/isr.c:101`
- `project/service/speed_loop_autotune_adapter.c`

### 2.3 VOFA 链

当前 VOFA 主链是：

1. `main()` 调 `debug_vofa_service()`
2. `debug_vofa_service()` 根据宏选择：
   - `vofa_service()`
   - 或 `vofa_service_legacy()`
3. `vofa_service()` 负责：
   - 从 FIFO 拉取命令
   - 更新 parser 统计
   - 先交给 `speed_loop_autotune_handle_text_command()`
   - 未消费时回退到 legacy 分支
   - 最后发 telemetry

关键位置：

- `project/service/debug_view.c:5`
- `project/service/debug_view.c:11`
- `project/service/vofa.c:174`
- `project/service/vofa.c:191`
- `project/service/vofa.c:315`

### 2.4 5ms 控制链

当前 5ms 执行链是：

1. `TM0_IRQHandler()`
2. `run_test_speed()`
3. 根据 mode 进入：
   - `air_dual_run_tick()`
   - `ground_dual_run_tick()`
   - `pwm_identify_run_tick()`

默认情况下，如果没有 ground/pwm-identify 特殊状态，会落到 `air_dual_run_tick()`。

关键位置：

- `project/speed_loop_autotune/firmware/speed_loop_trial.c:16`
- `project/speed_loop_autotune/firmware/air_dual_mode.c:42`

## 3. 评审结论

### 3.1 主要结论

`speed_loop_autotune` 的组件化、外部注册、VOFA transport 解耦和 legacy 回退路径整体是通的，Python 回归和 Keil 重建也能证明这条链目前可编译、可运行。

但当前仍有 1 个需要优先处理的逻辑问题。

## 4. 发现的问题

### [P1] `debug_view.c` 的宏只关掉了 VOFA 服务，没有真正关掉 `speed_loop_autotune` 组件执行

当前新增的开关：

- `project/service/debug_view.c:5`

只影响 `debug_vofa_service()` 走：

- `vofa_service()`
- 或 `vofa_service_legacy()`

但是它**不会影响**下面两件事：

1. `speed_loop_autotune_project_init()` 仍然在启动时无条件执行  
   位置：`project/user/int_user.c:72`

2. `TM0_IRQHandler()` 仍然每 5ms 无条件执行 `run_test_speed()`  
   位置：`project/user/isr.c:101`

而 `run_test_speed()` 在组件已初始化时，会继续进入专题控制链；默认模式下最终会走 `air_dual_run_tick()`：

- `project/speed_loop_autotune/firmware/speed_loop_trial.c:16`
- `project/speed_loop_autotune/firmware/air_dual_mode.c:44`

这意味着：

- `DEBUG_VIEW_ENABLE_SPEED_LOOP_AUTOTUNE = 0` 目前只是关闭了“VOFA 与专题组件的串口服务耦合”
- 但**没有关闭专题组件本身的初始化和 5ms 控制执行**

如果你的预期是“把这个宏改成 0 就完全关闭 `speed_loop_autotune` 功能”，那么当前实现**不满足预期**。

### 风险表现

- 现场会误以为专题组件已关闭，但中断里仍在执行专题控制链
- 如果后续想把这个宏作为“比赛固件 / 调参固件”切换开关，当前位置不够高，语义不完整
- 当前分支的 `TM0` 已经不再执行原来的 `run_time_1()`，所以这个宏不是整车控制路径的总开关

## 5. 暂未发现的新问题

本轮检查中，以下部分没有发现新的明显逻辑问题：

- `speed_loop_autotune` 与 `vofa.c` 的 transport 解耦
- `vofa_service()` 中 `speed_loop_autotune` 命令优先、legacy 回退次之的顺序
- parser 统计从 VOFA service 同步到组件 `INFO`
- Keil 工程已纳入 `host_transport.c/.h`

## 6. 推荐整改方向

如果希望“宏 = 真正功能开关”，建议把同一个宏上提为**专题组件总开关**，至少统一控制下面 3 个点：

1. `project/user/int_user.c`
   - 控制是否调用 `speed_loop_autotune_project_init()`

2. `project/user/isr.c`
   - 控制 `TM0_IRQHandler()` 走 `run_test_speed()` 还是走原正常控制链

3. `project/service/debug_view.c`
   - 控制是否走 `vofa_service()` 还是 `vofa_service_legacy()`

更稳的做法是把宏统一定义到一个公共头，例如：

- `project/service/debug_view.h`
- 或 `project/speed_loop_autotune/firmware/speed_loop_autotune.h`
- 或单独 `project/service/speed_loop_autotune_switch.h`

然后由：

- `int_user.c`
- `isr.c`
- `debug_view.c`

共同引用，避免一个文件里关了，另外两个地方还在跑。

## 7. 建议的分析框架

后续你再看这套专题代码，建议按下面顺序检查：

### 第一步：看总开关是否一致

先确认：

- 初始化是否开
- 5ms 中断是否开
- VOFA 服务是否开

如果这 3 处不是同一套开关控制，先不要继续往下看。

### 第二步：看“注册”和“执行”是否分离

检查：

- 板级绑定是不是只在 `speed_loop_autotune_adapter.c`
- 组件内部是不是只通过 `binding/port` 访问外部资源
- VOFA / service 层是否只负责 transport，不直接碰组件内部状态

### 第三步：看 host 命令链

检查路径：

- `vofa_service()`
- `handle_vofa_command()`
- `speed_loop_autotune_handle_text_command()`
- `host_autotune_command.c`

确认：

- 专题命令先消费
- 未消费时 legacy 回退仍然存在
- `INFO`、`START`、`AT_RESET`、`SAVE/LOAD` 都能找到归属

### 第四步：看 5ms 中断链

检查路径：

- `TM0_IRQHandler()`
- `run_test_speed()`
- `air/ground/pwm-identify` 分发

确认：

- 中断里没有引入新的重逻辑
- 默认模式行为明确
- stop / reset / cooldown 路径能及时清零输出

### 第五步：看 telemetry 与现场调试能力

确认：

- telemetry 是否仍和 host 解析兼容
- parser 统计是否还能出现在 `INFO`
- 禁用专题组件时，legacy 调试链是否仍可独立使用

## 8. 当前验证记录

已验证：

- `python -m unittest project.speed_loop_autotune.tests.test_vofa_autotune`
- `python -m unittest tests.test_vofa_autotune`
- `D:\keil_5\UV4\UV4.exe -r ...seekfree.uvproj -j0 -o ...build_speed_loop_autotune_rebuild.log`

结果：

- Python 单测通过
- Keil 重建通过

但这些验证只能说明“当前代码能编译、现有测试能过”，**不能证明 `debug_view.c` 宏已经是完整功能开关**。

## 9. 最终判断

当前整体修改方向是对的：

- 分层更清楚
- transport 和组件边界更清楚
- legacy 回退保住了

但“专题组件开启/关闭”这件事现在只做了一半。

所以当前结论是：

- **结构方向正确**
- **主链路基本闭合**
- **仍存在一个高优先级语义问题：开关位置不够高，未形成真正总开关**

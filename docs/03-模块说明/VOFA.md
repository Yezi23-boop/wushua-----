# VOFA 模块

## 当前状态

当前仓库未包含 `project/speed_loop_autotune/` 专题目录。速度环自动调参相关的旧
host/firmware/test 入口已经不再是本工程的有效实现。

当前可用的 VOFA 链路是 `project/service/vofa.c` 中的 legacy 文本命令解析：

- 从无线串口 FIFO 读取字节流
- 以 `!` 作为命令结束符
- 将完整命令放入固定深度队列
- 在主循环 `debug_vofa_service()` 中消费命令
- 支持旧版 PID、测试值、保存/加载和启停命令

`project/service/debug_view.c` 中的 `DEBUG_VIEW_ENABLE_SPEED_LOOP_AUTOTUNE` 当前为 `0`，
因此 `debug_vofa_service()` 会调用 `vofa_service_legacy()`。

## 旧工具入口

以下脚本只保留为兼容提示，不再尝试导入不存在的专题目录：

- `tools/vofa_autotune.py`
- `tools/agent_autotune.py`
- `tools/agent_autotune_sim.py`
- `tools/agent_autotune_session_sim.py`

执行这些脚本的 `--help` 会说明专题目录当前不可用；真实自动调参若后续恢复，需要同步恢复
`project/speed_loop_autotune/`、Keil 工程引用、测试和本页说明。

## 可用命令概览

legacy 命令仍在 `vofa.c` 中维护，常用形式为：

- `L_KP=...`、`L_KI=...`、`L_KD=...`
- `R_KP=...`、`R_KI=...`、`R_KD=...`
- `TEST_DIFF=...`
- `TEST_speed=...`
- `MOTOR=...`
- `SAVE`
- `LOAD`
- `STOP`
- `START`

注意：legacy 速度环 PID 命令直接修改 `PID.left_speed` / `PID.right_speed` 运行副本，
没有进入 `app` 配置真值链路。需要长期保存的参数仍应优先走菜单和 EEPROM 配置链。

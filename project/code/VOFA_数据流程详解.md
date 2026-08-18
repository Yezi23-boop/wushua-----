# VOFA 数据流程详解

当前仓库未包含 `project/speed_loop_autotune/` 自动调参专题目录，VOFA 数据流以
`project/service/vofa.c` 和 `project/service/debug_view.c` 的 legacy 链路为准。

## 接收流程

```text
wireless_uart FIFO
  -> vofa_parse_from_fifo()
  -> vofa_parse_byte()
  -> cmd_queue
  -> vofa_service_legacy()
  -> vofa_handle_legacy_command()
```

设计边界：

- 串口 DMA 中断只负责底层接收和回调，不做复杂文本解析。
- `debug_vofa_service()` 在主循环运行，避免占用 5ms 控制中断。
- 命令队列固定深度，队列满时丢弃新命令并记录溢出计数。

## 参数写入路径

legacy 命令分两类：

- `key=value`：写运行态参数或测试变量。
- 动作命令：执行保存、加载、启停等动作。

速度环 PID 命令直接写 `PID.left_speed` / `PID.right_speed`，不会自动同步到 `app`。
这与菜单调参链路不同，现场使用时要区分“临时运行态试验”和“EEPROM 持久配置”。

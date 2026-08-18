# VOFA 调参

## 当前可用范围

当前仓库未包含 `project/speed_loop_autotune/` 自动调参专题目录。现阶段 VOFA 只作为
legacy 在线命令入口使用，真实解析逻辑位于 `project/service/vofa.c`。

可用能力：

- 串口字节流解析与命令排队
- 手动修改左右速度环 PID 运行副本
- 修改测试值
- 执行 `SAVE` / `LOAD` / `STOP` / `START`

不可用能力：

- 自动批次试验
- agent 决策调参
- `project/speed_loop_autotune/host/*` 上位机脚本
- `project/speed_loop_autotune/firmware/*` 固件专题组件

## 使用边界

legacy VOFA 命令直接写运行态变量，尤其是 `L_KP/L_KI/L_KD/R_KP/R_KI/R_KD` 会直接改
`PID.left_speed` 和 `PID.right_speed`。这条路径不等价于菜单配置链：

```text
menu
  -> app
  -> control_apply_config()
  -> PID
  -> config_save()
  -> EEPROM
```

因此，VOFA 更适合临时排查和快速试验；需要掉电保存、现场稳定复现的参数，仍建议通过菜单或
明确的 EEPROM 保存流程管理。

## 旧入口说明

`tools/vofa_autotune.py` 以及 agent 自动调参相关脚本现在只输出不可用提示，不再导入缺失目录。
若后续恢复自动调参专题，应同时恢复目录、测试、文档和 Keil 工程配置。

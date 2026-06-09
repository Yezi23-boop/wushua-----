# VOFA 协议说明

当前仓库未包含 `project/speed_loop_autotune/` 自动调参专题目录，本页不再跳转到该路径。

现有 VOFA 协议以 `project/service/vofa.c` 的 legacy 文本命令为准：

- 单条命令以 `!` 结束
- `key=value` 形式用于参数写入
- 无等号命令用于动作触发
- 命令在主循环服务中解析，避免把文本解析放入 5ms 高频控制链

常见命令包括：

- `L_KP=...`、`L_KI=...`、`L_KD=...`
- `R_KP=...`、`R_KI=...`、`R_KD=...`
- `TEST_DIFF=...`
- `TEST_speed=...`
- `MOTOR=...`
- `SAVE`
- `LOAD`
- `STOP`
- `START`

旧 `tools/vofa_autotune.py` 只保留不可用提示，不再代表可执行自动调参流程。

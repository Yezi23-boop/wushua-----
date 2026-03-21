# VOFA 模块

速度环自动调参与上位机反复试验的真实实现已经收拢到 `project/speed_loop_autotune/`。

当前职责划分：
- `project/service/vofa.c` 保留通用 FireWater/VOFA 字节流解析、命令提取和通用配置命令。
- `project/speed_loop_autotune/firmware/host_autotune_command.*` 负责速度环自动试验相关命令。
- `project/speed_loop_autotune/firmware/host_service.*` 负责专题服务入口和 7 列遥测上报。
- `project/speed_loop_autotune/firmware/speed_loop_trial.*` 负责试验状态机和 `run_test_speed()`。

兼容入口仍保留：
- `debug_vofa_service()`
- `tools/vofa_autotune.py`
- `tests/test_vofa_autotune.py`

优先阅读：
- `project/speed_loop_autotune/README.md`
- `project/speed_loop_autotune/docs/protocol.md`
- `project/speed_loop_autotune/docs/scoring.md`

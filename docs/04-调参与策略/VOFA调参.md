# VOFA 调参

本仓库的速度环自动调参已经整理为独立专题目录 `project/speed_loop_autotune/`。

当前建议：
- 上位机脚本以 `project/speed_loop_autotune/host/vofa_autotune.py` 为真实实现。
- 旧入口 `tools/vofa_autotune.py` 只保留兼容层。
- 评分规则、超调门槛和现场调参说明优先参考专题文档，不再以本页维护细节。

优先阅读：
- `project/speed_loop_autotune/README.md`
- `project/speed_loop_autotune/docs/protocol.md`
- `project/speed_loop_autotune/docs/scoring.md`

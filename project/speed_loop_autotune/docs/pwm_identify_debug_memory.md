# PWM Identify Debug Memory

这份文档用于沉淀 `pwm-identify` 的开环辨识事实和结论。

使用规则：
- 先记录已验证事实，再记录推断
- 只把重复验证过的结论升级到 `pwm_identify_rules.md`
- 如果新结论推翻旧结论，不删除旧记录，直接标注“已替代”

## 当前状态

- 2026-03-23：新增 `pwm-identify` 模式，默认按单轮开环 PWM 阶跃收集有效级别
- 2026-03-23：Telemetry 追加 `mode_id,left_cmd_pwm,right_cmd_pwm`
- 2026-03-23：种子 PI 只产出到 RAM，不自动串联 `air-dual`

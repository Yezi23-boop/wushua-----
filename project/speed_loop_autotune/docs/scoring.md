# Speed Loop Autotune Scoring

当前默认评分聚焦高速竞速小车更关心的 4 个主指标：

- 起速 `rise_ratio`
- 超调 `overshoot`
- 稳定时间 `settle_ratio`
- 稳态误差 `steady_error`

## 默认权重

- `rise_weight = 1.0`
- `overshoot_weight = 12.0`
- `settle_weight = 2.6`
- `steady_weight = 10.0`
- `overshoot_gate = 0.08`
- `overshoot_gate_penalty = 3.0`

## 设计取向

- 对高速循迹车，优先避免明显超调和长时间收敛
- `skew` 与 `tail_jitter` 仍保留在指标里方便观察，但不参与主评分
- 超调超过门槛后会触发额外惩罚，避免“起得快但很毛躁”的参数胜出

## 调整入口

上位机脚本支持以下命令行参数：

- `--score-rise-weight`
- `--score-overshoot-weight`
- `--score-settle-weight`
- `--score-steady-weight`
- `--score-overshoot-gate`
- `--score-overshoot-gate-penalty`

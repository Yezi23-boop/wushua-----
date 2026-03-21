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

## 架空双轮评分

- 主评分入口：`score_dual_wheel_multi_speed_trial()`
- 先分别计算左轮、右轮多速度段得分
- 再做双轮合成评分
- 当双轮同时高 PWM、但速度明显追不上目标时，会追加“供电余量/速度下坠”惩罚

## 带负载双轮评分

- 主评分入口：`_score_ground_load_trial()`
- 重点看各段分数、停车状态、残余速度、残余 PWM
- `stop_flag`、`trial_active`、冷却段残余速度在带负载试验里直接参与惩罚
- 带负载双轮当前不复用架空双轮的“高 PWM 速度下坠”附加项描述，应按实车/带载逻辑单独分析

## 调整入口

上位机脚本支持以下命令行参数：

- `--score-rise-weight`
- `--score-overshoot-weight`
- `--score-settle-weight`
- `--score-steady-weight`
- `--score-overshoot-gate`
- `--score-overshoot-gate-penalty`

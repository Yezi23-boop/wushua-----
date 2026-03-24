# Ground Dual Speed Loop Tuning Rules

这份文档只记录 `ground-dual` 的带负载双轮回归规则，不复用 `air-dual` 或 `pwm-identify` 的结论。

## 目标

- 验证带负载双轮在真实阻力、供电压降和停车条件下是否仍稳定
- 重点看整组试验可重复性，而不是单段最低分

## 默认环境

- 串口：`COM8`
- 模式：`ground-dual`
- 负压：按实车需要设置，不默认沿用 `AT_FUYA=0`
- 默认不发送 `SAVE`

## 默认序列

- `25:200,35:200,45:200,35:200,25:200`

## 默认命令链

1. `AT_FUYA=<value>`
2. `AT_COOLDOWN_MS=<value>`
3. `AT_ARM`
4. `AT_TRIAL_MS=<value>`
5. `AT_SPEED=<first_target>`
6. `AT_FIRE`

## 评分关注点

- 各段综合得分
- `stop_flag`
- `trial_active`
- 冷却段残余速度
- 冷却段残余 PWM

## 使用边界

- `ground-dual` 只做带载回归和保守筛选
- 不在本模式里做开环 PWM 辨识
- 如果 `air-dual` 单轮更优、但 `ground-dual` 明显更差，优先保守回退

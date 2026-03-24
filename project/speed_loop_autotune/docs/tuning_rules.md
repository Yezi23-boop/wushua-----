# Air Dual Speed Loop Tuning Rules

## Confirmed PWM Limits

- Wheel motor PWM should be treated as `0~10000`.
- This wheel PWM range is the default assumption for `air-dual`, `ground-dual`, `pwm-identify`, and `pwm-map`.
- `AT_FUYA` is a separate vacuum command and should be treated as `0~4000`.
- Real hardware verification on `2026-03-24` over `COM8` confirmed that wheel commands at `5000`, `7000`, and `9000` were applied as real PWM.

这份文档只记录 `air-dual` 的默认闭环调参规则。
带负载双轮请看 `ground_dual_tuning_rules.md`，开环辨识请看 `pwm_identify_rules.md`。

## 目标

- 给左右轮分别找一组可共用的闭环速度环 PID
- 先单轮隔离，再双轮联合微调
- 优先保证多速度段稳定可重复，而不是单段最低分

## 默认环境

- 串口：`COM8`
- 模式：`air-dual`
- 负压：关闭，固定 `AT_FUYA=0`
- 默认不发送 `SAVE`

## 默认速度序列

主序列：
- `15:500,25:500,35:500,45:500,35:500,25:500,15:500`

校验序列：
- `15:300,25:300,35:300,45:300,35:300,25:300,15:300`

两组序列尾部都补 `TEST_speed=0`，并把尾部回零纳入采样窗口。

## 调参顺序

1. 先调 `Kp`
2. 再调 `Ki`
3. 只有连续多轮超调压不住时才尝试 `Kd`

当前默认步长：
- `Kp = 10`
- `Ki = 5`
- `Kd = 0.5`
- 搜索步长收敛到 `1.0` 就停

## 单轮隔离

- 调左轮时：左轮使用待测 PID，右轮固定 `0/0/0`
- 调右轮时：右轮使用待测 PID，左轮固定 `0/0/0`

## 双轮联调

- 单轮粗调后，允许在双轮一起跑的条件下做一轮小范围联合微调
- 双轮评分会额外惩罚“高 PWM 但速度明显爬不上目标”的候选

## 评分关注点

- 起速
- 超调
- 稳定时间
- 稳态误差
- 双轮同时高 PWM 时的速度下坠

## 更新规则

- `air-dual` 的新经验先写入 `debug_memory.md`
- 只有重复验证过的规则，才升级到这份文档

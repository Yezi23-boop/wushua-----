# PWM Identify Rules

这份文档只记录 `pwm-identify` 的开环 PWM 辨识规则。

## 目标

- 在架空单轮条件下，用开环 PWM 阶跃找到左右轮的种子 PID
- 只产出种子 PID，不直接做最终闭环精调

## 使用边界

- 只允许架空
- 只做正向 PWM
- 一次只激活一侧轮，另一侧固定 `0`
- 默认不发送 `SAVE`

## 默认参数

- `identify_pwm_step = 200`
- `identify_pwm_max = 3200`
- `identify_repeat = 2`
- `identify_hold_ms = 250`
- `identify_tail_zero_ms = 200`
- `AT_TEST_MODE = 1`

## 有效级别判定

- 当前轮速度连续 3 个样本绝对值大于 `5.0`，认为突破死区
- 每侧最多保留前 3 个有效 PWM 级别
- 每侧至少要有 2 个有效级别，否则整次辨识失败

## 指标提取

- 从命令 PWM 有效窗口的最后 20% 样本估稳态速度
- 在 10% 响应点估 `theta`
- 在 63.2% 响应点估 `tau`
- 用两个有效级别之间的 `delta_speed / delta_pwm` 估对象增益 `k`

## 种子 PI

- `tau_c = max(theta, 0.5 * tau)`
- `Kc = (1 / k) * tau / (tau_c + theta)`
- `Ti = min(tau, 4 * (tau_c + theta))`
- `Kd = 0`
- 离散化固定用 `Ts = 0.005s`

## 后续流程

- 辨识完成后只打印左右轮种子 PID
- 只有打开 `--apply-identify-seed` 时，才下发到 RAM
- 真正的闭环精调仍回到 `air-dual`

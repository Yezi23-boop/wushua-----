# PWM Identify Rules

Current workflow note:
- `pwm-identify` is still part of the supported path.
- Once seed PI is produced, the next closed-loop stage must continue through the Agent workflow in `docs/agent_autotune.md`.

这份文档只记录 `pwm-identify` 的开环 PWM 辨识规则。

## 当前默认前提

- `identify_pwm_max = 10000`
- 默认读取共享 profile：
  - `project/speed_loop_autotune/logs/current_tuning_profile.json`
- 共享 profile 中必须已经包含：
  - `pwm_map.left.deadzone_break_pwm`
  - `pwm_map.right.deadzone_break_pwm`
- `pwm-identify` 不再负责测试死区，死区由 `pwm-map` 先测出并写入 profile
- `pwm-identify` 的识别起点是“死区之上的第一个识别档位”，不是从 `0` 盲扫
- 成功辨识后会把 `seed_pi.left` 和 `seed_pi.right` 回写到同一份共享 profile

## 目标

- 在架空单轮条件下，用开环 PWM 阶跃找出左右轮的种子 PID
- 只产出种子 PID，不直接做最终闭环精调

## 使用边界

- 只允许架空
- 只做正向 PWM
- 一次只激活一侧轮，另一侧固定 `0`
- 默认不发送 `SAVE`

## 默认参数

- `identify_pwm_step = 200`
- `identify_pwm_max = 10000`
- `identify_repeat = 2`
- `identify_hold_ms = 250`
- `identify_tail_zero_ms = 200`
- `AT_TEST_MODE = 1`

## 有效级别判定

- 当前轮速度连续 3 个样本绝对值大于 `5.0`，认为该档位已经达到可识别运动条件
- 每侧最多保留前 3 个有效 PWM 级别
- 每侧至少要有 2 个有效级别，否则整次辨识失败

说明：
- 这里的“有效级别判定”只用于确认某个识别档位能不能参与 PI 建模
- 它不再承担“找死区”的职责

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
- 真正的闭环精调改为回到 Agent 工作流里的 `air_dual` batch stage

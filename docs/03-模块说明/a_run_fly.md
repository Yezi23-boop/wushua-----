# a_run_fly 模块

## 模块职责

`a_run_fly.c` 负责跷跷板/飞坡流程中的弱磁入口识别、离线保持、落地恢复和冷却窗口。它只在 `a_run_track_element` 判定当前期望元素为跷跷板时开放新入口。

## 对外入口函数

- `a_run_fly_update_speed(int *speed, uint8 allow_entry)`
- `a_run_fly_take_finish_event()`
- `a_run_fly_reset()`

## 依赖与被依赖关系

依赖：

- `app.fly`
- `ad1~ad4`
- `Err`
- `PID.steer.output`

被依赖：

- `a_run.c`
- `a_run_track_element.c`
- `motor.c` 的丢线保护读取 `fly_lost_line_blocked`

## 状态机流程

```text
IDLE
  -> HOLD
  -> RECOVER
  -> COOLDOWN
  -> IDLE
```

- `IDLE`：等待四路电感同时弱磁，且 `allow_entry` 为 1。
- `HOLD`：使用 `app.fly.count_fly_speed`，并把转向目标锁到 `FLY_HOLD_ANGLE`。
- `RECOVER`：电感回升后从低速开始阶梯提速，前 200ms 限制转向，直到中线连续稳定。
- `COOLDOWN`：完成后短时间禁止重复触发，随后复位回 `IDLE`。

## 关键运行数据

- `flat_fly`：当前飞坡/跷跷板阶段，供菜单和调试观察。
- `fly_lost_line_blocked`：高风险窗口屏蔽丢线保护；RECOVER 超过 1s 仍未完成时恢复丢线保护。
- `fly_recover_speed`：RECOVER 阶段当前阶梯速度，最大不超过 `app.fly.count_fly_speed`。

## 高频路径注意事项

- 本模块运行在 5ms 主控制链中，判断逻辑应保持常量比较和简单计数。
- RECOVER 阶段当前不再强制清零电机输出，而是通过限制转向和阶梯提速降低刚贴地打滑风险。
- `fly_lost_line_blocked` 必须及时释放，否则真实丢线会被误屏蔽；当前超过 1s 未恢复会重新打开丢线保护。

## 调参与常见风险

- 入口过早触发：提高 `count_fly_time_1` 或检查弱磁阈值是否过松。
- 离线保持过短：增加 `count_fly_time_2`，避免还未落地就进入恢复。
- 落地后打滑：降低 `count_fly_speed`，或延长前段限转向时间。

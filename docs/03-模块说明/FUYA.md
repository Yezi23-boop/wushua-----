# FUYA 模块

## 模块职责

`FUYA.c` 负责负压风机固定输出控制。当前策略是不再根据姿态或墙面状态动态调整负压，上电初始化保持停机，进入预启动状态后按 `app.start.fuya_xili` 固定输出。

## 对外入口函数

- `fuya_Init()`
- `fuya_motor_output(int pwm)`
- `fuya_set_percent(uint8 percent)`
- `fuya_update_simple()`

## 依赖与被依赖关系

依赖：

- `vx / vy / vz`
- `app.start.fuya_xili`
- EEPROM 恢复后的 `app`

被依赖：

- `int_user.c`
- `a_run_mode.c`
- 测试入口和历史兼容路径

## 关键运行数据

- `fuya_date`
- `fuya_target_pwm`
- `fuya_target_percent`
- `fuya_cylinder_peak_flag`

## 高频路径注意事项

- `fuya_Init()` 只初始化 PWM 并保持停机，不主动拉起负压
- `fuya_update_simple()` 只是兼容旧调用点，不改变负压输出
- 固定输出不依赖周期爬坡，`fuya_set_percent()` 会一次写到目标 PWM

## 调参与常见风险

- 当前对外暴露的主要可调参数是 `fuya_xili`
- `fuya_wall_percent` 保留在 EEPROM 和菜单中，但当前固定负压策略不使用它
- 如果调参对实际输出影响不明显，先确认是否已经进入 `start_state == 1` 的预启动拉负压阶段

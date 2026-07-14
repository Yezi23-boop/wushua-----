# PID 模块

## 模块职责

`pid.c` 负责左右速度环、转向环、角度环的初始化与更新，是整车核心控制计算中心。

## 对外入口函数

- `pid_speed_init()`
- `pid_steer_init()`
- `pid_speed_update()`
- `pid_steer_update()`
- `pid_angle_update()`（仅圆环目标角速度非零时调用）
- `Encoder_get()`
- `Pid_Differential()`（旧串级目标轮速分配接口，正式并级主链不调用）
- `Pure_Pursuit_Gyro_Control()`

## 依赖与被依赖关系

依赖：

- 编码器
- 电感误差 `Err`
- IMU 角速度
- `app`

被依赖：

- `int_user.c` 的控制器初始化
- `a_run.c` 的定时控制主链
- `vofa.c` 的在线调参

## 关键运行数据

- `PID.left_speed`
- `PID.right_speed`
- `PID.steer`
- `PID.angle`（圆环专用角速度控制器状态）

## 高频路径注意事项

- 速度环和姿态环都在周期任务中运行，参数修改必须避免破坏实时链
- 当前转向环包含 `Kp2 * error * abs(error)` 非线性项，适合高速下增强大误差段修正，但也更容易过猛
- `Pure_Pursuit_Gyro_Control()` 目前不是中断主链默认路径，属于可选控制逻辑

## 调参与常见风险

建议理解成四层关系：

1. 传感器误差是否可信
2. 转向环是否能稳定给出目标
3. 角度环是否能跟上目标
4. 左右速度环是否能稳定执行输出

如果车出现甩尾、蛇形、弯道迟滞，不要只盯一个环，先确认前一级目标是不是已经不合理。

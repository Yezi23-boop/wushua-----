# 主控制环 2ms 迁移设计

## 背景

当前工程使用两个 PIT 周期：

```text
TIME_0 = 5ms   主控制环 run_time_1()
TIME_1 = 10ms  状态环、按键、菜单和软定时器
```

`run_time_1()` 承担电感采样、编码器读取、IMU 更新、转向外环、元素仲裁、角速度环、速度环和电机输出。为了提高高速场景下的识别和控制精度，本次目标是把主控制环从 5ms 提升到 2ms，同时保持 10ms 状态环不变。

## 目标

- 将 `TIME_0` 从 5ms 改为 2ms。
- `TIME_1` 保持 10ms，避免按键、菜单、软定时器、堵转和电压检测被无意义加速。
- 速度环、角速度环和特殊元素识别随主环提升到 2ms。
- 转向外环从当前约 10ms 改为约 6ms，也就是 2ms 主环每 3 拍更新一次。
- 所有依赖 5ms 调用次数表达真实时间的计数，按 2ms 周期等效换算。
- 采用保守控制参数，先保证高速稳定性，再现场逐步提高响应。

## 非目标

- 不把 `TIME_1` 改成 2ms。
- 不重构主控制链路结构。
- 不新增 EEPROM 参数槽位。
- 不在中断路径增加串口打印、菜单操作或 I2C 写入。
- 不同时大幅改变圆环、圆桶、墙面和飞坡的识别阈值。

## 主控制环设计

`TIME_0` 改为 2ms 后，主链路仍保持现有顺序：

```text
read_AD()
Encoder_get()
imu_update_gyro_z_from_imu660rc()
pid_steer_update()  每 3 拍一次
a_run_track_element_update_gate()
pid_angle_update()
pid_speed_update()
motor_output()
```

转向外环分频从 `>= 2` 改为 `>= 3`。在 2ms 主环下，这表示约 6ms 更新一次外环。速度环和角速度环保持每个主环周期更新。

## 参数换算

### 编码器与速度环

编码器采样周期从 5ms 改为 2ms，单位时间速度换算需要乘以 `5 / 2 = 2.5`：

```text
Encoder_get() 速度比例: 0.07 -> 0.175
编码器低通 alpha: 0.5 -> 0.25
速度环 Ki: 25 -> 10
```

`Kp` 第一版保持不变，`Kd` 当前为 0，不需要同步换算。

### 转向与角速度环

转向外环从 10ms 改为 6ms，微分项会更频繁读取电感误差。第一版使用保守值：

```text
kd_Err: 8 -> 10
kd_Angle: 0.40 -> 0.70
```

`kp_Err`、`kp_Angle` 和限幅参数第一版保持不变，避免控制链路一次性变化过多。

### 圆环

圆环的里程积分必须按真实时间换算：

```text
ring_data.encoder += (speed_l + speed_r) * 0.005
改为
ring_data.encoder += (speed_l + speed_r) * 0.002
```

入口连续确认保持约 15ms：

```text
RING_ENTRY_CONFIRM_COUNT: 3 -> 8
```

圆环 yaw 累计不修改全局 `gyro_z` 输出量纲，只在圆环积分路径按 2ms 做等效修正。这样角速度控制链路和圆环角度累计可以分别调试，避免一个缩放同时影响两类行为。

### 圆桶

圆桶状态机由主环推进，所有 5ms 计数按 `* 2.5` 换算：

```text
CYLINDER_TOP_WINDOW_COUNT: 100 -> 250
CYLINDER_TOP_HIT_COUNT: 3 -> 8
CYLINDER_GROUND_CONFIRM_COUNT: 3 -> 8
CYLINDER_STABLE_DELAY_COUNT: 100 -> 250
```

强信号阈值保持不变。

### 墙面

墙面下墙计时保持约 1000ms：

```text
WALL_TIMING_COUNT: 200 -> 500
```

墙面电感阈值保持不变。

### 飞坡和跷跷板

飞坡计数由主环推进，按 2ms 周期等效换算：

```text
count_fly_time_1 默认值: 3 -> 8
count_fly_time_2 默认值: 30 -> 75
FLY_RECOVER_LINE_STABLE_COUNT: 10 -> 25
FLY_RECOVER_PWM_LIMIT_EARLY_COUNT: 60 -> 150
FLY_RECOVER_LOST_LINE_ENABLE_COUNT: 200 -> 500
```

`FLY_RELEASE_SPEED_STEP` 第一版保持 1。如果现场发现落地后速度恢复过快，再改为隔拍释放或降低释放步长。

### 电机起步斜坡

电机输出从 5ms 一次变为 2ms 一次，起步斜坡步长按 `* 0.4` 换算：

```text
MOTOR_START_PWM_RAMP_STEP: 120 -> 48
```

堵转检测仍在 10ms 状态环中执行，`MOTOR_STALL_CONFIRM_COUNT` 不改。

## 保持不变的 10ms 逻辑

以下逻辑仍由 `TIME_1 = 10ms` 驱动，不随本次主环改动：

```text
Keystroke_Scan_10ms()
Menu_Tick_10ms()
soft_timer_update_10ms()
a_run_mode_update_start_state()
lost_lines()
dianya_jiance()
motor_stall_check_10ms()
```

对应的按键长按、菜单节拍、软件定时器、堵转确认、电压保护和丢线确认计数不做换算。

## 风险与验证

主要风险：

- 2ms 中断负载增加，`read_AD()` 多次采样和排序可能成为主要耗时。
- 编码器单周期计数减少，速度反馈量化噪声会变大。
- 速度环更新变快后，积分项过大可能导致 PWM 抖动。
- 转向外环 6ms 后，`kd_Err` 过高会放大电感噪声。

验证顺序：

1. 源码测试覆盖关键常量和换算关系。
2. 若本机 Keil 可用，执行完整工程编译。
3. 上车先低速验证编码器速度显示、Err、左右目标速度和 PWM 输出无异常。
4. 依次验证普通循迹、圆桶、墙面、飞坡和圆环。
5. 若出现速度环抖动，优先下调速度环 `Ki` 或编码器低通 `alpha`。
6. 若出现转向摆振，优先下调 `kd_Err` 或 `kd_Angle`。

## 第一版落地参数

```text
TIME_0 = 2
TIME_1 = 10
steer_div >= 3
encoder scale = 0.175
encoder alpha = 0.25
speed Ki = 10
kd_Err = 10
kd_Angle = 0.70
RING_ENTRY_CONFIRM_COUNT = 8
CYLINDER_TOP_WINDOW_COUNT = 250
CYLINDER_TOP_HIT_COUNT = 8
CYLINDER_GROUND_CONFIRM_COUNT = 8
CYLINDER_STABLE_DELAY_COUNT = 250
WALL_TIMING_COUNT = 500
count_fly_time_1 = 8
count_fly_time_2 = 75
FLY_RECOVER_LINE_STABLE_COUNT = 25
FLY_RECOVER_PWM_LIMIT_EARLY_COUNT = 150
FLY_RECOVER_LOST_LINE_ENABLE_COUNT = 500
MOTOR_START_PWM_RAMP_STEP = 48
```

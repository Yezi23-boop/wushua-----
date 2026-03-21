# Speed Loop Autotune Protocol

本专题继续沿用 VOFA FireWater ASCII 协议，以 `!` 作为命令结束符。

## 上位机常用命令

- `AT_KP=<value>`
- `AT_KI=<value>`
- `AT_KD=<value>`
- `TEST_speed=<value>`
- `AT_SPEED=<value>`
- `AT_FUYA=<value>`
- `AT_TRIAL_MS=<value>`
- `AT_COOLDOWN_MS=<value>`
- `START`
- `STOP`
- `AT_ARM`
- `AT_FIRE`
- `AT_RESET`
- `INFO`

## 当前专题遥测格式

每帧输出 7 列，顺序保持兼容：

1. `target`
2. `left_speed`
3. `right_speed`
4. `left_pwm`
5. `right_pwm`
6. `trial_active`
7. `stop_flag`

输出示例：

```text
35.000000,34.200001,34.000000,3200,3180,1,0
```

## 模块边界

- `project/service/vofa.c`：通用帧解析壳层
- `project/speed_loop_autotune/firmware/host_autotune_command.*`：专题命令处理
- `project/speed_loop_autotune/firmware/host_service.*`：专题命令轮询与遥测输出
- `project/speed_loop_autotune/firmware/speed_loop_trial.*`：速度环试验运行时

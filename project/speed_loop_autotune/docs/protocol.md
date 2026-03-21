# Speed Loop Autotune Protocol

本专题继续沿用 VOFA FireWater ASCII 协议，以 `!` 作为命令结束符。

## 模式区分

- `air-dual`
  - 旧别名：`autotune`
  - 作用：架空双轮速度环调参与双轮联合微调

- `ground-dual`
  - 旧别名：`ground-load`
  - 作用：带负载双轮试验、带冷却节拍的整组回归

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

## 架空双轮命令链

- 主入口：`START`
- 速度切换：`TEST_speed=<value>`
- 结束特征：尾部补发 `TEST_speed=0`
- 典型用途：架空双轮、单轮隔离、双轮联合微调

## 带负载双轮命令链

- 主入口：`AT_ARM`
- 发车：`AT_FIRE`
- 目标速度：`AT_SPEED=<value>`
- 试验时长：`AT_TRIAL_MS=<value>`
- 冷却：`AT_COOLDOWN_MS=<value>`
- 负压：`AT_FUYA=<value>`
- 典型用途：带负载双轮整组回归、停车与残余 PWM 检查

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

## 阅读建议

- 架空双轮先读：`docs/tuning_rules.md` 与 `docs/debug_memory.md`
- 带负载双轮先读：`docs/ground_dual_tuning_rules.md` 与 `docs/ground_dual_debug_memory.md`

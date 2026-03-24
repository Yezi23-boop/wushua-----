# Speed Loop Autotune Protocol

专题继续使用 VOFA FireWater ASCII 协议，命令以 `!` 结尾。

## 模式

- `air-dual`
  - 旧别名：`autotune`
  - 用途：架空双轮闭环速度环调参与双轮联合微调

- `ground-dual`
  - 旧别名：`ground-load`
  - 用途：带负载双轮回归、停车质量和残余输出验证

- `pwm-identify`
  - 无旧别名
  - 用途：架空单轮开环 PWM 阶跃辨识，只产出种子 PID

## 共享命令

- `START`
- `STOP`
- `AT_RESET`
- `INFO`
- `L_KP=<value>`
- `L_KI=<value>`
- `L_KD=<value>`
- `R_KP=<value>`
- `R_KI=<value>`
- `R_KD=<value>`
- `AT_KP=<value>`
- `AT_KI=<value>`
- `AT_KD=<value>`

## `air-dual` 命令

- `TEST_speed=<value>`
- `START`
- `AT_RESET`

## `ground-dual` 命令

- `AT_SPEED=<value>`
- `AT_FUYA=<value>`
- `AT_TRIAL_MS=<value>`
- `AT_COOLDOWN_MS=<value>`
- `AT_ARM`
- `AT_FIRE`

## `pwm-identify` 命令

- `AT_TEST_MODE=0|1`
  - `0`：闭环 `TEST_speed`
  - `1`：开环 PWM 辨识
- `L_TEST_PWM=<value>`
- `R_TEST_PWM=<value>`
- `TEST_pwm=<value>`

## Telemetry

每帧输出 10 列，前 7 列兼容旧脚本：
1. `target`
2. `left_speed`
3. `right_speed`
4. `left_pwm`
5. `right_pwm`
6. `trial_active`
7. `stop_flag`
8. `mode_id`
  - `0`：`air-dual`
  - `1`：`ground-dual`
  - `2`：`pwm-identify`
9. `left_cmd_pwm`
10. `right_cmd_pwm`

示例：

```text
35.000000,34.200001,34.000000,3200,3180,1,0,0,0,0
0.000000,12.000000,0.000000,600,0,0,0,2,600,0
```

## Firmware Portability Boundary

- `firmware/speed_loop_autotune.h`
  - 板层唯一 public 头，外部适配文件只通过它看到绑定类型、端口类型和初始化入口
- `firmware/speed_loop_autotune_private.h`
  - 组件内部 private 头，非 `speed_loop_autotune` 内部文件不应直接依赖
- `firmware/autotune_token_registry.*`
  - 只负责 VOFA token 到处理函数的静态注册
- `firmware/autotune_binding.*`
  - 只负责 6 个 PID 参数成员的注册、校验、读写和镜像
- `firmware/autotune_port.*`
  - 只负责板级速度采样、PWM 输出、负压输出和启停状态控制
- `firmware/autotune_component.*`
  - 承载三种模式共用的组件核心，对内管理内部 PID 核和 telemetry 缓存
- `service/speed_loop_autotune_adapter.*`
  - 是当前项目的组件外接入点，负责把外部 PID 成员和板级端口注册进 `speed_loop_autotune`
## pwm-map

`pwm-map` does not add new firmware tokens. It reuses the existing open-loop PWM path:

- `AT_RESET`
- `AT_TEST_MODE=1`
- `L_TEST_PWM=<value>` / `R_TEST_PWM=<value>`
- `START`
- tail-zero by sending the tested wheel back to `0`
- `AT_TEST_MODE=0`
- `AT_RESET`

CSV columns:

- `timestamp`
- `wheel`
- `pwm_command`
- `steady_encoder`
- `peak_encoder`
- `sample_count`
- `repeat_count`
- `stop_flag_seen`
- `deadzone_break_pwm`

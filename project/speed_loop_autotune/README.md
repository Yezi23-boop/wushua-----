# Speed Loop Autotune

这个专题目录集中管理速度环上位机、专题固件接口、调参规则和调试记忆。

目录约定：
- `host/`：上位机 Python 脚本
- `firmware/`：专题固件命令、模式调度、运行时和组件抽象
- `docs/`：协议、模式规则、调试记忆
- `tests/`：上位机单元测试和源码结构检查

兼容入口继续保留：
- `tools/vofa_autotune.py`
- `tests/test_vofa_autotune.py`

## 三种正式模式

### `air-dual`
- 含义：架空双轮闭环速度环调参与双轮联合微调
- 旧别名：`autotune`
- 主要命令：`START`、`TEST_speed=<value>`
- 关注点：多段目标速度响应、尾部回零、双轮高 PWM 速度下坠

### `ground-dual`
- 含义：带负载双轮闭环回归与停车质量验证
- 旧别名：`ground-load`
- 主要命令：`AT_ARM`、`AT_FIRE`、`AT_SPEED=<value>`
- 关注点：预充、冷却、停车回零、残余 PWM、带载稳定性

### `pwm-identify`
- 含义：架空单轮开环 PWM 阶跃辨识，只产出左右轮种子 PID
- 无旧别名
- 主要命令：`AT_TEST_MODE=1`、`L_TEST_PWM=<value>`、`R_TEST_PWM=<value>`、`TEST_pwm=<value>`
- 关注点：死区突破、阶跃稳态速度、`k/theta/tau` 估计、SIMC 种子 PI

## Host 边界

- `host/common.py`
  - 共享串口客户端、telemetry 解析、PID 数据结构和增益下发
- `host/air_dual.py`
  - `air-dual` 的闭环调参与双轮微调
- `host/ground_dual.py`
  - `ground-dual` 的带载回归和评分
- `host/pwm_identify.py`
  - `pwm-identify` 的开环辨识和种子 PI 生成
- `host/vofa_autotune.py`
  - 只保留 CLI、参数校验和模式路由

## Firmware Portability

- `firmware/autotune_component.*`
  - 组件核心，统一管理 `target / feedback / output` 缓存，并调度内部 PID 核
- `firmware/speed_loop_autotune.h`
  - 组件对板层暴露的唯一 public 头，板层初始化只需要 include 这一份
- `firmware/speed_loop_autotune_private.h`
  - 组件内部 private 头，供专题固件内部文件共享更多内部接口
- `firmware/autotune_binding.*`
  - 6 个速度环增益成员的编译期注册、校验、读写和镜像
- `firmware/autotune_port.*`
  - 板级端口抽象，统一封装速度采样、电机 PWM、负压输出和启停状态
- `firmware/autotune_pid_core.*`
  - 内部增量式速度 PID，第一版与当前 `service/pid.c` 公式对齐
- `firmware/autotune_token_registry.*`
  - 只负责 VOFA token 到处理函数的静态注册，不再承担控制逻辑或 PID 成员绑定
- `firmware/air_dual_mode.*`
  - `air-dual` 命令与闭环执行
- `firmware/ground_dual_mode.*`
  - `ground-dual` 命令、发车、冷却和带载执行
- `firmware/pwm_identify_mode.*`
  - `pwm-identify` 命令和开环 PWM 输出
- `firmware/autotune_runtime.*`
  - 共享运行态、模式切换、统一复位和 telemetry mode id
- `firmware/host_autotune_command.*`
  - 专题命令解析和 `INFO` 输出
- `firmware/speed_loop_trial.*`
  - 只保留 5ms dispatcher

- `service/speed_loop_autotune_adapter.*`
  - 当前项目的组件外注册点，负责参数成员绑定、板级端口组装和显式初始化

移植到别的车或别的底层项目时，优先替换：
- `service/speed_loop_autotune_adapter.*`

## 当前 Telemetry

保持前 7 列兼容，并在尾部追加 3 列：
1. `target`
2. `left_speed`
3. `right_speed`
4. `left_pwm`
5. `right_pwm`
6. `trial_active`
7. `stop_flag`
8. `mode_id`
9. `left_cmd_pwm`
10. `right_cmd_pwm`

## 阅读建议

- `air-dual`：先看 `docs/tuning_rules.md` 和 `docs/debug_memory.md`
- `ground-dual`：先看 `docs/ground_dual_tuning_rules.md` 和 `docs/ground_dual_debug_memory.md`
- `pwm-identify`：先看 `docs/pwm_identify_rules.md` 和 `docs/pwm_identify_debug_memory.md`

# Speed Loop Autotune

这个专题目录集中管理速度环自动调参相关的上位机脚本、专题固件接口、规则文档和调试记忆。

## 目录约定

- `host/`
  - 上位机 Python 脚本
- `firmware/`
  - 专题固件命令、模式调度、运行时和组件抽象
- `docs/`
  - 协议、模式规则、调试记忆、阶段经验
- `tests/`
  - 上位机单元测试和源码结构检查

兼容入口继续保留：
- `tools/vofa_autotune.py`
- `tests/test_vofa_autotune.py`

## 四种正式模式

### `pwm-map`

- 含义：
  - 开环 `PWM -> 编码器速度` 标定模式
- 作用：
  - 从 `0 PWM` 开始测左右轮死区
  - 生成左右轮 `PWM -> 编码器速度` 映射
  - 更新共享 profile，供后续阶段直接使用
- 主要命令：
  - `AT_TEST_MODE=1`
  - `L_TEST_PWM=<value>`
  - `R_TEST_PWM=<value>`
  - `START`
  - `AT_RESET`
- 关注点：
  - 电机死区
  - 稳态编码器值
  - 左右轮开环差异
  - 原始 CSV 与共享 profile 是否一致

### `pwm-identify`

- 含义：
  - 架空单轮开环 PWM 阶跃辨识，只产出左右轮种子 PID
- 作用：
  - 读取 `pwm-map` 已经测出的死区
  - 从死区之上的识别档位起步
  - 估计 `k / theta / tau`
  - 生成左右轮种子 `PI`
- 主要命令：
  - `AT_TEST_MODE=1`
  - `L_TEST_PWM=<value>`
  - `R_TEST_PWM=<value>`
  - `TEST_pwm=<value>`
- 关注点：
  - 已知死区之上的阶跃稳态速度
  - `k / theta / tau` 估计
  - `SIMC` 种子 `PI`

### `air-dual`

- 含义：
  - 架空双轮闭环速度环调参与双轮联合微调
- 旧别名：
  - `autotune`
- 作用：
  - 读取共享 profile 里的真实编码器目标
  - 读取 `pwm-identify` 产出的种子 `PI`
  - 在架空条件下完成左右轮闭环微调
- 主要命令：
  - `START`
  - `TEST_speed=<value>`
- 关注点：
  - 多段目标速度响应
  - 尾部回零
  - 双轮高 PWM 下的速度下坠
  - 左右轮联合稳定性

### `ground-dual`

- 含义：
  - 带负载双轮闭环回归与停车质量验证
- 旧别名：
  - `ground-load`
- 作用：
  - 读取共享 profile 里的真实地面目标
  - 优先继承 `air-dual` 的最佳结果作为起点
  - 在下地条件下做回归验证与最终调参
- 主要命令：
  - `AT_ARM`
  - `AT_FIRE`
  - `AT_SPEED=<value>`
- 关注点：
  - 预充
  - 冷却
  - 停车回零
  - 残余 PWM
  - 带载稳定性

## 共享 Profile

当前标准交接文件：
- `project/speed_loop_autotune/logs/current_tuning_profile.json`

它用于把 4 个阶段串起来，当前主要保存：
- `pwm_map`
  - 左右轮死区
  - 左右轮 `PWM -> 编码器速度` 映射
- `shared_targets`
  - 按左右轮较小上限推导的真实目标值
  - `air-dual` / `ground-dual` 默认序列
  - 允许额外定义 `custom_sequences`
    - 有自定义序列时优先使用自定义序列
    - 没有自定义序列时回退到 `default_sequences`
    - 当前全流程默认模板固定为 `bands.low/mid`
    - 默认不会直接跑到 `bands.high/top`
- `pwm_identify`
  - 左右轮种子 `PI`
  - 有效识别档位
- `air_dual`
  - 架空阶段基线 PID、最佳 PID 和摘要结果
- `ground_dual`
  - 下地阶段基线 PID、最佳 PID 和摘要结果

## 标准调参流程

### 第 1 步：先运行 `pwm-map`

- 输出原始 CSV
- 更新共享 profile
- 记录左右轮死区
- 记录左右轮各档 PWM 对应的编码器速度

### 第 2 步：运行 `pwm-identify`

- 读取共享 profile
- 直接使用 `pwm-map` 已记录的死区结果
- 从死区之上的第一个识别档位开始辨识
- 把左右轮种子 `PI` 回写到共享 profile

### 第 3 步：运行 `air-dual`

- 从共享 profile 读取真实编码器目标
- 若 `shared_targets.custom_sequences` 中存在 `air_primary` 或 `air_verify`，优先使用自定义值
- 不再把旧的 `15 / 25 / 35 / 45` 直接当成真实目标
- 优先使用 `pwm-identify.seed_pi` 作为初始 PID
- 把架空阶段最佳结果回写到共享 profile
- 若没有自定义序列，则默认按低中速模板运行：
  - `air_primary = [low, mid, mid, low, low]`
  - `air_verify = [low, mid, mid, low, low]`

### 第 4 步：运行 `ground-dual`

- 从共享 profile 读取真实地面目标
- 若 `shared_targets.custom_sequences` 中存在 `ground_forward`，优先使用自定义值
- 优先使用 `air-dual` 最佳 PID 作为起点
- 把最终下地结果回写到共享 profile
- 若没有自定义序列，则默认按低中速模板运行：
  - `ground_forward = [low, mid, low]`

## Host 边界

- `host/common.py`
  - 共享串口客户端、telemetry 解析、PID 数据结构、profile 读写和公共工具
- `host/pwm_map.py`
  - `pwm-map` 的开环标定、CSV 输出和 profile 写回
- `host/pwm_identify.py`
  - `pwm-identify` 的开环辨识和种子 `PI` 生成
- `host/air_dual.py`
  - `air-dual` 的闭环调参与双轮微调
- `host/ground_dual.py`
  - `ground-dual` 的带载回归和评分
- `host/vofa_autotune.py`
  - 统一 CLI、参数校验和模式路由

## 固件可移植边界

- `firmware/autotune_component.*`
  - 组件核心，统一管理 `target / feedback / output` 缓存，并调度内部 PID 核
- `firmware/speed_loop_autotune.h`
  - 组件对板层暴露的唯一 public 头
- `firmware/speed_loop_autotune_private.h`
  - 组件内部 private 头
- `firmware/autotune_binding.*`
  - 6 个速度环增益成员的编译期注册、校验、读写和镜像
- `firmware/autotune_port.*`
  - 板级端口抽象，统一封装速度采样、电机 PWM、负压输出和启停状态
- `firmware/autotune_pid_core.*`
  - 内部增量式速度 PID
- `firmware/autotune_token_registry.*`
  - VOFA token 到处理函数的静态注册
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

## 已确认的 PWM 范围

- 轮子电机 PWM 命令使用真实的 `0~10000` 范围
- `air-dual`、`ground-dual`、`pwm-identify` 和 `pwm-map` 都按这个轮子 PWM 范围理解
- `AT_FUYA` 是单独的负压命令，仍然限制在 `0~4000`
- `2026-03-24` 在 `COM8` 上的实机验证已经确认：刷新新固件后，轮子开环命令 `5000`、`7000`、`9000` 都是作为真实 PWM 生效的

## 阅读建议

- `pwm-map`
  - 先看 `docs/pwm_map_rules.md`
- `pwm-identify`
  - 先看 `docs/pwm_identify_rules.md` 和 `docs/pwm_identify_debug_memory.md`
- `air-dual`
  - 先看 `docs/tuning_rules.md` 和 `docs/debug_memory.md`
- `ground-dual`
  - `docs/ground_dual_tuning_rules.md` / `docs/ground_dual_debug_memory.md`

## custom_sequences Manual Edit

- path:
  - `project/speed_loop_autotune/logs/current_tuning_profile.json`
- keys:
  - `shared_targets.custom_sequences.air_primary`
  - `shared_targets.custom_sequences.air_verify`
  - `shared_targets.custom_sequences.ground_forward`
- recommended text format:
  - `"30:500,60:500,90:500,120:500,90:500,60:500,30:500"`
- priority:
  - use `custom_sequences` first
  - fallback to `default_sequences` when custom value is missing
  - current default template uses `bands.low/mid` only
  - 先看 `docs/ground_dual_tuning_rules.md` 和 `docs/ground_dual_debug_memory.md`

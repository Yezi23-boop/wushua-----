# Speed Loop Autotune

这个专题目录集中管理“上位机反复试验速度环 + 固件试验接口 + 调参规则与调试记忆”。

目录约定：
- `host/`：上位机 Python 脚本真实实现
- `firmware/`：固件侧命令处理、试验状态机、专题服务入口
- `docs/`：协议、评分、调参规则、调试记忆
- `tests/`：上位机脚本专题测试

兼容入口继续保留：
- `tools/vofa_autotune.py`
- `tests/test_vofa_autotune.py`

调试前默认先读：
- `docs/tuning_rules.md`
- `docs/debug_memory.md`

## 模式区分

当前专题明确拆成两条线，避免架空双轮和带负载双轮混淆：

- `air-dual`
  - 含义：架空双轮速度环联调
  - 旧别名：`autotune`
  - 上位机入口：`run_air_dual_autotune()`
  - 命令特征：`START + TEST_speed`
  - 关注点：多速度段响应、尾部回零、双轮同跑时的高 PWM 速度下坠

- `ground-dual`
  - 含义：带负载双轮试验与回归
  - 旧别名：`ground-load`
  - 上位机入口：`run_ground_dual_autotune()`
  - 命令特征：`AT_ARM + AT_FIRE + AT_SPEED`
  - 关注点：预充、冷却、停车回零、残余 PWM、实际载荷下的收敛

## 代码边界

- `host/vofa_autotune.py`
  - `run_air_dual_autotune()`：架空双轮调参主入口
  - `run_ground_dual_autotune()`：带负载双轮调参主入口
  - `score_dual_wheel_multi_speed_trial()`：架空双轮评分
  - `_score_ground_load_trial()`：带负载双轮评分

- `firmware/host_autotune_command.*`
  - 两种模式共用的参数命令入口

- `firmware/host_service.*`
  - 两种模式共用的命令轮询和遥测输出

- `firmware/speed_loop_trial.*`
  - 架空双轮：`START/TEST_speed`
  - 带负载双轮：`AT_ARM/AT_FIRE/AT_SPEED/AT_COOLDOWN_MS`

## 文档分工

- `docs/tuning_rules.md`
  - 架空双轮默认调参方法
  - 包含速度序列、隔离方法、异常样本处理、调参顺序、验收标准
  - 新对话默认优先沿用这里的规则

- `docs/debug_memory.md`
  - 架空双轮调试记忆
  - 先写已验证事实，再写推断
  - 只有重复验证过的结论，才升级回 `tuning_rules.md`

- `docs/ground_dual_tuning_rules.md`
  - 带负载双轮规则与默认流程
  - 只记录 `ground-dual` 相关经验

- `docs/ground_dual_debug_memory.md`
  - 带负载双轮调试记忆
  - 后续实车或带载复验优先写这里

## 当前默认原则

- 端口默认 `COM8`
- 模式默认架空 `air-dual`
- 负压默认关闭
- 左右轮允许使用不同 PID
- 单轮调试时，另一侧直接设为 `0/0/0`
- `miss` 当前默认先忽略，不直接当作 PID 差
- 编码器速度绝对值大于 `200` 时，默认视为异常样本

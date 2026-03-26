# Speed Loop Agent Autotune Design

**Date:** 2026-03-26

## 背景

当前仓库已经具备较完整的速度环自动调参主链：

- `pwm_map`：摸底左右轮 PWM 死区和速度映射
- `pwm_identify`：根据建图结果生成第一组 `seed_pi`
- `air_dual`：架空双轮速度环搜索
- `ground_dual`：带载最终搜索

现状的问题不是“没有自动调参能力”，而是：

- 调参逻辑主要仍由脚本内部固定流程驱动
- 用户需要频繁介入决定何时继续、何时切阶段
- `air_dual` 和 `ground_dual` 还不具备“Agent 逐轮思考并决定下一轮 PID”的统一接口
- profile 和日志虽然已经记录了结果，但还不足以支撑一个可恢复、可解释的 Agent 闭环

本设计把当前方案升级为“**大模型 Agent 负责调参决策，现有 Python 脚本负责执行试验**”的半自动到自动过渡架构。

## 目标

- 提供一个新的“调参指挥官”skill，作为唯一用户入口。
- 让 Agent 自动连续完成 `pwm_map -> pwm_identify`。
- 让 Agent 在 `air_dual` 和 `ground_dual` 内部连续跑 10 轮 PID 候选试验，并在每轮后基于结构化结果决定下一轮 PID。
- 10 轮结束后再把批次结果交给用户决定是否继续、切换阶段或保存。
- 保持现有 worker 脚本职责单一，不把 LLM 逻辑硬塞进 `air_dual.py` 或 `ground_dual.py`。
- 强化 profile 和日志契约，使会话可恢复、决策可回放、结果可解释。

## 非目标

- 本版本不做无人值守全自动闭环到最终保存。批次结束后仍保留用户确认。
- 本版本不以原始波形自由理解作为主要优化依据，优先使用结构化指标。
- 本版本不重写固件协议，不改动 VOFA 遥测列定义，不引入新的 MCU 端复杂状态机。
- 本版本不把 `pwm_map` 和 `pwm_identify` 拆成逐轮 Agent 搜索器。

## 总体架构

系统拆成四层：

1. **调参指挥官 skill**
   - 唯一用户入口
   - 负责前置检查、阶段判断、worker 调用、读取结果、生成建议、请求用户确认

2. **Agent 决策器**
   - 负责在 `air_dual` 和 `ground_dual` 的 10 轮批次内逐轮生成下一轮 PID
   - 只在受限动作空间内决策，不直接自由输出任意 PID

3. **worker 脚本**
   - `pwm_map`
   - `pwm_identify`
   - `air_dual-step`
   - `ground_dual-step`
   - worker 只负责执行单轮或单阶段试验，并返回结构化结果

4. **状态与历史层**
   - `current_tuning_profile.json`：长期状态源
   - `tuning_history_summary.csv`：候选摘要历史
   - `tuning_history_segments.csv`：分段指标历史
   - `agent_decision_trace.jsonl`：Agent 每轮决策轨迹
   - `logs/agent_rounds/*.json`：每轮详细结果

这四层的边界是：

- skill 决定“下一步做什么”
- Agent 决定“下一轮 PID 怎么改”
- worker 决定“如何执行这一轮测试并评分”
- profile/log 决定“如何持久化状态并支持恢复”

## 主流程状态机

每次 skill 启动后，按以下顺序执行：

1. 检查串口、profile、日志目录、固件模式、必要参数
2. 若缺 `shared_targets` 或 `pwm_map` 关键字段，则自动运行 `pwm_map`
3. 若缺 `pwm_identify.seed_pi`，则自动运行 `pwm_identify`
4. 若当前仍处于架空调参阶段，则进入或恢复 `air_dual`
5. `air_dual` 每批自动执行 10 轮，批末等待用户确认
6. 用户执行 `enter_ground` 后，才进入 `ground_dual`
7. `ground_dual` 每批自动执行 10 轮，批末等待用户确认
8. 用户执行 `save` 后，才把当前结果视为本轮最终收敛

阶段切换规则固定为：

- `pwm_map` 和 `pwm_identify` 完全自动，不中断用户
- `air_dual` 未被用户显式结束前，不自动进入 `ground_dual`
- `ground_dual` 未被用户显式保存前，不自动覆盖最终最佳值

## Skill 编排职责

调参指挥官 skill 的固定职责是：

1. 检查前置条件
2. 判断当前阶段
3. 调用一个 worker
4. 读取结构化结果
5. 生成建议并请求用户确认

交互协议固定为“**先建议，再允许动作词覆盖**”：

- skill 先输出默认建议和理由
- 用户通过明确动作词确认
- skill 不依赖开放式自然语言推断用户意图

推荐动作词集合：

- `continue_air`
- `enter_ground`
- `stop_air`
- `continue_ground`
- `save`
- `stop_without_save`

每次批末 skill 输出三段固定信息：

- `Current stage`
- `What I observed`
- `Recommended action`

## Worker 职责与接口

### `pwm_map`

- 职责不变，继续负责死区和速度映射
- 缺失建图结果时由 skill 自动调用
- 结果继续写入 profile 的 `pwm_map`

### `pwm_identify`

- 职责不变，继续负责生成 `seed_pi`
- 缺失 `seed_pi` 时由 skill 自动调用
- 结果继续写入 profile 的 `pwm_identify.seed_pi`

### `air_dual-step`

- 新增单轮执行模式
- 不负责批内 10 轮循环
- 不负责决定下一轮 PID
- 不负责在批末询问继续/停止
- 单轮默认不写最终 `air_dual.best_pid`
- 新批次起点优先级固定为：
  1. `air_dual.active_batch.current_best_pid`
  2. `air_dual.last_batch_best.best_pid`
  3. `air_dual.best_pid`
  4. `pwm_identify.seed_pi`
  5. CLI 默认值

### `ground_dual-step`

- 新增单轮执行模式
- 与 `air_dual-step` 相同，只负责单轮带载验证/优化
- 单轮默认不写最终 `ground_dual.best_pid`
- 新批次起点优先级固定为：
  1. `ground_dual.active_batch.current_best_pid`
  2. `ground_dual.best_pid`
  3. `air_dual.best_pid`
  4. `pwm_identify.seed_pi`
  5. CLI 默认值

## 单轮 Worker 输入契约

`air_dual-step` 和 `ground_dual-step` 都接收以下输入：

- `mode`
- `profile_path`
- `candidate_pid`
- `baseline_pid`
- `batch_id`
- `round_index`
- `sequence_policy`
- `score_config`
- `save_policy`

其中：

- `candidate_pid` 为本轮要试验的完整左右轮 PID
- `baseline_pid` 为当前批起点，用于对比
- `round_index` 范围固定为 `1..10`
- `save_policy` 在单轮模式下固定为“只记运行记录，不写最终 best”

## 单轮 Worker 输出契约

每轮必须生成稳定的机器可读结果，优先写 JSON 文件，同时允许 stdout 在末尾附带同内容摘要。

每轮至少输出以下字段：

- `mode`
- `batch_id`
- `round_index`
- `candidate_pid`
- `baseline_pid`
- `a_score`
- `b_score`
- `combined_score`
- `left_score`
- `right_score`
- `band_scores`
- `stage_reached`
- `overshoot_flag`
- `persistent_overshoot_flag`
- `speed_drop_flag`
- `stop_clean_flag`
- `pwm_saturation_ratio`
- `recover_from_saturation_ms`
- `current_limit_or_headroom_flag`
- `dominant_issue`
- `decision_hints`
- `summary_path`
- `raw_segment_path`
- `timestamp`

其中 `combined_score` 是统一的唯一优化目标，用于：

- Agent 排序候选
- 记录每轮最优
- 判断批内是否改善
- 生成批末建议
- 回写 profile 摘要

`band_scores` 至少覆盖：

- `low`
- `mid`
- `high`
- `top`

用于防止 Agent 只在单一速度点上追求最优而牺牲其他速度带表现。

### `dominant_issue`

允许的值：

- `slow_response`
- `steady_error`
- `overshoot`
- `left_right_mismatch`
- `plateau`
- `noisy_measurement`
- `saturation_limited`

### `decision_hints`

允许的值是一个字符串数组，成员来自：

- `prefer_raise_kp`
- `prefer_lower_kp`
- `prefer_raise_ki`
- `prefer_lower_ki`
- `consider_kd`
- `keep_nearby_verify`
- `rebalance_left`
- `rebalance_right`
- `rollback`

## Profile 扩展设计

`current_tuning_profile.json` 继续作为单一长期状态源，但不保存每轮所有原始细节。

### 顶层新增 `agent_tuning`

```json
"agent_tuning": {
  "session_id": "20260326_190500",
  "workflow_stage": "air_dual",
  "workflow_status": "running",
  "current_batch_id": "air_0003",
  "current_round_index": 6,
  "auto_completed_stages": ["pwm_map", "pwm_identify"],
  "pending_user_action": null,
  "last_worker_mode": "air-dual-step",
  "last_result_path": "logs/agent_rounds/air_0003_r06.json",
  "last_decision_trace_path": "logs/agent_decision_trace.jsonl"
}
```

### `air_dual` 新增/固定字段

- `baseline_pid`
- `best_pid`
- `last_summary`
- `last_batch_best`
- `batch_round`
- `last_batch_summary`
- `active_batch`

其中 `last_summary` 和 `last_batch_summary` 都应包含：

- `a_score`
- `b_score`
- `combined_score`
- `band_scores`
- `autotune_sequence`
- `verify_sequence`

`active_batch` 的目标结构：

```json
"active_batch": {
  "batch_id": "air_0003",
  "start_pid": {"left": {}, "right": {}},
  "current_best_pid": {"left": {}, "right": {}},
  "current_best_score": 123.4,
  "rounds_completed": 6,
  "search_phase": "shrink",
  "last_round_pid": {"left": {}, "right": {}},
  "last_round_score": 130.2,
  "last_round_result_path": "logs/agent_rounds/air_0003_r06.json"
}
```

### `ground_dual` 新增/固定字段

- `baseline_pid`
- `best_pid`
- `last_summary`
- `last_batch_best`
- `batch_round`
- `last_batch_summary`
- `active_batch`

其中 `last_summary` 和 `last_batch_summary` 都应包含：

- `combined_score`
- `band_scores`
- `trial_name`
- `segments_ms`

### 批末等待用户时的挂起状态

```json
"pending_user_action": {
  "stage": "air_dual",
  "batch_id": "air_0003",
  "recommended_action": "continue_air",
  "allowed_actions": ["continue_air", "enter_ground", "stop_air"],
  "reason": "最近 3 轮仍在改善，但步长已收缩，建议再跑一批确认平台区"
}
```

## 日志与历史策略

- profile 只保留当前状态和最近批次摘要
- 每轮详细结果写入 `logs/agent_rounds/`
- Agent 每轮决策单独写 `agent_decision_trace.jsonl`
- `tuning_history_summary.csv` 和 `tuning_history_segments.csv` 继续保留，供趋势回看

这样设计的原因是：

- profile 仍然轻量、稳定、易恢复
- 每轮细节可回放
- Agent 的“为什么这么调”可审计

## Agent 决策器设计

Agent 不直接自由输出任意 PID，而是在规则层收窄的动作空间里做选择。

### 每轮决策输入

- 当前轮结构化结果
- 当前批历史结果
- 当前批最优 PID
- 上一批最优 PID
- 当前阶段上下文
- profile 中的已知限制与带宽信息

### 每轮决策输出

Agent 输出结构化动作，而不是直接报任意 PID：

```json
{
  "decision_type": "mutate_from_best",
  "base_pid_source": "current_batch_best",
  "target_wheel": "left",
  "target_param": "kp",
  "delta": 5.0,
  "phase": "shrink",
  "reason": "combined_score improved, left wheel still slower, overshoot acceptable"
}
```

### 受限动作空间

允许的动作：

- `keep_and_verify`
- `mutate_kp`
- `mutate_ki`
- `mutate_kd`
- `rebalance_left`
- `rebalance_right`
- `shared_adjust`
- `rollback_to_best`

约束规则：

- 不允许自由大跳
- 不允许连续两轮同时做多个大步调整
- 不允许输出负 PID
- 不允许跨越当前阶段定义的步长上限

## 10 轮批内决策策略

每批固定 10 轮，分三段执行：

### `explore`，第 1-3 轮

- 目标：快速判断是“响应不够”还是“稳定性不够”
- 允许中大步长
- 优先搜索 `Kp`
- 可以做共享调整，也可以修正明显更差的一侧

### `shrink`，第 4-7 轮

- 目标：围绕当前批最优点收缩搜索
- 只允许小步或中步
- 主要搜索 `Kp` 和 `Ki`
- 若左右轮失衡明显，优先改单侧

### `confirm`，第 8-10 轮

- 目标：验证当前最优是否稳定
- 允许复验或小步微调
- 若进入平台区，则不再做激进探索
- 只有在重复超调条件下，才开放 `Kd` 小步修正

## 参数调整硬规则

### `Kp`

- 响应慢、超调不明显、稳态误差不突出时，优先提高 `Kp`
- 出现明显超调、低频振荡或停机拖尾时，优先降低 `Kp`

### `Ki`

- 稳态误差明显、响应速度已基本够用时，再提高 `Ki`
- `Ki` 的比较必须建立在固定采样周期、固定评分窗口上
- 若提高 `Ki` 后出现持续振荡或停不干净，优先回退 `Ki`

### `Kd`

- 默认不进入首轮搜索
- 只有在多轮重复超调、`Kp` 和 `Ki` 回退后仍不能抑制时，才开放 `Kd`
- `Kd` 只允许小步条件触发调整

### 左右轮修正

- 若左右轮分数分裂明显，优先改单侧
- 不因一侧改善而忽略另一侧明显恶化
- `combined_score` 改善但左右轮平衡明显变差时，不直接视为可接受最优

## 参考官方调参经验形成的硬约束

以下规则来自公开官方资料，并直接约束 Agent：

- 先调 `Kp`，后调 `Ki`
- `Ki` 评估必须在固定采样周期下进行
- 不在单一速度点上假设得到全局最优
- 明显饱和和 anti-windup 异常时，不把结果视为高质量候选
- 低速测速纹波和强磁/齿槽带来的天然波动，不直接当成闭环性能恶化

这些规则的主要参考来源包括：

- ST AN1905
- TI TIDA-01496 用户指南
- TI MCF8329A Tuning Guide
- Microchip AN1307 Tuning Guide
- maxon 速度控制手动调参和测速精度支持文档

## 安全护栏

在任何轮次中，若出现下列情况，Agent 不得继续沿当前方向推进：

- 遥测缺失或段数据不完整
- `stop_flag` 异常
- 明显输出饱和
- 疑似电流或供电限幅
- 连续两轮显著恶化
- 停机后仍有较大残余速度或残余 PWM

策略要求：

- 单轮严重恶化时，下一轮强制 `rollback_to_best`
- 连续坏样本时，终止当前批并等待用户
- 前置条件异常时，skill 直接报告“当前工况不适合自动调参”

## 中断恢复

skill 启动时必须检查 `active_batch` 和 `agent_tuning`：

- 若会话处于运行中且批次未完成，则从 `current_round_index + 1` 恢复
- 若已完成 10 轮且 `pending_user_action` 不为空，则只输出建议并等待用户
- 若 profile 缺失新增字段，则自动补默认值并兼容旧 profile

## 用户交互策略

用户只在批次边界介入，不在单轮之间介入。

### `air_dual` 批末可用动作

- `continue_air`
- `enter_ground`
- `stop_air`

### `ground_dual` 批末可用动作

- `continue_ground`
- `save`
- `stop_without_save`

用户拒绝继续时，skill 不自动越级进入下一阶段。

## 测试与验证

### 单元测试

- worker 单轮输入输出契约
- `active_batch` 和恢复逻辑
- Agent 受限动作空间
- `Kd` 条件触发规则
- 平台区、回退、停止条件

### 集成测试

- skill 自动完成 `pwm_map -> pwm_identify`
- `air_dual` 自动完成 10 轮后正确等待用户
- `ground_dual` 只在用户确认后进入
- 旧 profile 缺少新增字段时自动补默认值

### 真机回归

- 架空低速、中速、高速
- 带载启动、加速、减速、停止
- 左右轮差异明显时的再平衡
- 强磁电机低速纹波不被误判
- 饱和、供电、限流条件下不盲目继续搜 PID

## 设计取舍

本方案优先保留以下原则：

- 实时试验执行仍由现有 Python/固件链路承担
- Agent 只做高层调参与批内决策，不接管固件实时控制
- 先增强结构化结果和状态契约，再引入 Agent 决策
- 优先保证可恢复、可解释、可人工接管，而不是追求一次性全自动跑到底

这个取舍符合当前项目的赛场使用场景：用户希望 Agent 自动跑前置阶段和批内 10 轮搜索，但仍希望在阶段切换和最终保存时保留人工把关。

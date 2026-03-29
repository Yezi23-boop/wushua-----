# Speed Loop LLM 决策版设计
**Date:** 2026-03-29

## 背景

当前 `project/speed_loop_autotune/` 已具备完整执行链：

- `pwm_map`：建图和死区摸底
- `pwm_identify`：生成 `seed_pi`
- `air_dual-step`：架空双轮单轮执行 worker
- `ground_dual-step`：带载双轮单轮执行 worker
- `agent_orchestrator`：批次编排、结果落盘、批末等待用户确认

当前短板不是“不能自动调参”，而是“自动调参的决策核心仍主要由固定规则驱动”。本设计把系统升级为：

- `skill` 作为唯一用户入口
- `speed_loop_tuning` agent 作为每轮 PID 决策大脑
- `agent_orchestrator` 作为唯一运行时状态机
- `worker` 只负责单轮执行

## 目标

- 把“下一轮 PID 怎么改”的核心判断交给 LLM
- 保留 `pwm_map -> pwm_identify -> air_dual -> ground_dual` 主链不变
- 保留批次边界的显式用户确认
- 允许 LLM 在必要时做明显跳跃式 PID 调整
- 规则层只保留运行安全、输入整理、输出校验和熔断恢复
- 保证每一轮都可落盘、可回放、可恢复、可复盘

## 非目标

- 本版不做无人值守直到最终 `save`
- 本版不把 LLM 直接嵌进 `air_dual.py` 或 `ground_dual.py`
- 本版不把原始波形作为唯一优化目标
- 本版不保留旧规则决策器作为并行主路径

## 职责边界

### 1. skill：唯一用户入口和桥接层

skill 只负责：

- 启动一次调参会话
- 调用 orchestrator 的公开接口
- 把 orchestrator 返回的 `decision_request` 转交给 `speed_loop_tuning` agent
- 把 agent 返回的原始文本输出重新交给 orchestrator
- 在批次边界向用户展示：
  - 当前阶段
  - 本批摘要
  - 推荐动作
- 接收用户动作词并提交给 orchestrator

skill 不负责：

- 自己串起 10 轮状态机
- 自己判断是否补跑 `pwm_map` / `pwm_identify`
- 自己判断阶段切换
- 自己决定下一轮 PID

一句话：skill 是用户入口和消息桥，不持有运行状态机。

### 2. orchestrator：唯一运行时状态机

orchestrator 独占这些职责：

- 读取和恢复 profile
- 自动补跑 `pwm_map` / `pwm_identify`
- 判断当前阶段和当前批状态
- 启动、推进、恢复、结束 batch
- 生成 `decision_context`
- 接收并校验 LLM 决策
- 调 step worker 执行
- 写 `profile`、`round result`、`decision trace`、`waveform path`
- 触发熔断、恢复、等待用户确认
- 在第 10 轮或熔断后生成批次摘要和推荐动作

orchestrator 不负责：

- 用规则替 LLM 决定下一轮 PID
- 绕过批次边界自动 `save`

一句话：orchestrator 是唯一持有批内状态机的运行时引擎。

### 3. agent：唯一决策大脑

`speed_loop_tuning` agent 只负责：

- 读取 `decision_context`
- 分析最近几轮结构化结果、批内最优、历史阶段最优和波形摘要
- 输出下一轮完整 PID
- 输出决策理由、风险、信心和决策类型

agent 不负责：

- 直接发串口命令
- 直接修改 profile 主状态
- 直接执行 `save`

### 4. worker：唯一单轮执行器

worker 只负责单轮试验执行：

- `air_dual-step`
- `ground_dual-step`

输入一组 PID，执行一轮试验，输出结构化结果。worker 不负责下一轮决策，也不负责阶段切换。

## 总体流程

### 顶层流程

1. 用户调用 skill
2. skill 调用 orchestrator `start_or_resume_workflow()`
3. orchestrator 自动补跑 `pwm_map` / `pwm_identify`
4. orchestrator 进入或恢复 `air_dual` / `ground_dual`
5. orchestrator 返回一个 `decision_request`
6. skill 把 `decision_request.context` 交给 `speed_loop_tuning` agent
7. agent 返回原始文本输出
8. skill 调用 orchestrator `submit_agent_response()`
9. orchestrator 解析、校验、重试并执行本轮，随后：
   - 若批次未结束，则返回下一个 `decision_request`
   - 若批次结束或触发熔断，则返回 `waiting_user`
10. skill 只在 `waiting_user` 时向用户展示摘要和动作词

### 批次边界规则

- 批内自动，批末等待用户
- `enter_ground` 必须显式确认
- `save` 只在完成一批 `ground_dual` 后才合法
- 批内所有“继续下一轮”的推进都由 orchestrator 状态机驱动，不由 skill 拼接

## LLM 决策原则

本系统采用：

- `skill` 负责用户交互和 agent 桥接
- `agent` 深度参与每一轮决策
- `orchestrator` 只做编排、校验、熔断和恢复

本系统不采用：

- 规则主导的 PID 搜索
- worker 内部固定枚举 10 组 PID
- 由 skill 自己控制批内轮次推进

### 允许的自由度

- agent 可以直接输出下一轮完整 PID
- agent 可以做明显跳跃式调整
- agent 不受“只能围绕当前最优小步修改”的硬规则限制

### 保留的最小安全层

保留的是运行安全，不是调参策略：

- JSON 和 schema 校验
- PID 数值合法性和硬边界
- worker 成功执行与结果落盘
- 批次边界和 `save` 权限校验
- 熔断、恢复和等待用户

### 参考字段不是隐藏规则

`search_phase`、`decision_hints`、`score_trend`、`plateau_detected` 这类字段必须标注为：

- `advisory_only: true`

它们只能作为提示证据，不能在实现中被当作硬编码动作开关。

## 每轮决策输入包

每一轮都向 agent 提供一个固定结构的 `decision_context` JSON。

### 顶层约束

- `schema_version`: `1`
- 顶层 `additionalProperties = false`
- 未声明字段一律非法
- 所有分数字段统一为 `lower_is_better`
- 所有 PID 字段统一单位为 `controller_gain`

### 顶层结构

```json
{
  "schema_version": 1,
  "stage_context": {},
  "pid_anchors": {},
  "recent_rounds": [],
  "waveform_summary": {},
  "runtime_guardrails": {},
  "recovery_state": {},
  "advisory_hints": {}
}
```

### `stage_context`

- 类型：object
- 必填：是
- 额外字段：禁止

字段：

- `stage_name`: string，必填，枚举 `air_dual | ground_dual`
- `batch_id`: string，必填，非空
- `round_index`: integer，必填，范围 `1..10`
- `batch_size`: integer，必填，固定 `10`
- `current_status`: string，必填，固定 `running`
- `score_direction`: string，必填，固定 `lower_is_better`
- `waveform_role`: string，必填，固定 `secondary_evidence`
- `disallowed_actions`: string[]，必填，元素枚举 `save | enter_ground | continue_air | continue_ground | stop_air | stop_without_save`

### `pid_anchors`

- 类型：object
- 必填：是
- 额外字段：禁止

字段：

- `batch_start_pid`: `pid_bundle`，必填
- `baseline_pid`: `pid_bundle`，必填
- `current_batch_best_pid`: `pid_bundle | null`，必填
- `current_batch_best_score`: `number | null`，必填
- `historical_stage_best_pid`: `pid_bundle | null`，必填
- `historical_stage_best_score`: `number | null`，必填
- `last_batch_best`: `pid_bundle | null`，必填
- `seed_pi`: `pid_bundle | null`，必填

### `recent_rounds`

- 类型：array
- 必填：是
- 长度：`0..5`
- 元素类型：`recent_round`

`recent_round` 字段：

- 类型：object
- 必填：是
- 额外字段：禁止
- required 集合：
  - `round_index`
  - `candidate_pid`
  - `combined_score`
  - `a_score`
  - `b_score`
  - `left_score`
  - `right_score`
  - `band_scores`
  - `overshoot_flag`
  - `persistent_overshoot_flag`
  - `speed_drop_flag`
  - `stop_clean_flag`
  - `pwm_saturation_ratio`
  - `current_limit_or_headroom_flag`
  - `dominant_issue`
  - `delta_vs_previous_combined`
  - `delta_vs_batch_best_combined`
  - `left_right_gap`
  - `score_trend`
  - `plateau_detected`
  - `decision_hints`
  - `advisory_only`
  - `result_path`
  - `waveform_path`

- `round_index`: integer，必填，范围 `1..10`
- `candidate_pid`: `pid_bundle`，必填
- `combined_score`: number，必填
- `a_score`: number，必填
- `b_score`: number，必填
- `left_score`: number，必填
- `right_score`: number，必填
- `band_scores`: object，必填，字段固定为 `low/mid/high/top`，值为 `number | null`，`additionalProperties = false`
- `overshoot_flag`: boolean，必填
- `persistent_overshoot_flag`: boolean，必填
- `speed_drop_flag`: boolean，必填
- `stop_clean_flag`: boolean，必填
- `pwm_saturation_ratio`: number，必填，范围 `0.0..1.0`
- `current_limit_or_headroom_flag`: boolean，必填
- `dominant_issue`: string | null，必填
- `delta_vs_previous_combined`: `number | null`，必填
- `delta_vs_batch_best_combined`: `number | null`，必填
- `left_right_gap`: number，必填
- `score_trend`: string | null，必填，枚举 `improving | worsening | flat`
- `plateau_detected`: boolean，必填
- `decision_hints`: string[]，必填
- `advisory_only`: boolean，必填，固定 `true`
- `result_path`: string，必填
- `waveform_path`: `string | null`，必填

### `waveform_summary`

- 类型：object
- 必填：是
- 额外字段：禁止

字段：

- `available`: boolean，必填
- `waveform_digest`: object，必填，`additionalProperties = false`，required 集合为全部 5 个字段，字段：
  - `tail_jitter`: `number | null`
  - `peak_windows`: `number | null`
  - `settling_tail_shape`: `string | null`
  - `stop_tail_residual`: `number | null`
  - `oscillation_hint`: `string | null`
- `waveform_flags`: object，必填，`additionalProperties = false`，required 集合为全部 4 个字段，字段：
  - `looks_noisy`: boolean
  - `looks_underdamped`: boolean
  - `looks_saturated`: boolean
  - `looks_measurement_limited`: boolean

### `runtime_guardrails`

- 类型：object
- 必填：是
- 额外字段：禁止

字段：

- `absolute_pid_limits`: object，必填，`additionalProperties = false`，required 集合为全部 6 个字段
  - `kp_min`: number
  - `kp_max`: number
  - `ki_min`: number
  - `ki_max`: number
  - `kd_min`: number
  - `kd_max`: number
- `precision`: object，必填，`additionalProperties = false`，required 集合为全部 3 个字段
  - `kp_decimals`: integer，范围 `0..6`
  - `ki_decimals`: integer，范围 `0..6`
  - `kd_decimals`: integer，范围 `0..6`
- `allow_large_jump`: boolean，必填，固定 `true`
- `single_candidate_only`: boolean，必填，固定 `true`
- `worker_timeout_seconds`: integer，必填，默认 `20`
- `decision_retry_budget`: integer，必填，默认 `2`
- `timeout_retry_budget`: integer，必填，默认 `1`
- `significant_regression_abs`: number，必填，默认 `5.0`
- `significant_regression_ratio`: number，必填，默认 `0.03`
- `plateau_abs_threshold`: number，必填，默认 `2.0`
- `plateau_rounds`: integer，必填，默认 `3`
- `abnormal_persistent_rounds`: integer，必填，默认 `2`
- `waveform_severe_flag_threshold`: integer，必填，默认 `2`

说明：

- 这些阈值属于运行安全参数，不属于 LLM 搜索策略
- 可由 profile 覆盖；缺失时使用 host 默认值
- host 默认安全范围固定为：
  - `kp_min = 0`
  - `kp_max = 500`
  - `ki_min = 0`
  - `ki_max = 200`
  - `kd_min = 0`
  - `kd_max = 50`

### `recovery_state`

- 类型：object
- 必填：是
- 额外字段：禁止

字段：

- `must_recover`: boolean，必填，作为唯一恢复态真值
- `high_risk_round_seen`: boolean，必填
- `recovery_reason`: `string | null`，必填，枚举 `score_regression | high_risk_failure | manual_resume | null`

### `advisory_hints`

- 类型：object
- 必填：是
- 额外字段：禁止

字段：

- `search_phase`: `string | null`，枚举 `explore | shrink | confirm`
- `advisory_only`: boolean，必填，固定 `true`

### `pid_bundle`

`pid_bundle` 为固定对象：

```json
{
  "left": {"kp": 0.0, "ki": 0.0, "kd": 0.0},
  "right": {"kp": 0.0, "ki": 0.0, "kd": 0.0}
}
```

约束：

- `left/right` 必填
- `kp/ki/kd` 必填
- 类型均为 `number`
- 值必须为有限数值
- 对象 `additionalProperties = false`

补充：

- `left` 与 `right` 自身 `additionalProperties = false`
- `left/right` 的 required 集合固定为 `kp/ki/kd`

## 每轮决策输出契约

agent 每轮必须输出结构化 `llm_decision` JSON。

### 顶层约束

- `schema_version`: `1`
- 顶层 `additionalProperties = false`
- 不接受自由文本代替 JSON

### 顶层结构

```json
{
  "schema_version": 1,
  "candidate_pid": {},
  "decision_summary": "",
  "primary_reason": "",
  "supporting_signals": [],
  "decision_mode": "",
  "base_reference": "",
  "expected_outcome": "",
  "confidence": "",
  "risk_level": "",
  "needs_waveform_review": false,
  "batch_end_recommendation_if_no_improve": ""
}
```

### 字段定义

- `candidate_pid`: `pid_bundle`，必填
- `decision_summary`: string，必填，长度 `1..400`
- `primary_reason`: string，必填，枚举：
  - `slow_response`
  - `steady_error`
  - `overshoot`
  - `left_right_mismatch`
  - `plateau_break`
  - `saturation_limited`
  - `measurement_conflict`
- `supporting_signals`: string[]，必填，长度 `1..12`
- `decision_mode`: string，必填，枚举：
  - `hold`
  - `local_refine`
  - `rollback`
  - `jump_explore`
  - `rebalance_left`
  - `rebalance_right`
  - `kd_probe`
- `base_reference`: string，必填，枚举：
  - `batch_start`
  - `last_round`
  - `current_batch_best`
  - `historical_stage_best`
  - `seed_pi`
- `expected_outcome`: string，必填，枚举：
  - `improve_response`
  - `reduce_overshoot`
  - `improve_steady_state`
  - `fix_left_right_gap`
  - `verify_plateau`
  - `test_new_region`
- `confidence`: string，必填，枚举 `low | medium | high`
- `risk_level`: string，必填，枚举 `low | medium | high`
- `needs_waveform_review`: boolean，必填
- `batch_end_recommendation_if_no_improve`: string，必填，枚举：
  - `continue_air`
  - `enter_ground`
  - `stop_air`
  - `continue_ground`
  - `save`
  - `stop_without_save`

## orchestrator 运行时接口

为支持“Codex 当前会话是决策大脑”，orchestrator 对外公开 4 个接口。skill 只能调用这些接口，不能越级操作内部状态。

### 1. `start_or_resume_workflow()`

输入：

- `profile_path`
- `requested_stage: air_dual | ground_dual | auto`

职责：

- 读取并恢复 profile
- 自动补跑 `pwm_map` / `pwm_identify`
- 判断阶段
- 启动或恢复当前 batch
- 返回 `orchestrator_response`

返回：

- 若需要下一轮决策：`state = decision_required`
- 若已到用户边界：`state = waiting_user`

### 2. `submit_agent_response()`

输入：

- `batch_id`
- `round_index`
- `request_id`
- `raw_agent_output`

职责：

- 解析 `raw_agent_output`，目标产物为 `llm_decision`
- 做 schema 校验
- 做数值边界校验
- 处理 malformed JSON、字段缺失、非法枚举和超时重试预算
- 写入 `decision_trace`
- 调 worker 执行本轮
- 更新 profile 和 round result
- 生成新的 `orchestrator_response`

### 3. `submit_user_action()`

输入：

- `batch_id`
- `user_action`

职责：

- 校验动作词是否合法
- 推进到下一阶段或下一批
- 更新 profile

### 4. `get_workflow_status()`

职责：

- 返回当前会话状态
- 用于中断恢复、UI 展示和排查

### `orchestrator_response`

顶层固定结构：

```json
{
  "state": "decision_required",
  "batch_id": "air_0003",
  "round_index": 4,
  "decision_request": {
    "request_id": "air_0003_r04",
    "context": {}
  },
  "batch_summary": null,
  "recommended_action": null,
  "allowed_actions": []
}
```

字段定义：

- `state`: 必填，枚举 `decision_required | waiting_user | workflow_complete | failed`
- `batch_id`: `string | null`
- `round_index`: `integer | null`
- `decision_request`: `decision_request_payload | null`
- `batch_summary`: `object | null`
- `recommended_action`: `string | null`
- `allowed_actions`: `string[]`

`decision_request_payload` 固定结构：

- 类型：object
- 必填：是
- `additionalProperties = false`
- required 集合：
  - `request_id`
  - `context`
- 字段：
  - `request_id`: string，非空，建议格式 `<batch_id>_r<round_index>`
  - `context`: `decision_context`

`allowed_actions` 在 `waiting_user` 下必须按阶段固定枚举：

- 正常 `air_dual` 批末：
  - `continue_air`
  - `enter_ground`
  - `stop_air`
- 正常 `ground_dual` 批末：
  - `continue_ground`
  - `save`
  - `stop_without_save`
- 因非法输出、超时或熔断触发的保护停机：
  - `air_dual` 只允许 `continue_air | stop_air`
  - `ground_dual` 只允许 `continue_ground | stop_without_save`

## LLM 输出失败路径

必须定义清晰失败处理，且由 orchestrator 执行。

### 非法输出

包括：

- malformed JSON
- 缺字段
- 非法枚举
- 数值不可解析
- PID 超出硬边界
- 额外字段出现

处理：

1. 记录一次 `decision_error`
2. 同一轮最多重试 `decision_retry_budget` 次，默认 `2`
3. 若仍失败：
   - 当前 batch 进入 `waiting_user`
   - `recommended_action = stop_air` 或 `stop_without_save`，按阶段决定
   - `allowed_actions` 只暴露安全动作

### 模型超时

处理：

1. 记录 `decision_timeout`
2. 同一轮最多重试 `timeout_retry_budget` 次，默认 `1`
3. 再失败则进入 `waiting_user`

### 低信心高风险输出

如果：

- `confidence = low`
- 且 `risk_level = high`

则：

- 允许执行
- 但必须在 `agent_decision_trace.jsonl` 当前轮记录里写 `high_risk_round = true`
- 且下一轮 `decision_context.recovery_state.high_risk_round_seen = true`
- 若本轮执行后同时满足：
  - `combined_score` 相比上一轮恶化大于 `max(significant_regression_abs, previous_combined * significant_regression_ratio)`
  - 或 `combined_score` 相比当前 batch 最优恶化大于同一阈值
则下一轮：

- `decision_context.recovery_state.must_recover = true`
- `decision_context.recovery_state.recovery_reason = high_risk_failure`

## 运行安全护栏

这些护栏属于运行安全，不属于 LLM 策略。

### 1. PID 硬边界

执行前必须校验：

- `kp_min <= kp <= kp_max`
- `ki_min <= ki <= ki_max`
- `kd_min <= kd <= kd_max`

边界来源：

- 优先读取 profile
- 缺失时使用 host 默认安全范围

### 2. 执行熔断

满足任一条件，当前 batch 立即停止并进入 `waiting_user`：

- worker 超时
- telemetry 缺失
- result JSON 不完整
- 一个轮次内 `waveform_flags` 为真的项数量 `>= waveform_severe_flag_threshold`
- 连续 `abnormal_persistent_rounds` 轮 `stop_clean_flag = false`
- 连续 `abnormal_persistent_rounds` 轮 `combined_score` 均恶化超过：
  - `max(significant_regression_abs, current_batch_best_score * significant_regression_ratio)`

定义：

- `telemetry 缺失` 指本轮执行期内缺少以下任一必需字段：
  - `combined_score`
  - `left_score`
  - `right_score`
  - `stop_clean_flag`
- `result JSON 不完整` 指 `round_result.json` 缺少以下任一必需字段：
  - `candidate_pid`
  - `combined_score`
  - `a_score`
  - `b_score`
  - `left_score`
  - `right_score`
  - `result_path`

### 3. 批内恢复

若未触发熔断，但发生“明显恶化”：

明显恶化定义为满足任一条件：

- `combined_score` 相比上一轮恶化大于 `max(significant_regression_abs, previous_combined * significant_regression_ratio)`
- `combined_score` 相比当前 batch 最优恶化大于同一阈值

- 当前轮标记 `recovery_recommended = true`
- 下一轮 `recovery_state.must_recover = true`
- 下一轮 `recovery_state.recovery_reason = score_regression`
- orchestrator 不替 LLM 决策，只把这个事实写入下一轮上下文

### 4. 平台区标记

如果最近 `plateau_rounds` 轮都满足：

- `abs(delta_vs_previous_combined) <= plateau_abs_threshold`

则：

- 仅标记 `plateau_detected = true`
- 不自动替 LLM 收缩搜索

## 波形角色

原始波形可作为辅助证据，但不是主优化目标。

### 默认权重

- 正常情况下：结构化指标优先
- 波形只作辅助判断

### 提升波形权重的条件

以下任一条件满足时，agent 应显式关注波形摘要：

- 结构化分数互相冲突
- 低速纹波与真实超调难区分
- `combined_score` 改善但停车/尾部不干净
- 疑似饱和或测量受限

## 记录与恢复

### 必须落盘的内容

- `current_tuning_profile.json`
- 每轮 `round_result.json`
- `agent_decision_trace.jsonl`
- `waveform_path` 或 `waveform_digest`
- 批次摘要

### 恢复原则

- `start_or_resume_workflow()` 必须幂等
- 如果上次停在 `decision_required`，下一次恢复必须返回同一轮未消费的 `decision_request`
- 如果上次停在 `waiting_user`，下一次恢复不得偷偷继续跑
- 如果 `decision_trace` 已写入但 `profile` 未完成更新，恢复时必须能识别半提交状态并补全或回滚到上一个完整轮次

## 测试策略

### 1. schema 与校验测试

- `decision_context` 顶层和子对象 `additionalProperties = false`
- 必填字段缺失时报错
- 非法枚举时报错
- PID 非数字、负数、越界时报错
- 数组元素 shape 错误时报错
- `decision_request_payload` 的 `request_id/context` 缺失时报错

### 2. 正常批次测试

- 可自动补跑 `pwm_map -> pwm_identify`
- `air_dual` 可连续跑满 10 轮
- `ground_dual` 可连续跑满 10 轮
- 第 10 轮后进入 `waiting_user`

### 3. LLM 失败路径测试

- malformed JSON 重试至预算耗尽后进入 `waiting_user`
- 超时重试至预算耗尽后进入 `waiting_user`
- 非法枚举、缺字段、额外字段都会被拒绝
- `low confidence + high risk` 且结果显著恶化后，下一轮 `must_recover = true`
- `high_risk_round = true` 正确写入当前轮 `decision_trace`
- `submit_agent_response()` 能接收原始文本，并由 orchestrator 自行解析和重试

### 4. 熔断与恢复测试

- 连续恶化超过阈值后触发熔断并进入 `waiting_user`
- 波形严重异常超过阈值后触发熔断
- `stop_clean_flag = false` 持续达到阈值后触发熔断
- 熔断后不得继续自动跑下一轮
- 普通轮次明显恶化但未熔断时，下一轮也必须进入 `must_recover = true`

### 5. 落盘与幂等测试

- `decision_trace` 写入成功但 `profile` 写入失败时，恢复逻辑正确
- `profile` 已更新但 `round_result` 未写完时，恢复逻辑正确
- 重复调用 `start_or_resume_workflow()` 不会重复消费同一轮
- `waiting_user` 状态下重复恢复不会偷偷推进状态机

### 6. 用户边界测试

- 未显式 `enter_ground` 前不得进入 `ground_dual`
- 未显式 `save` 前不得写最终保存状态
- 用户拒绝继续时，不得自动越级到下一阶段

## 实现建议

推荐实现顺序：

1. 先把 `decision_context` / `llm_decision` schema 固定下来
2. 再把 orchestrator 改成 `decision_required -> submit_llm_decision -> waiting_user` 状态机
3. 再把 skill 改成“桥接 orchestrator 与 agent”的唯一入口
4. 最后补异常恢复和幂等测试

## 结论

这版设计不是把 LLM 塞进 Python，而是把它放在正确的位置：

- skill 负责入口
- agent 负责深度决策
- orchestrator 负责状态机
- worker 负责执行

这样既能让 LLM 真正主导“下一轮 PID 怎么改”，又不会丢掉运行安全、恢复能力和用户边界。

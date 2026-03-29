# Speed Loop LLM 决策版设计

**Date:** 2026-03-29

## 背景

当前 `project/speed_loop_autotune/` 已经具备：

- `pwm_map`：建图和死区摸底
- `pwm_identify`：生成 `seed_pi`
- `air_dual-step`：架空双轮单轮执行 worker
- `ground_dual-step`：带载双轮单轮执行 worker
- `agent_orchestrator`：批次编排、日志落盘、批末确认

当前问题不是“没有自动调参”，而是“自动调参的决策核心仍然主要由固定规则驱动”：

- 下一轮 PID 由 `default_decision_policy()` 里的固定规则决定
- LLM 没有成为真正的调参大脑
- 允许的动作空间和跳跃策略主要由 if/else 预先写死
- 波形只能作为日志证据，不能真正参与复杂冲突判断

本设计把现有方案升级为：

**skill 作为唯一用户入口，LLM agent 作为每轮决策大脑，orchestrator 只做运行时编排，worker 只做单轮执行。**

## 目标

- 把“下一轮 PID 怎么改”的核心权力交给 LLM
- 保留 `pwm_map -> pwm_identify -> air_dual -> ground_dual` 主链不变
- 保留批次边界的显式用户确认
- 允许 LLM 在必要时做明显跳跃式 PID 调整
- 规则层只保留最小安全护栏、输入整理和输出校验
- 保证每一轮决策都能落盘、回放、复盘和恢复

## 非目标

- 本版不做全自动无人值守直到最终 `save`
- 本版不把 LLM 直接嵌入 `air_dual.py` 或 `ground_dual.py`
- 本版不把原始波形作为唯一优化目标
- 本版不重写固件协议和 VOFA 遥测定义
- 本版不让 orchestrator 继续充当主要规则决策器

## 总体架构

系统拆成四层：

1. `speed-loop-agent-autotune` skill
2. `speed_loop_tuning` agent
3. `agent_orchestrator`
4. step worker

职责边界固定如下：

### 1. skill

skill 是唯一用户入口，负责：

- 检查 profile、日志、端口和现场前置条件
- 判断当前阶段
- 驱动 `pwm_map` / `pwm_identify`
- 驱动 orchestrator 的 prepare / execute / finalize 流程
- 在批次边界向用户展示结果和建议
- 接收动作词：
  - `continue_air`
  - `enter_ground`
  - `stop_air`
  - `continue_ground`
  - `save`
  - `stop_without_save`

skill 不负责每轮 PID 决策本身。

### 2. agent

`speed_loop_tuning` agent 是调参大脑，负责：

- 读取每轮决策输入包
- 分析最近几轮趋势、当前批最优、历史阶段最优、波形摘要
- 给出下一轮完整 PID
- 输出决策理由、风险、信心和决策类型

agent 不负责：

- 直接发串口命令
- 直接改 profile 主状态
- 绕过批次边界的用户确认
- 直接执行 `save`

### 3. orchestrator

`agent_orchestrator` 只做运行时编排，负责：

- 自动补跑 `pwm_map` / `pwm_identify`
- 恢复会话和 batch 状态
- 生成每轮 `decision_context`
- 接收 agent 返回的结构化决策
- 做最小合法性校验
- 调 step worker 执行
- 写 round result、waveform、decision trace、profile
- 生成批次摘要和推荐动作

orchestrator 不再拥有主要调参策略，不再靠固定规则决定下一轮 PID。

### 4. worker

worker 只负责单轮执行：

- `air_dual-step`
- `ground_dual-step`

输入一组 PID，执行一轮试验，输出结构化结果和波形摘要。

worker 不负责：

- 决定下一轮 PID
- 决定是否继续下一批
- 决定是否进入下一阶段

## 主流程

每次 skill 启动后，按以下状态机执行：

1. 检查 `current_tuning_profile.json`
2. 若缺 `shared_targets` 或 `pwm_map` 关键结果，自动跑 `pwm_map`
3. 若缺 `pwm_identify.seed_pi`，自动跑 `pwm_identify`
4. 若当前仍处于架空阶段，进入或恢复 `air_dual`
5. `air_dual` 每批执行 10 轮，每轮由 agent 决策下一轮 PID
6. 第 10 轮结束后 skill 展示批次摘要并等待用户确认
7. 用户执行 `enter_ground` 后，才进入 `ground_dual`
8. `ground_dual` 每批执行 10 轮，每轮同样由 agent 决策
9. 用户执行 `save` 后，才把结果视为最终保存

批次边界规则固定：

- 批内自动
- 批末等待用户
- 不允许越级自动切阶段
- 不允许隐式保存

## LLM 决策模式

本设计采用：

- `skill` 作为唯一入口
- `agent` 深度参与每一轮决策
- `orchestrator` 只做编排

不采用：

- 纯规则决策
- 纯 worker 内部固定搜索
- 让 LLM 直接嵌进 worker

LLM 的自由度定义为：

- 允许直接输出下一轮完整 PID
- 允许在必要时做明显跳跃式调整
- 不限制它只能围绕当前最优小步搜索

代码层保留的规则只有：

- 输出格式必须合法
- PID 必须完整、可解析、非负
- 轮次结果必须成功落盘
- 批次边界必须等待用户动作词

## 每轮决策输入包

每一轮都向 agent 提供固定结构的 `decision_context`，至少包含五层信息。

### 1. 当前阶段上下文

- `stage_name`
- `batch_id`
- `round_index`
- `search_phase`
- `batch_size`
- 当前是否处于批内还是批末
- 当前不允许执行的动作

### 2. PID 锚点

- `batch_start_pid`
- `baseline_pid`
- `current_batch_best_pid`
- `current_batch_best_score`
- `historical_stage_best_pid`
- `historical_stage_best_score`
- `last_batch_best`
- `seed_pi`

### 3. 最近几轮结构化结果

至少包含最近 3 到 5 轮：

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
- `decision_hints`

并由 orchestrator 预先补充派生趋势：

- `delta_vs_previous_combined`
- `delta_vs_batch_best_combined`
- `left_right_gap`
- `score_trend`
- `plateau_detected`

### 4. 波形辅助摘要

波形只作为辅助证据，不作为主目标。

建议包含：

- `waveform_digest.tail_jitter`
- `waveform_digest.peak_windows`
- `waveform_digest.stop_tail_residual`
- `waveform_flags.looks_noisy`
- `waveform_flags.looks_underdamped`
- `waveform_flags.looks_saturated`
- `waveform_flags.looks_measurement_limited`

只有在结构化指标冲突、低速纹波和真实超调难区分、或测量看起来异常时，才提升波形权重。

### 5. 决策约束说明

这层不是规则决策，而是最小边界说明：

- 允许明显跳跃式调整
- PID 必须非负
- 当前轮只能输出一个执行候选
- 优先优化 `combined_score`
- 不可忽略左右轮失衡
- 不能在批内直接执行阶段切换或保存

## 每轮决策输出契约

agent 每轮必须输出结构化 JSON，而不是自由文本建议。

### 必填字段

- `candidate_pid`
  - `left.kp`
  - `left.ki`
  - `left.kd`
  - `right.kp`
  - `right.ki`
  - `right.kd`
- `decision_summary`
- `primary_reason`
- `supporting_signals`
- `decision_mode`
- `base_reference`
- `expected_outcome`
- `confidence`
- `risk_level`
- `needs_waveform_review`
- `batch_end_recommendation_if_no_improve`

### 决策模式建议枚举

- `hold`
- `local_refine`
- `rollback`
- `jump_explore`
- `rebalance_left`
- `rebalance_right`
- `kd_probe`

### 主要原因建议枚举

- `slow_response`
- `steady_error`
- `overshoot`
- `left_right_mismatch`
- `plateau_break`
- `saturation_limited`
- `measurement_conflict`

### 基准参考建议枚举

- `batch_start`
- `last_round`
- `current_batch_best`
- `historical_stage_best`
- `seed_pi`

## orchestrator 运行时接口

为支持当前 Codex 会话深度参与决策，建议把 orchestrator 显式拆成四段式运行时接口。

### `prepare_next_round()`

负责：

- 恢复 batch 状态
- 自动补跑前置阶段
- 生成 `decision_context`

### `accept_llm_decision()`

负责：

- 接收 agent 输出的结构化决策
- 做最小合法性校验
- 保存本轮决策记录

### `execute_round()`

负责：

- 调 step worker 执行本轮
- 保存 result json、waveform、profile 更新

### `finalize_or_continue()`

负责：

- 未满 10 轮时，进入下一轮
- 满 10 轮时，生成批次摘要和推荐动作

## 让 LLM 深度参与但不失控

本设计不通过“强规则限制”来压制 LLM，而是通过运行安全层来接住它。

### 保留的最小安全层

- JSON 结构合法
- PID 字段完整
- PID 数值非负
- 左右轮结构完整
- worker 成功返回结果
- 批次边界动作必须显式确认

### 不保留的旧规则策略

不再由代码硬编码：

- 超调就一定减 `Kp`
- 平台区就一定加 `Ki`
- confirm 阶段只能小步调整
- 某几轮固定只能做哪类动作

### 失控止损思路

不是事前限制它“不能跳”，而是保证：

- 每次跳跃都可回放
- 每轮都能看出参考点和理由
- 恶化后可以明确回到 `current_batch_best`
- 数据异常可标记为坏样本
- 批末仍由用户决定是否继续、切阶段、保存

## Profile 与日志扩展

继续以 `current_tuning_profile.json` 作为长期状态源，但要补齐 LLM 决策相关字段。

### 顶层 `agent_tuning`

至少包含：

- `session_id`
- `workflow_stage`
- `workflow_status`
- `current_batch_id`
- `current_round_index`
- `auto_completed_stages`
- `pending_user_action`
- `last_worker_mode`
- `last_result_path`
- `last_decision_trace_path`

### 每阶段状态

`air_dual` / `ground_dual` 至少保留：

- `baseline_pid`
- `best_pid`
- `last_summary`
- `last_batch_best`
- `batch_round`
- `last_batch_summary`
- `active_batch`

### 新增日志

- `logs/agent_rounds/<stage>/<batch>_rXX.json`
- `logs/agent_waveforms/<stage>/<batch>_rXX.jsonl`
- `logs/agent_decision_trace.jsonl`

其中 `agent_decision_trace.jsonl` 每轮至少记录：

- 决策输入摘要
- agent 输出的完整决策
- 本轮结果摘要
- 本轮与 batch best 的差异

## 批次边界交互

skill 在批末统一输出三段：

- `Current stage`
- `What I observed`
- `Recommended action`

允许的动作词固定为：

- `continue_air`
- `enter_ground`
- `stop_air`
- `continue_ground`
- `save`
- `stop_without_save`

原则：

- 批内不暴露动作词
- 批末才暴露动作词
- `save` 只在 `ground_dual` 完成一批后才合法

## 测试策略

### 单元测试

- `decision_context` 生成完整且字段稳定
- agent 输出 JSON 的合法性校验
- 非法 PID 输出能被拒绝
- 恶化轮次后的回退上下文能正确生成

### 集成测试

- `pwm_map -> pwm_identify -> air_dual 10轮`
- `air_dual` 批末正确进入 `waiting_user`
- `enter_ground` 后正确启动 `ground_dual`
- `save` 前不落最终 `ground_dual.best_pid`

### 真机验证

优先验证：

1. 新 profile 能否跑通一整批 `air_dual`
2. 是否稳定写出：
   - `agent_tuning`
   - `agent_rounds/`
   - `agent_decision_trace.jsonl`
   - `last_batch_summary`
3. 左右轮分裂严重时，LLM 是否会给出单侧重平衡
4. 连续恶化时，LLM 是否会回到稳定参考点附近

## 风险

- LLM 允许大跳跃，某些轮次可能产生激进 PID
- 如果 `decision_context` 组织不好，LLM 会“猜”而不是“判”
- 波形摘要质量不足时，复杂冲突场景下判断可能不稳
- 若 profile 迁移和日志落盘不完整，会影响恢复和复盘

## 推荐实现顺序

1. 把 orchestrator 从规则决策器改成运行时编排壳
2. 固化 `decision_context` 和 `llm_decision` 的输入输出契约
3. 把 `speed_loop_tuning` agent 接入 skill 主流程
4. 先跑 `air_dual` 真机闭环，再接 `ground_dual`
5. 最后再做 prompt 和决策策略调优

## 结论

最终系统建议固定为：

- `speed-loop-agent-autotune`：唯一用户入口 skill
- `speed_loop_tuning`：LLM 深度决策 agent
- `agent_orchestrator`：运行时编排引擎
- `air_dual-step` / `ground_dual-step`：单轮执行 worker

这个结构能同时满足：

- LLM 真正深度参与调参
- 批次边界仍可控
- 执行层仍可测试
- 决策过程可回放、可恢复、可解释

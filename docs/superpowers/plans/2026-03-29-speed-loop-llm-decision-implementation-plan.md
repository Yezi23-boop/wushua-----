# Speed Loop LLM Decision Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把当前规则型 `speed_loop_autotune` 升级为“skill 桥接 + LLM agent 深度参与每轮 PID 决策 + orchestrator 持有状态机”的可恢复调参系统。

**Architecture:** 继续复用 `pwm_map`、`pwm_identify`、`air_dual-step`、`ground_dual-step` 的执行职责，在 host 侧新增严格的 LLM 输入输出契约与运行时校验层，并把 `agent_orchestrator` 改成 `decision_required -> submit_agent_response -> waiting_user` 的状态机。外层 skill 只做用户入口和 agent 桥接，所有安全边界、失败路径、重试、熔断、恢复与幂等都落到仓库内的 Python 契约和测试里。

**Tech Stack:** Python 3、`unittest`、JSON/JSONL、现有 `project/speed_loop_autotune/host/*` 脚本、仓库内文档、外部 Codex skill Markdown。

---

Reference spec: `C:\Users\ye\Desktop\龙丘电机\docs\superpowers\specs\2026-03-29-speed-loop-llm-decision-design.md`

## File Map

**Create**
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\llm_decision_contract.py`
  - 负责 `decision_context`、`llm_decision`、`decision_request_payload`、`runtime_guardrails` 的严格 schema 校验与规范化拒绝逻辑。
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_llm_decision_contract.py`
  - 只覆盖 LLM 契约、精度、request id、一致性和 guardrail 校验。

**Modify**
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\agent_session.py`
  - 扩充 `recovery_state`、`failure_trace`、`last_committed_request_id`、提交顺序和恢复真源。
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\agent_orchestrator.py`
  - 改造成 raw agent output 驱动的 orchestrator 状态机，接管解析、重试、熔断、消费语义和批次边界。
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_agent_orchestrator.py`
  - 覆盖 `decision_required`、混合重试预算、执行侧熔断、恢复、用户边界和 request 幂等。
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_agent_autotune.py`
  - 补 `agent_session` 的恢复态清除、manual resume、提交边界与 round result 最小完备字段。
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\docs\agent_autotune.md`
  - 更新 skill 桥接流程、raw response 提交协议和失败恢复。
- `C:\Users\ye\.codex\skills\speed-loop-agent-autotune\SKILL.md`
  - 改成“只做入口与桥接”的 LLM 决策版 workflow，禁止 skill 自己拼批内状态机。

## Chunk 1: LLM Contract And Guardrails

### Task 1: 为 LLM 输入输出建立严格契约模块

**Files:**
- Create: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\llm_decision_contract.py`
- Create: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_llm_decision_contract.py`

- [ ] **Step 1: 写 `decision_context` 和 `decision_request_payload` 的失败测试**

```python
def test_validate_decision_context_rejects_cross_batch_recent_rounds(self):
    context = make_valid_decision_context()
    context["recent_rounds"][-1]["round_index"] = 9
    context["stage_context"]["round_index"] = 4
    with self.assertRaises(ValueError):
        validate_decision_context(context)

def test_validate_decision_request_rejects_mismatched_request_id(self):
    payload = make_valid_decision_request_payload()
    payload["request_id"] = "ground_0001_r03"
    with self.assertRaises(ValueError):
        validate_decision_request_payload(payload, batch_id="air_0001", round_index=3)
```

- [ ] **Step 2: 跑失败测试确认缺实现**

Run: `python -m unittest project.speed_loop_autotune.tests.test_llm_decision_contract -v`  
Expected: FAIL，提示 `llm_decision_contract` 模块或校验函数不存在。

- [ ] **Step 3: 实现最小 contract 模块**

```python
def validate_decision_context(context):
    # 校验 required/additionalProperties/排序/连续性/精度边界
    ...

def validate_decision_request_payload(payload, batch_id, round_index):
    # 校验 regex、一致性和 retry counters
    ...

def validate_llm_decision(decision, guardrails):
    # 校验 candidate_pid、枚举、精度与非负边界
    ...
```

- [ ] **Step 4: 跑 contract 测试确认通过**

Run: `python -m unittest project.speed_loop_autotune.tests.test_llm_decision_contract -v`  
Expected: PASS

- [ ] **Step 5: 提交 contract 基线**

```bash
git add project/speed_loop_autotune/host/llm_decision_contract.py project/speed_loop_autotune/tests/test_llm_decision_contract.py
git commit -m "feat: add speed loop llm decision contracts"
```

### Task 2: 把 guardrails 和 round result 最小完备字段写进测试

**Files:**
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_llm_decision_contract.py`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_agent_autotune.py`

- [ ] **Step 1: 写 guardrail 反向边界与 round result 缺字段失败测试**

```python
def test_validate_runtime_guardrails_rejects_inverted_limits(self):
    guardrails = make_valid_guardrails()
    guardrails["absolute_pid_limits"]["kp_min"] = 10
    guardrails["absolute_pid_limits"]["kp_max"] = 5
    with self.assertRaises(ValueError):
        validate_runtime_guardrails(guardrails)

def test_validate_round_result_requires_worker_waveform_flags(self):
    result = make_valid_round_result()
    del result["waveform_flags"]
    with self.assertRaises(ValueError):
        validate_round_result(result)
```

- [ ] **Step 2: 跑目标测试确认失败**

Run: `python -m unittest project.speed_loop_autotune.tests.test_llm_decision_contract project.speed_loop_autotune.tests.test_agent_autotune -v`  
Expected: FAIL，提示缺少 `validate_runtime_guardrails` 或 `validate_round_result` 约束。

- [ ] **Step 3: 实现最小校验**

```python
def validate_runtime_guardrails(guardrails):
    # 校验预算、超时、阈值和 min<=max
    ...

def validate_round_result(result):
    # 校验 worker 输出的最小完备字段和 waveform_flags 来源
    ...
```

- [ ] **Step 4: 跑目标测试确认通过**

Run: `python -m unittest project.speed_loop_autotune.tests.test_llm_decision_contract project.speed_loop_autotune.tests.test_agent_autotune -v`  
Expected: PASS

- [ ] **Step 5: 提交 guardrail 完整性**

```bash
git add project/speed_loop_autotune/tests/test_llm_decision_contract.py project/speed_loop_autotune/tests/test_agent_autotune.py project/speed_loop_autotune/host/llm_decision_contract.py
git commit -m "feat: validate llm guardrails and round result completeness"
```

## Chunk 2: Orchestrator Retry, Failure, And Consumption Semantics

### Task 3: 把 orchestrator 改成 raw agent output 驱动

**Files:**
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\agent_orchestrator.py`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_agent_orchestrator.py`

- [ ] **Step 1: 写 raw output、timeout 和混合重试预算失败测试**

```python
def test_submit_agent_response_retries_same_request_id_on_invalid_json(self):
    state = start_or_resume_workflow(...)
    retry = submit_agent_response(
        batch_id=state["batch_id"],
        round_index=state["round_index"],
        request_id=state["decision_request"]["request_id"],
        response_status="ok",
        raw_agent_output="not-json",
    )
    self.assertEqual(retry["state"], "decision_required")
    self.assertEqual(retry["decision_request"]["request_id"], state["decision_request"]["request_id"])
    self.assertEqual(retry["decision_request"]["retry_counters"]["decision_errors_used"], 1)

def test_submit_agent_response_tracks_timeout_budget_independently(self):
    ...
```

- [ ] **Step 2: 跑 orchestrator 目标测试确认失败**

Run: `python -m unittest project.speed_loop_autotune.tests.test_agent_orchestrator -v`  
Expected: FAIL，提示 `submit_agent_response()` 参数或重试状态机不匹配。

- [ ] **Step 3: 实现 raw output 状态机**

```python
def submit_agent_response(...):
    # 解析 raw_agent_output
    # timeout/decision_error 分别计数
    # 预算未耗尽时返回同 request_id 的 decision_required
    ...
```

- [ ] **Step 4: 跑 orchestrator 目标测试确认通过**

Run: `python -m unittest project.speed_loop_autotune.tests.test_agent_orchestrator -v`  
Expected: PASS

- [ ] **Step 5: 提交 retry 状态机**

```bash
git add project/speed_loop_autotune/host/agent_orchestrator.py project/speed_loop_autotune/tests/test_agent_orchestrator.py
git commit -m "feat: add raw agent response retry state machine"
```

### Task 4: 收紧 request 消费语义和用户边界

**Files:**
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\agent_orchestrator.py`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_agent_orchestrator.py`

- [ ] **Step 1: 写旧 request 重放、成功后重复提交和 ground 越权失败测试**

```python
def test_submit_agent_response_rejects_consumed_request_id(self):
    state = run_one_round_success(...)
    with self.assertRaises(ValueError):
        submit_agent_response(request_id=state["consumed_request_id"], ...)

def test_start_or_resume_workflow_rejects_ground_without_enter_ground(self):
    state = start_or_resume_workflow(profile_path=..., requested_stage="ground_dual")
    self.assertNotEqual(state.get("stage_name"), "ground_dual")
```

- [ ] **Step 2: 跑目标测试确认失败**

Run: `python -m unittest project.speed_loop_autotune.tests.test_agent_orchestrator -v`  
Expected: FAIL，提示 request 消费或 `requested_stage` 边界缺失。

- [ ] **Step 3: 实现一次性消费与用户边界**

```python
def start_or_resume_workflow(...):
    # 未 enter_ground 时拒绝或降级 requested_stage=ground_dual
    ...

def submit_agent_response(...):
    # 拒绝旧 request_id 和已消费 request_id
    ...
```

- [ ] **Step 4: 跑目标测试确认通过**

Run: `python -m unittest project.speed_loop_autotune.tests.test_agent_orchestrator -v`  
Expected: PASS

- [ ] **Step 5: 提交边界与消费语义**

```bash
git add project/speed_loop_autotune/host/agent_orchestrator.py project/speed_loop_autotune/tests/test_agent_orchestrator.py
git commit -m "feat: enforce request consumption and stage gates"
```

## Chunk 3: Recovery State And Persistence Ordering

### Task 5: 在 agent_session 里实现 recovery_state 生命周期

**Files:**
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\agent_session.py`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_agent_autotune.py`

- [ ] **Step 1: 写 must_recover 置位、manual resume、成功后清除的失败测试**

```python
def test_mark_score_regression_sets_recovery_state(self):
    profile = make_profile()
    updated = mark_recovery_required(profile, reason="score_regression")
    self.assertTrue(updated["agent_tuning"]["recovery_state"]["must_recover"])

def test_finalize_successful_round_clears_manual_resume_state(self):
    profile = make_profile_with_manual_resume()
    updated = finalize_successful_round(profile)
    self.assertFalse(updated["agent_tuning"]["recovery_state"]["must_recover"])
```

- [ ] **Step 2: 跑目标测试确认失败**

Run: `python -m unittest project.speed_loop_autotune.tests.test_agent_autotune -v`  
Expected: FAIL，提示 `recovery_state` 生命周期函数不存在或行为不符。

- [ ] **Step 3: 实现 recovery_state helper**

```python
def mark_recovery_required(profile, reason):
    ...

def mark_manual_resume(profile):
    ...

def finalize_successful_round(profile):
    ...
```

- [ ] **Step 4: 跑目标测试确认通过**

Run: `python -m unittest project.speed_loop_autotune.tests.test_agent_autotune -v`  
Expected: PASS

- [ ] **Step 5: 提交 recovery state 生命周期**

```bash
git add project/speed_loop_autotune/host/agent_session.py project/speed_loop_autotune/tests/test_agent_autotune.py
git commit -m "feat: add recovery state lifecycle helpers"
```

### Task 6: 实现单轮落盘顺序、failure trace 和恢复真源

**Files:**
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\agent_session.py`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_agent_autotune.py`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_agent_orchestrator.py`

- [ ] **Step 1: 写 pending/committed、failure trace 和恢复真源失败测试**

```python
def test_restore_ignores_round_result_beyond_last_committed_request_id(self):
    state = make_half_committed_state()
    restored = restore_from_disk(state)
    self.assertEqual(restored["last_committed_request_id"], "air_0001_r03")

def test_worker_circuit_break_marks_request_as_consumed(self):
    state = run_worker_circuit_break(...)
    restored = restart_and_resume(...)
    self.assertNotEqual(restored["decision_request"]["request_id"], state["request_id"])
```

- [ ] **Step 2: 跑目标测试确认失败**

Run: `python -m unittest project.speed_loop_autotune.tests.test_agent_autotune project.speed_loop_autotune.tests.test_agent_orchestrator -v`  
Expected: FAIL，提示提交顺序、`last_committed_request_id` 或 `failure_trace` 行为不符。

- [ ] **Step 3: 实现提交顺序与恢复真源**

```python
def persist_round_transaction(...):
    # pending result -> pending trace -> commit marker -> committed trace
    ...

def restore_workflow_state(...):
    # 以 last_committed_request_id 为恢复真源
    ...
```

- [ ] **Step 4: 跑目标测试确认通过**

Run: `python -m unittest project.speed_loop_autotune.tests.test_agent_autotune project.speed_loop_autotune.tests.test_agent_orchestrator -v`  
Expected: PASS

- [ ] **Step 5: 提交恢复与幂等语义**

```bash
git add project/speed_loop_autotune/host/agent_session.py project/speed_loop_autotune/tests/test_agent_autotune.py project/speed_loop_autotune/tests/test_agent_orchestrator.py
git commit -m "feat: persist round commit markers and recovery traces"
```

## Chunk 4: Skill Bridge, Docs, And Final Verification

### Task 7: 更新 skill 和 operator guide 到 LLM 决策版

**Files:**
- Modify: `C:\Users\ye\.codex\skills\speed-loop-agent-autotune\SKILL.md`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\docs\agent_autotune.md`

- [ ] **Step 1: 写文档 smoke checklist**

```text
- skill 只做入口和 agent 桥接
- skill 不拼接批内状态机
- skill 向 orchestrator 提交 raw agent output
- 文档包含 decision_required / waiting_user / submit_user_action
```

- [ ] **Step 2: 更新 skill 与文档**

```markdown
- 读取 orchestrator 的 `decision_request`
- 调用 `speed_loop_tuning`
- 把原始输出交给 `submit_agent_response()`
- 只在 `waiting_user` 时显示动作词
```

- [ ] **Step 3: 做文档校对**

Run:

```powershell
Get-Content -Raw 'C:\Users\ye\.codex\skills\speed-loop-agent-autotune\SKILL.md'
Get-Content -Raw 'C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\docs\agent_autotune.md'
```

Expected:
- skill 只描述 bridge 流程
- 文档中的状态名与代码一致

- [ ] **Step 4: 提交文档与 skill**

```bash
git add project/speed_loop_autotune/docs/agent_autotune.md
git commit -m "docs: align agent autotune guide with llm decision flow"
```

### Task 8: 跑最终回归并记录残余风险

**Files:**
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\docs\agent_autotune.md`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\README.md`

- [ ] **Step 1: 跑新增测试**

Run:

```bash
python -m unittest project.speed_loop_autotune.tests.test_llm_decision_contract project.speed_loop_autotune.tests.test_agent_autotune project.speed_loop_autotune.tests.test_agent_orchestrator -v
```

Expected:
- PASS

- [ ] **Step 2: 跑现有 step worker 回归**

Run:

```bash
python -m unittest project.speed_loop_autotune.tests.test_vofa_autotune -v
```

Expected:
- PASS

- [ ] **Step 3: 更新 README/操作文档中的现行入口**

```markdown
- 入口是 skill + agent + orchestrator
- step worker 仍是底层执行器
- 旧规则决策器不是现行路径
```

- [ ] **Step 4: 记录残余风险**

至少记录：
- 当前仍依赖 Codex 会话稳定返回结构化 JSON
- 真机上仍需验证不同电机的 prompt 泛化能力
- skill 在仓库外，后续更新要单独同步

- [ ] **Step 5: 最终提交**

```bash
git add project/speed_loop_autotune/README.md project/speed_loop_autotune/docs/agent_autotune.md project/speed_loop_autotune/host/llm_decision_contract.py project/speed_loop_autotune/host/agent_session.py project/speed_loop_autotune/host/agent_orchestrator.py project/speed_loop_autotune/tests/test_llm_decision_contract.py project/speed_loop_autotune/tests/test_agent_autotune.py project/speed_loop_autotune/tests/test_agent_orchestrator.py
git commit -m "feat: add llm driven speed loop decision runtime"
```

## Completion Checklist

- `decision_context` / `llm_decision` / `decision_request_payload` 有严格校验器
- orchestrator 接收 raw agent output，不再依赖规则决策器
- 混合重试预算、执行侧熔断、request 消费语义、恢复真源全部有测试
- `must_recover` / `manual_resume` 生命周期闭合
- skill 和文档与代码状态机一致


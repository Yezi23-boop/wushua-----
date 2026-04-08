# Agent Autotune Entry Point

The current runtime is:

- skill entrypoint
- `speed_loop_tuning` decision agent
- repo-side orchestrator
- step workers

The orchestrator lives in [project/speed_loop_autotune/host/agent_orchestrator.py](C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\agent_orchestrator.py).

The repo-side workflow CLI now lives at [tools/agent_autotune.py](C:\Users\ye\Desktop\龙丘电机\tools\agent_autotune.py). Its default role is a bridge for the current session or skill layer:

- start or resume the orchestrator
- stop at `decision_required`
- surface the raw `decision_request`
- accept raw agent JSON back through `submit_agent_response(...)`

The repo heuristic module in [project/speed_loop_autotune/host/decision_heuristic.py](C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\decision_heuristic.py) remains available only as an explicit fallback path.

## Locked Requirement

The following requirement is intentional and should not be loosened by later refactors unless the user explicitly changes it:

- default runtime mode is agent-analysis mode, not heuristic-autoplay mode
- every round must wait for a fresh `speed_loop_tuning`-style agent decision before PID is filled
- one raw agent decision may advance only one round
- a normal run should continue in this round-by-round pattern for 10 rounds, then report a summary to the user
- normal rounds should stay quiet; only failure-stop boundaries, invalid agent JSON, serial faults, hardware faults, or orchestrator rejection may interrupt early
- batch boundaries must not auto-cross into `enter_ground`, `save`, or the next batch without an explicit user action
- the local heuristic path may exist only as an explicit fallback and must never silently become the default execution path

## Current Runtime API

Use these two orchestrator calls for the real workflow:

- `start_or_resume_workflow(...)`
- `submit_agent_response(...)`

The compatibility wrapper `run_agent_workflow(...)` should only be treated as a thin entry shim that stops at the first `decision_required` boundary. It is not the current batch executor.

## State Machine

The orchestrator now runs as:

1. `start_or_resume_workflow(...)`
2. `decision_required`
3. skill calls the decision agent
4. `submit_agent_response(...)`
5. either:
   - another `decision_required`
   - `waiting_user`
   - `completed`

State meanings:

- `decision_required`
  - one round request is ready
  - the skill must call the decision agent
  - the skill must submit the raw model output unchanged
- `waiting_user`
  - the orchestrator stopped at a legal boundary
  - no new round may run before a user action word
- `completed`
  - the current branch of the workflow has finished

## Stage Flow

- automatic prerequisites:
  - `pwm_map`
  - `pwm_identify`
- batch stages:
  - `air_dual`
  - `ground_dual`

The orchestrator still auto-runs prerequisites when the profile is missing them.

## Skill Bridge Contract

When the orchestrator returns `decision_required`:

1. read `decision_request`
2. pass `decision_request.context` to the decision agent
3. keep `decision_request.request_id` unchanged
4. submit the raw model output through `submit_agent_response(...)`

Typical bridge commands:

```bash
python tools/agent_autotune.py continue_air
python tools/agent_autotune.py --submit-raw-file decision.json
python tools/agent_autotune.py --submit-raw-json "{\"schema_version\":1,...}"
```

Behavior notes:

- if the workflow is at `waiting_user`, the CLI prints only allowed action words by default
- if the workflow is at `decision_required`, the CLI prints JSON including `decision_request`
- submitting raw agent JSON advances exactly one round and returns the next orchestrator state
- a normal operator loop is therefore: start or resume -> collect `decision_request` -> let the agent analyze once -> submit one raw agent JSON -> repeat until 10 rounds complete or the orchestrator stops early

The skill must not:

- rebuild request ids
- patch retry counters
- reimplement recovery logic
- bypass `submit_agent_response(...)`

## User Action Words

Normal batch-boundary actions:

- air:
  - `continue_air`
  - `enter_ground`
  - `stop_air`
- ground:
  - `continue_ground`
  - `save`
  - `stop_without_save`

Failure-stop boundaries are narrower:

- air failure boundary:
  - `continue_air`
  - `stop_air`
- ground failure boundary:
  - `continue_ground`
  - `stop_without_save`

Only expose the orchestrator-returned `allowed_actions`.

## Recovery And Commit Boundary

The current restore truth sources are:

- `agent_tuning.last_committed_request_id`
- `agent_tuning.failure_trace`
- `agent_tuning.recovery_state`

Meaning:

- if a round result exists beyond `last_committed_request_id`, it is treated as uncommitted and ignored on restore
- if `failure_trace.type` is `worker_circuit_break`, the failed request is already consumed and restore must advance past it
- if `failure_trace.type` is `decision_error_exhausted` or `decision_timeout_exhausted`, the failed request is not consumed and restore may continue the same round
- `manual_resume` is written only when the user continues after a failure-stop boundary

## Round Persistence

The single-round transaction now follows this order:

1. write `round_result.pending.json`
2. append one `decision_trace` row with `status="pending"`
3. atomically rename the pending result to the final round result path
4. commit session state, including `last_committed_request_id`
5. append one `decision_trace` row with `status="committed"`

## Logs

- `logs/current_tuning_profile.json`
- `logs/agent_rounds/<stage>/`
- `logs/agent_waveforms/<stage>/`
- `logs/agent_decision_trace.jsonl`

Use evidence in this order:

1. round JSON and structured scores
2. decision trace
3. waveform summary and raw waveform files only when needed

## Real-Run Checklist

Before a real run:

1. confirm the serial port and autotune mode are correct
2. confirm whether the profile should start fresh or resume intentionally
3. confirm no stale `waiting_user` boundary is being resumed by accident
4. confirm suction, load state, and space match the intended stage

After a batch:

1. inspect `current_tuning_profile.json`
2. inspect `agent_rounds/<stage>/`
3. inspect `agent_decision_trace.jsonl`
4. inspect waveform files only when structure and behavior disagree

## Residual Risks

- The runtime still depends on the decision agent returning valid structured JSON.
- Different motors may still require prompt and knowledge tuning even with the same orchestrator.
- The skill lives outside the repo, so repo-side runtime changes must be kept in sync with the external skill file.

# Agent Autotune Entry Point

The Codex skill is the only user entrypoint for this workflow.

## Stage Flow

- Automatic stages:
  - `pwm_map`
  - `pwm_identify`
- Batch stages:
  - `air_dual`
  - `ground_dual`

The skill reads `logs/current_tuning_profile.json`, auto-runs `pwm_map` and `pwm_identify` when required profile fields are missing, then drives the batch stages in 10-round worker runs.

The repo-side batch orchestrator now lives in `host/agent_orchestrator.py`.
It is responsible for:

- auto-running `pwm_map` and `pwm_identify` when the profile is missing prerequisites
- starting or resuming a 10-round `air_dual` or `ground_dual` batch
- appending `agent_decision_trace.jsonl` after every round
- writing `last_batch_summary`, `last_batch_best`, and `pending_user_action` at the batch boundary
- honoring explicit boundary actions such as `continue_air`, `enter_ground`, `continue_ground`, `save`, and `stop_without_save`

## Step-Worker Command Templates

Use these command shapes for worker execution:

```bash
python tools/vofa_autotune.py --mode pwm-map --profile-path ...
python tools/vofa_autotune.py --mode pwm-identify --profile-path ...
python tools/vofa_autotune.py --mode air-dual-step --profile-path ... --batch-id ... --round-index ... --candidate-json ... --baseline-json ... --result-json ... --waveform-path ...
python tools/vofa_autotune.py --mode ground-dual-step --profile-path ... --batch-id ... --round-index ... --candidate-json ... --baseline-json ... --result-json ... --waveform-path ...
```

## Logs

- `logs/agent_rounds/`
- `logs/agent_waveforms/`
- `logs/agent_decision_trace.jsonl`

## Batch Execution Checklist

For each `air_dual` or `ground_dual` batch:

1. Read `current_tuning_profile.json`.
2. Recover `agent_tuning`, `active_batch`, and `pending_user_action`.
3. If the profile is missing `pwm_map` or `pwm_identify.seed_pi`, run those stages first.
4. Start or resume the current 10-round batch.
5. For each round:
   - read the previous round JSON first
   - compare `combined_score` before any waveform inspection
   - adjust `Kp` first, then `Ki`, and only consider `Kd` after repeated overshoot
   - if the last move made the score worse, roll back toward the current batch best
   - append one decision row to `agent_decision_trace.jsonl`
6. After round 10, write `last_batch_summary`, `last_batch_best`, and `pending_user_action`.
7. Only expose action words at the batch boundary.

## Resume Rules

- If `workflow_status=running` and `current_round_index < 10`, resume from `current_round_index + 1`.
- If `workflow_status=waiting_user`, do not run another round until the user chooses a legal action word.
- `save` is only valid after a completed `ground_dual` batch.

## Batch Actions

Expose user action words only at batch boundaries:

- `continue_air`
- `enter_ground`
- `stop_air`
- `continue_ground`
- `save`
- `stop_without_save`

## Decision Policy

- Use structure first.
- Use waveform second.
- Prefer structured metrics, scores, and traces for the primary decision.
- Inspect waveforms only as secondary evidence when metrics conflict, look noisy, or need confirmation.
- Keep `save` explicit. Do not treat a batch as saved until the user chooses `save`.

## Preflight Checklist

Before the first real hardware run:

1. Confirm the target serial port is correct and the car can enter autotune mode.
2. Confirm `logs/current_tuning_profile.json` is either empty/new or intentionally resumed from a known batch.
3. Confirm the profile already contains valid `shared_targets` and `pwm_map` results, or allow the orchestrator to regenerate them.
4. Confirm `pwm_identify.seed_pi` exists, or allow the orchestrator to regenerate it before entering `air_dual`.
5. Confirm no stale `pending_user_action` is blocking the workflow unless you really want to resume from that batch boundary.
6. Confirm suction, load state, and safety space match the intended stage before choosing `enter_ground`.

## Real-Run Checks

After an `air_dual` or `ground_dual` batch finishes, inspect:

- `logs/current_tuning_profile.json`
- `logs/agent_rounds/<stage>/`
- `logs/agent_decision_trace.jsonl`
- `logs/agent_waveforms/<stage>/` only when structured metrics conflict or regress unexpectedly

What to verify:

- `agent_tuning.workflow_status` becomes `waiting_user` at the batch boundary
- `last_batch_summary.evaluated_groups` is `10`
- `last_batch_best.best_pid` and `last_batch_summary.best_pid` agree
- `ground_dual.best_pid` stays empty until the explicit `save` action is applied

## Commit Hygiene

The repo ignores runtime outputs created by the agent workflow:

- `logs/agent_candidates/`
- `logs/agent_rounds/`
- `logs/agent_waveforms/`
- `logs/agent_decision_trace.jsonl`
- Python `__pycache__` and `*.pyc`

Before committing, stage only the intentional source changes and docs. Leave generated telemetry and local run artifacts out of the commit.

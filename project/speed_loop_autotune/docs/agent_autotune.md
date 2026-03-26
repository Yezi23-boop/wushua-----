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

## Step-Worker Command Templates

Use these command shapes for worker execution:

```bash
python tools/vofa_autotune.py --mode pwm-map --profile-path ...
python tools/vofa_autotune.py --mode pwm-identify --profile-path ...
python tools/vofa_autotune.py --mode air-dual-step --candidate-json ... --baseline-json ... --result-json ...
python tools/vofa_autotune.py --mode ground-dual-step --candidate-json ... --baseline-json ... --result-json ...
```

## Logs

- `logs/agent_rounds/`
- `logs/agent_waveforms/`
- `logs/agent_decision_trace.jsonl`

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


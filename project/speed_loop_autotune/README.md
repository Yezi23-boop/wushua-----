# Speed Loop Autotune

This directory contains the autotune workflow, shared profile, protocol docs, and tuning notes for the speed-loop project.

## What Lives Here

- `host/` for the Python-side tuning worker logic.
- `firmware/` for the target-side command handling, runtime state, and component wrappers.
- `docs/` for protocol, tuning rules, debug notes, and scoring guidance.
- `tests/` for host-side unit tests and structural checks.

## Current Workflow

- The autotune skill is the only user-facing entry point.
- The decision path is `skill -> speed_loop_tuning agent -> agent_orchestrator -> step worker`.
- The skill reads `project/speed_loop_autotune/logs/current_tuning_profile.json` first.
- If required profile fields are missing, the skill may auto-run `pwm_map` and `pwm_identify`.
- `air_dual` and `ground_dual` now run through the request/response orchestrator state machine, not the retired direct rule loop.
- Boundary actions are only exposed after a batch completes.
- `save` is only a `ground_dual` batch-boundary action.
- Structured metrics are the primary evidence; waveforms are secondary evidence.

## Four-Stage Chain

- `pwm_map`
  Open-loop PWM dead-zone calibration and shared target preparation.
- `pwm_identify`
  Open-loop step identification that produces the initial left/right `seed_pi`.
- `air_dual`
  Airborne dual-wheel LLM-driven batch tuning.
- `ground_dual`
  Loaded dual-wheel LLM-driven batch verification and final convergence.

For the full batch flow, boundary actions, and log locations, see `docs/agent_autotune.md`.

## Runtime States

- `decision_required`
  - orchestrator has prepared one round request for the decision agent
- `waiting_user`
  - orchestrator has stopped at a legal user boundary
- `completed`
  - the current workflow branch is done

The current restore truth sources are:

- `agent_tuning.last_committed_request_id`
- `agent_tuning.failure_trace`
- `agent_tuning.recovery_state`

## Profile Fallbacks

- Prefer `shared_targets.custom_sequences.*` first.
- If custom sequences are missing, prefer the profile's low/mid templates.
- If worker code still contains older hard-coded defaults, treat them as worker fallback defaults only.
- `air_dual` should start from `air_dual.active_batch.current_best_pid`, then `air_dual.last_batch_best.best_pid`, then `air_dual.best_pid`, then `pwm_identify.seed_pi`, then CLI defaults.
- `ground_dual` should start from `ground_dual.active_batch.current_best_pid`, then `ground_dual.best_pid`, then `air_dual.best_pid`, then `pwm_identify.seed_pi`, then CLI defaults.

## Confirmed PWM Range

- Wheel motor PWM commands use `0~10000`.
- `air_dual`, `ground_dual`, `pwm_identify`, and `pwm_map` all use that wheel PWM range.
- `AT_FUYA` is a separate vacuum command and stays limited to `0~4000`.

## Telemetry Columns

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

## Reading Guide

- `docs/protocol.md`
  Skill-managed batch protocol and boundary actions.
- `docs/tuning_rules.md`
  `air_dual` batch tuning rules.
- `docs/ground_dual_tuning_rules.md`
  `ground_dual` batch tuning rules and save gate.
- `docs/pwm_map_rules.md`
  `pwm_map` prerequisites and mapping notes.
- `docs/pwm_identify_rules.md`
  `pwm_identify` seed generation rules.

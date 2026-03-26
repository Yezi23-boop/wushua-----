# Ground Dual Speed Loop Tuning Rules

This file stays aligned with the Codex skill entrypoint in `docs/agent_autotune.md`. The agent runs the batch flow, keeps `save` explicit at batch boundaries, and uses structured metrics first with waveform as secondary evidence.

## Goal

- Verify loaded dual-wheel recovery under real friction, supply sag, and stop conditions.
- Keep the focus on whole-trial repeatability instead of a single low score.

## Default Environment

- Serial port: `COM8`
- Mode: `ground-dual`
- Vacuum: set as needed for the real car, not forced to `AT_FUYA=0`
- Default save behavior: no automatic `SAVE`

## Default Sequence

- `25:200,35:200,45:200,35:200,25:200`

## Default Command Chain

1. `AT_FUYA=<value>`
2. `AT_COOLDOWN_MS=<value>`
3. `AT_ARM`
4. `AT_TRIAL_MS=<value>`
5. `AT_SPEED=<first_target>`
6. `AT_FIRE`

## Scoring Focus

- Integrated score across all segments.
- `stop_flag`
- `trial_active`
- Remaining speed during the cooldown segment.
- Remaining PWM during the cooldown segment.

## Usage Boundary

- `ground-dual` only performs loaded recovery and conservative screening.
- Do not do open-loop PWM identification in this mode.
- If `air-dual` is better in isolation but `ground-dual` is clearly worse, prefer the conservative fallback.

## Agent Batch Policy

- Use the skill entrypoint from `docs/agent_autotune.md`.
- Run `ground_dual` as a 10-round batch.
- Stop at the batch boundary for the user's action word.
- Keep `save` explicit and do not auto-save after `ground_dual`.
- Treat waveform as secondary evidence when structured metrics already explain the result.


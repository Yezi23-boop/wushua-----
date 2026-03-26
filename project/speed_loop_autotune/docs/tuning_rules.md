# Air Dual Speed Loop Tuning Rules

This file covers the current `air_dual` batch workflow. The retired single-wheel-then-coupling model is no longer the primary description here.

## Goal

- Tune the left and right wheels as a coordinated dual-wheel batch.
- Prefer stable, repeatable batch results over a one-shot minimum score.
- Keep the workflow structured first and waveform second.

## PWM Limits

- Wheel motor PWM should be treated as `0~10000`.
- `AT_FUYA` is a separate vacuum command and should be treated as `0~4000`.
- Real hardware verification on `2026-03-24` over `COM8` confirmed that wheel commands at `5000`, `7000`, and `9000` were applied as real PWM.

## Batch Flow

- Use the skill entrypoint from `docs/agent_autotune.md`.
- `air_dual` runs as 10-round batches.
- The skill may auto-run `pwm_map` and `pwm_identify` before `air_dual` if the current profile is missing required fields.
- The batch boundary actions are limited to `continue_air`, `enter_ground`, and `stop_air`.
- `save` is not an air-stage action.

## Sequence Selection

- Prefer `shared_targets.custom_sequences.air_primary` and `shared_targets.custom_sequences.air_verify` when they exist.
- If custom sequences are missing, prefer the profile's low/mid templates.
- If worker code still contains older hard-coded defaults, name them explicitly as worker fallback defaults instead of preferred defaults.
- Do not describe `15/25/35/45` nominal labels as the preferred fallback source.

## Tuning Order

- Tune `Kp` first.
- Tune `Ki` next.
- Use `Kd` only when repeated overshoot or oscillation cannot be controlled by the first two terms.

Current default search step guidance:
- `Kp = 10`
- `Ki = 5`
- `Kd = 0.5`
- Stop shrinking once the search step reaches `1.0`.

## Evaluation

- Compare structured metrics across the full 10-round batch.
- Focus on response, overshoot, settling, steady-state error, and speed drop under dual-wheel load.
- Use waveform data only when the structured result is noisy or contradictory.
- Favor the candidate that is stable across both the primary and verify sequences.

## Update Rules

- New `air_dual` experience should land in `debug_memory.md` first.
- Promote a rule into this file only after repeated verification.

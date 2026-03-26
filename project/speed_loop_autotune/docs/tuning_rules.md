# Air Dual Speed Loop Tuning Rules

This file covers the current `air_dual` batch workflow. The retired single-wheel-then-coupling model is no longer the primary description here.

## Goal

- Tune the left and right wheels as a coordinated dual-wheel batch.
- Prefer stable, repeatable batch results over a one-shot minimum score.
- Keep the workflow structured first and waveform second.

## PWM Limits

- Wheel motor PWM should be treated as `0~10000`.
- `AT_FUYA` is a separate vacuum command and should be treated as `0~4000`.

## Batch Flow

- `air_dual` runs as 10-round batches.
- The skill may auto-run `pwm_map` and `pwm_identify` before `air_dual` if the current profile is missing required fields.
- The batch should start from the profile's current best candidate.
- The batch boundary actions are limited to `continue_air`, `enter_ground`, and `stop_air`.

## Sequence Selection

- Prefer `shared_targets.custom_sequences.air_primary` and `shared_targets.custom_sequences.air_verify` when they exist.
- If custom sequences are missing, prefer the profile's low/mid templates.
- If worker code still contains older hard-coded defaults, name them explicitly as worker fallback defaults. Do not describe them as the preferred source.
- Do not present `high/top` expansion as the default fallback path.

## Tuning Order

- Tune `Kp` first.
- Tune `Ki` next.
- Use `Kd` only when repeated overshoot or oscillation cannot be controlled by the first two terms.

## Evaluation

- Compare structured metrics across the full 10-round batch.
- Use waveform data only when the structured result is noisy or contradictory.
- Favor the candidate that is stable across both the primary and verify sequences.

## Legacy Language

- Do not describe the workflow as "single-wheel first, coupling later".
- If a note still mentions that retired model, treat it as historical context only.

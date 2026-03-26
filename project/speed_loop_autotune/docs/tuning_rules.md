# Air Dual Speed Loop Tuning Rules

This file stays aligned with the Codex skill entrypoint in `docs/agent_autotune.md`. The agent runs the batch flow, keeps `save` explicit at batch boundaries, and uses structured metrics first with waveform as secondary evidence.

## Confirmed PWM Limits

- Wheel motor PWM should be treated as `0~10000`.
- This wheel PWM range is the default assumption for `air-dual`, `ground-dual`, `pwm-identify`, and `pwm-map`.
- `AT_FUYA` is a separate vacuum command and should be treated as `0~4000`.
- Real hardware verification on `2026-03-24` over `COM8` confirmed that wheel commands at `5000`, `7000`, and `9000` were applied as real PWM.
- `air-dual` default targets should now come from the shared tuning profile, not from the old nominal `15/25/35/45` labels.
- `air-dual` should prefer `pwm-identify.seed_pi` as the initial PID pair when the shared profile already contains it.
- When `custom_sequences` is absent, `air-dual` and `ground_dual` should default to the profile's low-mid section, using `low/mid` targets instead of expanding to `high/top`.

## Goal

- Find one reusable closed-loop speed PID set for the left and right wheels.
- Tune single-wheel isolation first, then dual-wheel coupling.
- Prefer repeatable multi-speed stability over a single lowest score.

## Default Environment

- Serial port: `COM8`
- Mode: `air-dual`
- Vacuum: off, fixed `AT_FUYA=0`
- Default save behavior: no automatic `SAVE`

## Default Speed Sequences

Main sequence:
- `15:500,25:500,35:500,45:500,35:500,25:500,15:500`

Verification sequence:
- `15:300,25:300,35:300,45:300,35:300,25:300,15:300`

Both sequences append `TEST_speed=0` at the tail and keep the tail return-to-zero inside the sampling window.

## Tuning Order

1. Tune `Kp`.
2. Tune `Ki`.
3. Try `Kd` only when repeated multi-wheel overshoot still cannot be suppressed.

Current default step:
- `Kp = 10`
- `Ki = 5`
- `Kd = 0.5`
- Stop when search-step shrink reaches `1.0`.

## Single-Wheel Isolation

- When tuning the left wheel, keep the right wheel fixed at `0/0/0`.
- When tuning the right wheel, keep the left wheel fixed at `0/0/0`.

## Dual-Wheel Coupling

- After single-wheel coarse tuning, allow a small-range dual-wheel coupled fine tune.
- Dual-wheel scoring should still prefer repeatable stability over the lowest single-stage number.

## Scoring Focus

- Start response.
- Overshoot.
- Settling time.
- Steady-state error.
- Speed drop when both wheels run at high PWM together.

## Update Rules

- New `air-dual` experience goes into `debug_memory.md` first.
- Promote a rule into this file only after it has been verified repeatedly.

## Agent Batch Policy

- Use the skill entrypoint from `docs/agent_autotune.md`.
- Run `air_dual` as a 10-round batch.
- Stop at the batch boundary for the user's action word.
- Keep `save` explicit and do not auto-save after `air_dual`.
- Treat waveform as secondary evidence when structured metrics already explain the result.


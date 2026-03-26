# Speed Loop Autotune

This project centralizes the speed-loop autotune workflow, including the Codex skill entrypoint, worker command contract, tuning rules, and tuning logs.

## Entry Point

- Use the Codex skill described in `docs/agent_autotune.md` as the only user entrypoint.
- The workflow auto-runs `pwm_map` and `pwm_identify` when required profile fields are missing.
- Batch stages run in 10-round worker sequences for `air_dual` and `ground_dual`.
- `save` is explicit and only available at the ground batch boundary.
- Structured metrics are the primary evidence; waveforms are secondary evidence.

## Stages

### `pwm_map`

- Open-loop PWM dead-zone calibration.
- Updates the shared profile with wheel dead zones and PWM-to-speed mapping.

### `pwm_identify`

- Open-loop PWM step-response identification.
- Produces left/right seed `PI` values from the identified dead-zone region.

### `air_dual`

- Airborne dual-wheel closed-loop speed tuning.
- Uses the shared profile and the `pwm_identify` seed values when available.

### `ground_dual`

- Loaded dual-wheel regression and final quality verification.
- Reuses the best air-stage result as the starting point.

For the full batch flow, boundary actions, and log locations, see `docs/agent_autotune.md`.

## Shared Profile

The current standard handoff file is `project/speed_loop_autotune/logs/current_tuning_profile.json`.

It carries the four-stage chain:

- `pwm_map`
- `pwm_identify`
- `air_dual`
- `ground_dual`

## Standard Tuning Flow

### Step 1: Run `pwm-map`

- Output the raw CSV.
- Update the shared profile.
- Record wheel dead zones.

### Step 2: Run `pwm-identify`

- Read the shared profile.
- Reuse the `pwm_map` dead-zone result.
- Write left/right seed `PI` back to the shared profile.

### Step 3: Run `air-dual`

- Read the real encoder targets from the shared profile.
- Prefer `pwm-identify.seed_pi` as the initial PID when present.
- Write the best air-stage result back to the shared profile.
- Use the low/mid default template when no custom sequence exists.

### Step 4: Run `ground-dual`

- Read the real ground targets from the shared profile.
- Prefer the best `air_dual` result as the starting point.
- Write the final loaded result back to the shared profile.
- Use the low/mid default template when no custom sequence exists.

## Host Boundary

- `host/common.py`
- `host/pwm_map.py`
- `host/pwm_identify.py`
- `host/air_dual.py`
- `host/ground_dual.py`
- `host/vofa_autotune.py`

## Firmware Boundary

- `firmware/autotune_component.*`
- `firmware/speed_loop_autotune.h`
- `firmware/speed_loop_autotune_private.h`
- `firmware/autotune_binding.*`
- `firmware/autotune_port.*`
- `firmware/autotune_pid_core.*`
- `firmware/autotune_token_registry.*`
- `firmware/air_dual_mode.*`
- `firmware/ground_dual_mode.*`
- `firmware/pwm_identify_mode.*`
- `firmware/autotune_runtime.*`
- `firmware/host_autotune_command.*`
- `firmware/speed_loop_trial.*`
- `service/speed_loop_autotune_adapter.*`

## Current Telemetry

Keep the first 7 columns and append 3 more at the tail:

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

## Confirmed PWM Range

- Wheel motor PWM commands use the real `0~10000` range.
- `air_dual`, `ground_dual`, `pwm_identify`, and `pwm_map` all use that wheel PWM range.
- `AT_FUYA` is a separate vacuum command and stays limited to `0~4000`.

## Reading Guide

- `pwm-map`
  - `docs/pwm_map_rules.md`
- `pwm-identify`
  - `docs/pwm_identify_rules.md`
  - `docs/pwm_identify_debug_memory.md`
- `air-dual`
  - `docs/tuning_rules.md`
  - `docs/debug_memory.md`
- `ground-dual`
  - `docs/ground_dual_tuning_rules.md`
  - `docs/ground_dual_debug_memory.md`

## `custom_sequences` Manual Edit

- path:
  - `project/speed_loop_autotune/logs/current_tuning_profile.json`
- keys:
  - `shared_targets.custom_sequences.air_primary`
  - `shared_targets.custom_sequences.air_verify`
  - `shared_targets.custom_sequences.ground_forward`
- priority:
  - use `custom_sequences` first
  - fallback to `default_sequences` when custom value is missing
  - current default template uses `bands.low/mid` only


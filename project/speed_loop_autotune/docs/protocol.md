# Speed Loop Autotune Protocol

This document describes the current skill-managed protocol for speed-loop autotuning.

## Entry Point

- The autotune skill is the only user-facing entry point for this workflow.
- The skill reads `project/speed_loop_autotune/logs/current_tuning_profile.json` first.
- If required profile fields are missing, the skill may auto-run `pwm_map` and `pwm_identify` before tuning starts.
- The host parser now exposes step worker modes, but user-facing docs should still describe the skill-managed workflow first.

## Open-Loop Startup Confirmation

- `pwm_map` and `pwm_identify` now use the same startup confirmation protocol.
- Host sends `AT_START_SEQ=<n>` before each open-loop `START`.
- Firmware latches the current request sequence and reports the startup state in telemetry.
- Host must not begin capture until it sees two consecutive telemetry frames that confirm:
  - `mode_id == 2`
  - `start_seq == current request sequence`
  - `start_state == 2`
  - the commanded wheel PWM matches the current request
- If confirmation times out once, host performs `AT_RESET` and retries the same request parameters one time.
- If confirmation still fails after the retry, host must stop and report the last observed startup state.

## Batch Model

- `air_dual` runs in 10-round batches.
- `ground_dual` runs in 10-round batches.
- Each round produces structured metrics first.
- Waveform data is secondary evidence only.

## Boundary Actions

Action words are only exposed at batch boundaries, never in the middle of a batch.

### Air batch boundary

- `continue_air`
- `enter_ground`
- `stop_air`

### Ground batch boundary

- `continue_ground`
- `save`
- `stop_without_save`

## Save Gate

- `save` is explicit.
- `save` is only valid at the end of a completed `ground_dual` batch.
- No other stage should treat save as a normal action.

## Decision Policy

- Use structured scores, round summaries, and profile state as the primary evidence.
- Use waveforms only to confirm a conflict, explain noise, or inspect an ambiguous result.
- Do not let a waveform impression override stable structured evidence by default.

## Implementation Note

- Step worker CLI details are implementation-facing.
- The stable contract for this doc is the batch flow, boundary actions, save gate, and evidence policy.

## Telemetry Compatibility

- The first 10 telemetry columns remain unchanged.
- Open-loop runs may append two more columns:
  - `start_seq`
  - `start_state`
- Host code must accept both 10-column legacy frames and 12-column confirmed-start frames.

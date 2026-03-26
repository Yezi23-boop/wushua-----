# Speed Loop Autotune

This directory contains the autotune workflow, shared profile, protocol docs, and tuning notes for the speed-loop project.

## What Lives Here

- `host/` for the Python-side tuning worker logic.
- `firmware/` for the target-side command handling, runtime state, and component wrappers.
- `docs/` for protocol, tuning rules, debug notes, and scoring guidance.
- `tests/` for host-side unit tests and structural checks.

## Current Workflow

- The autotune skill is the user-facing entry point.
- The skill reads `logs/current_tuning_profile.json` first.
- If required profile fields are missing, the skill may auto-run `pwm_map` and `pwm_identify` before batch tuning starts.
- `air_dual` and `ground_dual` both use 10-round batches.
- Boundary actions are only exposed after a batch completes.
- `save` is only a ground-stage boundary action.
- Structured metrics are the primary evidence; waveforms are secondary.

## Profile Fallbacks

- Prefer the profile's custom sequences first.
- If custom sequences are missing, use the profile's low/mid templates.
- If worker code still contains older hard-coded defaults, treat them as worker fallback defaults only.

## Docs

- `docs/protocol.md` describes the current batch protocol.
- `docs/tuning_rules.md` covers `air_dual`.
- `docs/ground_dual_tuning_rules.md` covers `ground_dual`.
- `docs/pwm_map_rules.md` and `docs/pwm_identify_rules.md` cover the prerequisite calibration stages.

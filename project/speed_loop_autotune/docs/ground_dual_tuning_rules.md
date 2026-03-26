# Ground Dual Speed Loop Tuning Rules

This file covers the current `ground_dual` batch workflow and the final save gate.

## Goal

- Validate the dual-wheel controller under load, supply sag, and stop conditions.
- Prefer repeatable batch quality over a single best round.

## Batch Flow

- `ground_dual` runs as 10-round batches.
- The current profile should be read before the batch starts.
- The batch should start from the best candidate already stored in the profile.
- The batch boundary actions are limited to `continue_ground`, `save`, and `stop_without_save`.
- `save` is only valid after a completed `ground_dual` batch.

## Sequence Selection

- Prefer `shared_targets.custom_sequences.ground_forward` when present.
- If custom ground sequences are missing, prefer the profile's low/mid template `ground_forward = [low, mid, low]`.
- If worker code still carries older hard-coded defaults, call them worker fallback defaults explicitly.
- Do not describe worker fallback defaults as the preferred source.

## Evaluation

- Check the combined batch score.
- Check `stop_flag` and `trial_active`.
- Check residual speed and residual PWM after the cooldown phase.
- Use waveform data only to confirm a structured result, not to replace it.

## Save Rule

- `save` means the current ground batch is accepted into the profile.
- If the batch is not stable enough, choose `continue_ground` or `stop_without_save` instead.
- Do not expose save as an air-stage action.

## Boundary Reminder

- `continue_ground` keeps tuning within the ground stage.
- `save` closes the ground stage with persistence.
- `stop_without_save` exits without changing the saved profile.

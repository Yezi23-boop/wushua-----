# air-dual Three-Stage Tuning

## Summary

- `air-dual` now uses dual-wheel batch tuning.
- Default order:
  1. coarse `P`
  2. coarse `I`
  3. fine `P`
  4. fine `I`
  5. micro `P`
  6. micro `I`
- Every batch evaluates `10` candidates and keeps the lowest single-sequence `score` as the batch best.

## Batch Rule

- Every batch changes only one parameter family:
  - `P` batch keeps `I` and `Kd` fixed
  - `I` batch keeps `P` and `Kd` fixed
- Both wheels move together:
  - `L.Kp` and `R.Kp` use the same offset in a `P` batch
  - `L.Ki` and `R.Ki` use the same offset in an `I` batch
- The default `10` candidates are:
  1. baseline
  2. `-4 step`
  3. `-3 step`
  4. `-2 step`
  5. `-1 step`
  6. `+1 step`
  7. `+2 step`
  8. `+3 step`
  9. `+4 step`
  10. adaptive edge extension or baseline repeat
- Adaptive rule:
  - if the best candidate is still on the high edge, the last slot becomes `+5 step`
  - if the best candidate is still on the low edge, the last slot becomes `-5 step`
  - otherwise the last slot repeats baseline as a stability check

## Presets

- coarse
  - `Kp step=10`
  - `Ki step=5`
  - `repeat_each=1`
- fine
  - `Kp step=5`
  - `Ki step=2`
  - `repeat_each=2`
- micro
  - `Kp step=2`
  - `Ki step=1`
  - `repeat_each=3`

## Advance Rule

- If the batch best stays on the outer edge, keep the same preset and expand again.
- If two same-preset batches both keep the best candidate in the middle region, advance to the next preset or the next parameter family.
- `Kd` stays at `0` in the default flow and is only for later manual experiments.

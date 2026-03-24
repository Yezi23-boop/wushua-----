# PWM Map Rules

`pwm-map` is the open-loop calibration mode for building a per-wheel PWM-to-encoder baseline table.

## Defaults

- `map_pwm_step = 500`
- `map_pwm_max = 10000`
- `map_repeat = 2`
- `map_hold_ms = 250`
- `map_tail_zero_ms = 200`
- scan order always starts at `0`

## Flow

- test left wheel with `L_TEST_PWM=<value>` and `R_TEST_PWM=0`
- test right wheel with `R_TEST_PWM=<value>` and `L_TEST_PWM=0`
- each level uses:
  - `AT_RESET`
  - wait `rest_seconds`
  - `AT_TEST_MODE=1`
  - set PWM commands
  - `START`
  - hold for `map_hold_ms`
  - tail-zero for `map_tail_zero_ms`
  - `AT_TEST_MODE=0`
  - `AT_RESET`

## Deadzone

- the scan begins at `0 PWM`
- the first level whose active window contains 3 consecutive samples with absolute encoder speed `> 5.0` is marked as `deadzone_break_pwm`
- the mode keeps scanning after the deadzone is found

## Aggregation

- low PWM levels that do not move are still recorded
- `steady_encoder` is the average absolute encoder value over the last 20% of the active PWM window
- `peak_encoder` is the maximum absolute encoder value in the active PWM window
- repeated runs are aggregated by median
- if any repeat has empty telemetry, no active PWM window, or `stop_flag=1`, the whole `pwm-map` run fails immediately

## Confirmed Range Notes

- Wheel motor PWM is confirmed to cover the real `0~10000` range after flashing the fresh firmware.
- `pwm-map` should therefore be read as a wheel PWM calibration table over `0~10000`, not `0~4000`.
- `AT_FUYA` is not part of the wheel PWM map and still uses `0~4000`.
- Real hardware verification on `2026-03-24` over `COM8` confirmed applied wheel PWM at `5000`, `7000`, and `9000`.

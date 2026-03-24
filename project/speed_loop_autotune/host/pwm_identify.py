import collections
import time

from .common import (
    PidGains,
    WheelPidGains,
    _median_value,
    apply_speed_gains,
)


IdentifyLevelMetrics = collections.namedtuple(
    "IdentifyLevelMetrics",
    ["pwm_command", "steady_speed", "theta_s", "tau_s", "valid"],
)

DEFAULT_IDENTIFY_PWM_STEP = 200
DEFAULT_IDENTIFY_PWM_MAX = 3200
DEFAULT_IDENTIFY_REPEAT = 2
DEFAULT_IDENTIFY_HOLD_MS = 250
DEFAULT_IDENTIFY_TAIL_ZERO_MS = 200
DEFAULT_IDENTIFY_SAMPLE_PERIOD_S = 0.02
DEFAULT_IDENTIFY_DISCRETE_TS_S = 0.005
DEFAULT_IDENTIFY_MIN_MOTION_SPEED = 5.0
DEFAULT_IDENTIFY_REQUIRED_CONSECUTIVE_SAMPLES = 3
DEFAULT_IDENTIFY_MIN_VALID_LEVELS = 2
DEFAULT_IDENTIFY_MAX_VALID_LEVELS = 3


def _clamp_identify_pwm(value):
    pwm_value = int(round(float(value)))
    if pwm_value < 0:
        return 0
    if pwm_value > 4000:
        return 4000
    return pwm_value


def _build_pwm_command(wheel_name, pwm_value):
    if wheel_name == "left":
        return ("L_TEST_PWM={0}".format(int(pwm_value)), "R_TEST_PWM=0")
    if wheel_name == "right":
        return ("L_TEST_PWM=0", "R_TEST_PWM={0}".format(int(pwm_value)))
    raise ValueError("wheel_name must be 'left' or 'right'")


def run_pwm_identify_trial(
    client,
    wheel_name,
    pwm_value,
    hold_ms,
    tail_zero_ms,
    rest_seconds,
    sleep_fn=time.sleep,
):
    pwm_value = _clamp_identify_pwm(pwm_value)
    active_command, idle_command = _build_pwm_command(wheel_name, pwm_value)
    zero_active_command, zero_idle_command = _build_pwm_command(wheel_name, 0)

    client.send_command("AT_RESET")
    sleep_fn(rest_seconds)
    client.send_command("AT_TEST_MODE=1")
    client.send_command(active_command)
    client.send_command(idle_command)
    client.send_command("START")
    samples = client.capture_trial(
        float(hold_ms + tail_zero_ms) / 1000.0,
        events=[
            (float(hold_ms) / 1000.0, zero_active_command),
            (float(hold_ms) / 1000.0, zero_idle_command),
        ],
    )
    client.send_command("AT_TEST_MODE=0")
    client.send_command("AT_RESET")
    return samples


def _extract_active_identify_samples(samples, wheel_name, command_pwm):
    active = []
    target_value = float(command_pwm)

    for sample in samples:
        if wheel_name == "left":
            current_command = sample.left_cmd_pwm
        else:
            current_command = sample.right_cmd_pwm

        if abs(current_command - target_value) <= 0.5:
            active.append(sample)

    if active:
        return active

    return list(samples)


def extract_identify_level_metrics(
    samples,
    wheel_name,
    command_pwm,
    sample_period_s=DEFAULT_IDENTIFY_SAMPLE_PERIOD_S,
    min_motion_speed=DEFAULT_IDENTIFY_MIN_MOTION_SPEED,
    required_consecutive_samples=DEFAULT_IDENTIFY_REQUIRED_CONSECUTIVE_SAMPLES,
):
    active_samples = _extract_active_identify_samples(samples, wheel_name, command_pwm)
    responses = []
    consecutive = 0
    detected_motion = 0

    for sample in active_samples:
        if wheel_name == "left":
            response = abs(sample.left_speed)
        else:
            response = abs(sample.right_speed)
        responses.append(response)
        if response > min_motion_speed:
            consecutive += 1
            if consecutive >= required_consecutive_samples:
                detected_motion = 1
        else:
            consecutive = 0

    if not responses:
        return IdentifyLevelMetrics(float(command_pwm), 0.0, 0.0, 0.0, False)

    tail_count = int(len(responses) * 0.2 + 0.9999)
    if tail_count < 1:
        tail_count = 1
    steady_values = responses[-tail_count:]
    steady_speed = sum(steady_values) / float(len(steady_values))

    if steady_speed <= 0.0:
        return IdentifyLevelMetrics(float(command_pwm), 0.0, 0.0, 0.0, False)

    theta_threshold = steady_speed * 0.1
    tau_threshold = steady_speed * 0.632
    theta_index = 0
    tau_index = 0
    found_theta = 0
    found_tau = 0

    for index, value in enumerate(responses):
        if not found_theta and value >= theta_threshold:
            theta_index = index
            found_theta = 1
        if not found_tau and value >= tau_threshold:
            tau_index = index
            found_tau = 1
        if found_theta and found_tau:
            break

    if not found_theta or not found_tau or tau_index < theta_index:
        return IdentifyLevelMetrics(float(command_pwm), steady_speed, 0.0, 0.0, False)

    return IdentifyLevelMetrics(
        float(command_pwm),
        steady_speed,
        theta_index * sample_period_s,
        (tau_index - theta_index) * sample_period_s,
        bool(detected_motion and steady_speed > min_motion_speed),
    )


def _aggregate_identify_level_metrics(level_runs, command_pwm):
    valid_runs = [level for level in level_runs if level.valid]
    if not valid_runs:
        return IdentifyLevelMetrics(float(command_pwm), 0.0, 0.0, 0.0, False)

    return IdentifyLevelMetrics(
        float(command_pwm),
        _median_value([level.steady_speed for level in valid_runs]),
        _median_value([level.theta_s for level in valid_runs]),
        _median_value([level.tau_s for level in valid_runs]),
        True,
    )


def build_identify_seed_from_levels(levels, discrete_sample_s=DEFAULT_IDENTIFY_DISCRETE_TS_S):
    valid_levels = [level for level in levels if level.valid]
    if len(valid_levels) < DEFAULT_IDENTIFY_MIN_VALID_LEVELS:
        raise RuntimeError("identify requires at least two valid levels")

    valid_levels = sorted(valid_levels, key=lambda item: item.pwm_command)
    low_level = valid_levels[0]
    high_level = valid_levels[-1]
    delta_pwm = high_level.pwm_command - low_level.pwm_command
    delta_speed = high_level.steady_speed - low_level.steady_speed

    if delta_pwm <= 0.0 or delta_speed <= 0.0:
        raise RuntimeError("identify slope is invalid")

    process_gain = delta_speed / delta_pwm
    theta_s = _median_value([level.theta_s for level in valid_levels])
    tau_s = _median_value([level.tau_s for level in valid_levels])

    if process_gain <= 0.0 or theta_s < 0.0 or tau_s <= 0.0:
        raise RuntimeError("identify process metrics are invalid")

    tau_c = max(theta_s, 0.5 * tau_s)
    if tau_c <= 0.0:
        raise RuntimeError("identify tau_c is invalid")

    controller_gain = (1.0 / process_gain) * tau_s / (tau_c + theta_s)
    integral_time = min(tau_s, 4.0 * (tau_c + theta_s))

    if integral_time <= 0.0:
        raise RuntimeError("identify integral time is invalid")

    kp_value = controller_gain
    ki_value = controller_gain * discrete_sample_s / integral_time

    if kp_value < 0.0:
        kp_value = 0.0
    if ki_value < 0.0:
        ki_value = 0.0

    return PidGains(kp_value, ki_value, 0.0)


def _collect_wheel_levels(client, args, wheel_name):
    valid_levels = []
    pwm_value = int(args.identify_pwm_step)

    while pwm_value <= int(args.identify_pwm_max):
        run_metrics = []
        run_index = 0

        while run_index < int(args.identify_repeat):
            samples = run_pwm_identify_trial(
                client,
                wheel_name,
                pwm_value,
                hold_ms=args.identify_hold_ms,
                tail_zero_ms=args.identify_tail_zero_ms,
                rest_seconds=args.rest_seconds,
            )
            run_metrics.append(extract_identify_level_metrics(samples, wheel_name, pwm_value))
            run_index += 1

        level_metrics = _aggregate_identify_level_metrics(run_metrics, pwm_value)
        print(
            "identify {0} pwm={1} speed={2:.3f} theta={3:.3f} tau={4:.3f} valid={5}".format(
                wheel_name,
                pwm_value,
                level_metrics.steady_speed,
                level_metrics.theta_s,
                level_metrics.tau_s,
                int(level_metrics.valid),
            )
        )
        if level_metrics.valid:
            valid_levels.append(level_metrics)
            if len(valid_levels) >= DEFAULT_IDENTIFY_MAX_VALID_LEVELS:
                break

        pwm_value += int(args.identify_pwm_step)

    return valid_levels


def run_pwm_identify(client, args):
    if args.save_best:
        raise RuntimeError("pwm-identify does not support --save-best")

    left_levels = _collect_wheel_levels(client, args, "left")
    if len(left_levels) < DEFAULT_IDENTIFY_MIN_VALID_LEVELS:
        raise RuntimeError("left wheel identify did not reach two valid PWM levels")

    right_levels = _collect_wheel_levels(client, args, "right")
    if len(right_levels) < DEFAULT_IDENTIFY_MIN_VALID_LEVELS:
        raise RuntimeError("right wheel identify did not reach two valid PWM levels")

    seed_pair = WheelPidGains(
        build_identify_seed_from_levels(left_levels),
        build_identify_seed_from_levels(right_levels),
    )

    print(
        "identify seed left kp={0:.4f} ki={1:.4f} kd={2:.4f}".format(
            seed_pair.left.kp,
            seed_pair.left.ki,
            seed_pair.left.kd,
        )
    )
    print(
        "identify seed right kp={0:.4f} ki={1:.4f} kd={2:.4f}".format(
            seed_pair.right.kp,
            seed_pair.right.ki,
            seed_pair.right.kd,
        )
    )

    if args.apply_identify_seed:
        apply_speed_gains(client, seed_pair)
        client.send_command("AT_RESET")
        client.send_command("TEST_speed=0")

    return 0

import collections
import time

from .common import (
    PidGains,
    WheelPidGains,
    _median_value,
    apply_speed_gains,
    load_tuning_profile,
    pid_gains_to_dict,
    resolve_profile_path,
    save_tuning_profile,
)


IdentifyLevelMetrics = collections.namedtuple(
    "IdentifyLevelMetrics",
    ["pwm_command", "steady_speed", "theta_s", "tau_s", "valid"],
)

DEFAULT_IDENTIFY_PWM_STEP = 200
DEFAULT_IDENTIFY_PWM_MAX = 10000
DEFAULT_IDENTIFY_REPEAT = 2
DEFAULT_IDENTIFY_HOLD_MS = 250
DEFAULT_IDENTIFY_TAIL_ZERO_MS = 200
DEFAULT_IDENTIFY_SAMPLE_PERIOD_S = 0.02
DEFAULT_IDENTIFY_DISCRETE_TS_S = 0.005
DEFAULT_IDENTIFY_MIN_MOTION_SPEED = 5.0
DEFAULT_IDENTIFY_REQUIRED_CONSECUTIVE_SAMPLES = 3
DEFAULT_IDENTIFY_MIN_VALID_LEVELS = 2
DEFAULT_IDENTIFY_MAX_VALID_LEVELS = 3
DEFAULT_IDENTIFY_READY_WAIT_SECONDS = 1.0
DEFAULT_IDENTIFY_READY_POLL_SECONDS = 0.02
DEFAULT_IDENTIFY_START_KEEPALIVE_MS = 40


def _clamp_identify_pwm(value):
    pwm_value = int(round(float(value)))
    if pwm_value < 0:
        return 0
    if pwm_value > DEFAULT_IDENTIFY_PWM_MAX:
        return DEFAULT_IDENTIFY_PWM_MAX
    return pwm_value


def _build_pwm_command(wheel_name, pwm_value):
    if wheel_name == "left":
        return ("L_TEST_PWM={0}".format(int(pwm_value)), "R_TEST_PWM=0")
    if wheel_name == "right":
        return ("L_TEST_PWM=0", "R_TEST_PWM={0}".format(int(pwm_value)))
    raise ValueError("wheel_name must be 'left' or 'right'")


def _get_sample_command_pwm(sample, wheel_name):
    if wheel_name == "left":
        return sample.left_cmd_pwm
    return sample.right_cmd_pwm


def _build_identify_capture_events(wheel_name, hold_ms):
    events = []
    keepalive_ms = DEFAULT_IDENTIFY_START_KEEPALIVE_MS
    zero_active_command, zero_idle_command = _build_pwm_command(wheel_name, 0)

    if keepalive_ms > 0:
        while keepalive_ms < hold_ms:
            events.append((float(keepalive_ms) / 1000.0, "START"))
            keepalive_ms += DEFAULT_IDENTIFY_START_KEEPALIVE_MS

    events.append((float(hold_ms) / 1000.0, zero_active_command))
    events.append((float(hold_ms) / 1000.0, zero_idle_command))
    return events


def _is_identify_ready_sample(sample, wheel_name, command_pwm):
    current_command = 0.0

    if abs(sample.mode_id - 2.0) > 0.5:
        return None
    if sample.stop_flag >= 0.5:
        return None

    current_command = _get_sample_command_pwm(sample, wheel_name)
    if float(command_pwm) <= 0.5:
        if abs(current_command) <= 0.5:
            return 0.0
        return None

    if abs(current_command - float(command_pwm)) <= 0.5:
        return current_command

    return None


def _wait_for_identify_ready(
    client,
    wheel_name,
    command_pwm,
    wait_seconds=DEFAULT_IDENTIFY_READY_WAIT_SECONDS,
    poll_seconds=DEFAULT_IDENTIFY_READY_POLL_SECONDS,
):
    deadline = 0.0
    samples = []
    sample = None
    prefetched_samples = []

    if not hasattr(client, "read_samples"):
        return prefetched_samples

    deadline = time.monotonic() + wait_seconds
    while time.monotonic() < deadline:
        samples = client.read_samples(poll_seconds)
        prefetched_samples.extend(samples)
        for sample in samples:
            if _is_identify_ready_sample(sample, wheel_name, command_pwm) is not None:
                return prefetched_samples
        client.send_command("START")

    raise RuntimeError("pwm-identify start did not become ready")


def _resolve_identify_start_pwm(wheel_section, pwm_step):
    deadzone_pwm = 0
    step_value = int(pwm_step)

    if step_value <= 0:
        raise RuntimeError("identify_pwm_step must be positive")

    deadzone_pwm = int(wheel_section.get("deadzone_break_pwm", 0))
    if deadzone_pwm < 0:
        deadzone_pwm = 0

    return ((deadzone_pwm // step_value) + 1) * step_value


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

    client.send_command("AT_RESET")
    sleep_fn(rest_seconds)
    client.send_command("AT_TEST_MODE=1")
    client.send_command(active_command)
    client.send_command(idle_command)
    client.send_command("START")
    if hasattr(client, "drain_input"):
        client.drain_input()
    prefetched_samples = _wait_for_identify_ready(client, wheel_name, pwm_value)
    samples = client.capture_trial(
        float(hold_ms + tail_zero_ms) / 1000.0,
        events=_build_identify_capture_events(wheel_name, hold_ms),
    )
    client.send_command("AT_TEST_MODE=0")
    client.send_command("AT_RESET")
    if prefetched_samples:
        return list(prefetched_samples) + list(samples)
    return samples


def _extract_active_identify_samples(samples, wheel_name, command_pwm):
    active = []
    target_value = float(command_pwm)
    current_command = 0.0

    for sample in samples:
        if abs(sample.mode_id - 2.0) > 0.5:
            continue
        if sample.stop_flag >= 0.5:
            continue

        current_command = _get_sample_command_pwm(sample, wheel_name)

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
    profile = None
    pwm_map_section = None
    wheel_section = None
    pwm_value = 0

    profile = load_tuning_profile(args.profile_path, required=True)
    pwm_map_section = profile.get("pwm_map")
    if not isinstance(pwm_map_section, dict):
        raise RuntimeError("Missing pwm_map data in tuning profile: {0}".format(resolve_profile_path(args.profile_path)))

    wheel_section = pwm_map_section.get(wheel_name)
    if not isinstance(wheel_section, dict) or wheel_section.get("deadzone_break_pwm") is None:
        raise RuntimeError("Missing {0} deadzone in tuning profile: {1}".format(wheel_name, resolve_profile_path(args.profile_path)))

    pwm_value = _resolve_identify_start_pwm(wheel_section, args.identify_pwm_step)
    print(
        "identify {0} start_pwm={1} deadzone_pwm={2}".format(
            wheel_name,
            pwm_value,
            int(wheel_section.get("deadzone_break_pwm", 0)),
        )
    )

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
    profile = None
    left_seed = None
    right_seed = None

    if args.save_best:
        raise RuntimeError("pwm-identify does not support --save-best")

    profile = load_tuning_profile(args.profile_path, required=True)
    if not isinstance(profile.get("pwm_map"), dict):
        raise RuntimeError("Missing pwm_map data in tuning profile: {0}".format(resolve_profile_path(args.profile_path)))

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
    left_seed = seed_pair.left
    right_seed = seed_pair.right

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

    profile["pwm_identify"] = {
        "seed_pi": {
            "left": pid_gains_to_dict(left_seed),
            "right": pid_gains_to_dict(right_seed),
        },
        "source_levels": {
            "left": [
                {
                    "pwm_command": float(level.pwm_command),
                    "steady_speed": float(level.steady_speed),
                    "theta_s": float(level.theta_s),
                    "tau_s": float(level.tau_s),
                    "valid": int(level.valid),
                }
                for level in left_levels
            ],
            "right": [
                {
                    "pwm_command": float(level.pwm_command),
                    "steady_speed": float(level.steady_speed),
                    "theta_s": float(level.theta_s),
                    "tau_s": float(level.tau_s),
                    "valid": int(level.valid),
                }
                for level in right_levels
            ],
        },
    }
    save_tuning_profile(profile, args.profile_path)

    if args.apply_identify_seed:
        apply_speed_gains(client, seed_pair)
        client.send_command("AT_RESET")
        client.send_command("TEST_speed=0")

    return 0

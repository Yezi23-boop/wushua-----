import collections
import csv
import pathlib
import time

from .common import _median_value, build_shared_target_profile, load_tuning_profile, resolve_profile_path, save_tuning_profile


PwmMapLevelMetrics = collections.namedtuple(
    "PwmMapLevelMetrics",
    [
        "pwm_command",
        "steady_encoder",
        "peak_encoder",
        "sample_count",
        "repeat_count",
        "stop_flag_seen",
        "deadzone_reached",
    ],
)
PwmMapTrialResult = collections.namedtuple("PwmMapTrialResult", ["samples", "effective_pwm"])

DEFAULT_MAP_PWM_STEP = 500
DEFAULT_MAP_PWM_MAX = 10000
DEFAULT_MAP_REPEAT = 2
DEFAULT_MAP_HOLD_MS = 250
DEFAULT_MAP_TAIL_ZERO_MS = 200
DEFAULT_MAP_MIN_MOTION_SPEED = 5.0
DEFAULT_MAP_REQUIRED_CONSECUTIVE_SAMPLES = 3
DEFAULT_MAP_READY_WAIT_SECONDS = 1.0
DEFAULT_MAP_READY_POLL_SECONDS = 0.02
DEFAULT_MAP_START_KEEPALIVE_MS = 40
PWM_MAP_HEADERS = [
    "timestamp",
    "wheel",
    "pwm_command",
    "steady_encoder",
    "peak_encoder",
    "sample_count",
    "repeat_count",
    "stop_flag_seen",
    "deadzone_break_pwm",
]


def _clamp_map_pwm(value):
    pwm_value = int(round(float(value)))
    if pwm_value < 0:
        return 0
    return pwm_value


def build_pwm_map_levels(step, max_pwm):
    step = int(step)
    max_pwm = int(max_pwm)
    levels = [0]
    pwm_value = step

    while pwm_value < max_pwm:
        levels.append(pwm_value)
        pwm_value += step

    if levels[-1] != max_pwm:
        levels.append(max_pwm)

    return levels


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


def _build_pwm_map_capture_events(wheel_name, hold_ms, command_pwm):
    events = []
    keepalive_ms = DEFAULT_MAP_START_KEEPALIVE_MS
    zero_active_command, zero_idle_command = _build_pwm_command(wheel_name, 0)

    if keepalive_ms > 0:
        while keepalive_ms < hold_ms:
            events.append((float(keepalive_ms) / 1000.0, "START"))
            keepalive_ms += DEFAULT_MAP_START_KEEPALIVE_MS

    events.append((float(hold_ms) / 1000.0, zero_active_command))
    events.append((float(hold_ms) / 1000.0, zero_idle_command))
    return events


def _is_pwm_map_ready_sample(sample, wheel_name, command_pwm):
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

    if current_command >= 0.5:
        return current_command

    return None


def _wait_for_pwm_map_ready(
    client,
    wheel_name,
    command_pwm,
    wait_seconds=DEFAULT_MAP_READY_WAIT_SECONDS,
    poll_seconds=DEFAULT_MAP_READY_POLL_SECONDS,
):
    deadline = 0.0
    samples = []
    sample = None

    if not hasattr(client, "read_samples"):
        return None

    deadline = time.monotonic() + wait_seconds
    while time.monotonic() < deadline:
        samples = client.read_samples(poll_seconds)
        for sample in samples:
            sample_command = _is_pwm_map_ready_sample(sample, wheel_name, command_pwm)
            if sample_command is not None:
                return sample_command
        client.send_command("START")

    raise RuntimeError("pwm-map start did not become ready")


def run_pwm_map_trial(
    client,
    wheel_name,
    pwm_value,
    hold_ms,
    tail_zero_ms,
    rest_seconds,
    sleep_fn=time.sleep,
):
    pwm_value = _clamp_map_pwm(pwm_value)
    active_command, idle_command = _build_pwm_command(wheel_name, pwm_value)

    client.send_command("AT_RESET")
    sleep_fn(rest_seconds)
    client.send_command("AT_TEST_MODE=1")
    client.send_command(active_command)
    client.send_command(idle_command)
    client.send_command("START")
    if hasattr(client, "drain_input"):
        client.drain_input()
    active_pwm = _wait_for_pwm_map_ready(client, wheel_name, pwm_value)
    if active_pwm is not None:
        pwm_value = active_pwm
        if hasattr(client, "drain_input"):
            client.drain_input()
    samples = client.capture_trial(
        float(hold_ms + tail_zero_ms) / 1000.0,
        events=_build_pwm_map_capture_events(wheel_name, hold_ms, pwm_value),
    )
    client.send_command("AT_TEST_MODE=0")
    client.send_command("AT_RESET")
    return PwmMapTrialResult(samples, pwm_value)


def _unpack_pwm_map_trial_result(trial_result, requested_pwm):
    if hasattr(trial_result, "samples") and hasattr(trial_result, "effective_pwm"):
        return trial_result.samples, _clamp_map_pwm(trial_result.effective_pwm)
    return trial_result, _clamp_map_pwm(requested_pwm)


def _extract_active_map_samples(samples, wheel_name, command_pwm):
    active = []
    target_value = float(command_pwm)
    open_loop_mode = 2.0
    sample = None

    for sample in samples:
        if abs(sample.mode_id - open_loop_mode) > 0.5:
            continue

        current_command = _get_sample_command_pwm(sample, wheel_name)

        if abs(current_command - target_value) <= 0.5:
            active.append(sample)

    if target_value <= 0.5:
        while active and active[0].stop_flag >= 0.5:
            active.pop(0)

    return active


def _detect_deadzone_reached(responses, min_motion_speed, required_consecutive_samples):
    consecutive = 0

    for response in responses:
        if response > min_motion_speed:
            consecutive += 1
            if consecutive >= required_consecutive_samples:
                return True
        else:
            consecutive = 0

    return False


def extract_pwm_map_level_metrics(
    samples,
    wheel_name,
    command_pwm,
    min_motion_speed=DEFAULT_MAP_MIN_MOTION_SPEED,
    required_consecutive_samples=DEFAULT_MAP_REQUIRED_CONSECUTIVE_SAMPLES,
):
    responses = []
    active_samples = []
    tail_count = 0
    steady_values = []

    if not samples:
        raise RuntimeError("pwm-map capture is empty")

    active_samples = _extract_active_map_samples(samples, wheel_name, command_pwm)
    if not active_samples:
        raise RuntimeError("pwm-map active PWM window is empty")

    for sample in active_samples:
        if sample.stop_flag >= 0.5:
            raise RuntimeError("pwm-map capture saw stop_flag")

    for sample in active_samples:
        if wheel_name == "left":
            responses.append(abs(sample.left_speed))
        else:
            responses.append(abs(sample.right_speed))

    tail_count = int(len(responses) * 0.2 + 0.9999)
    if tail_count < 1:
        tail_count = 1
    steady_values = responses[-tail_count:]

    return PwmMapLevelMetrics(
        float(command_pwm),
        sum(steady_values) / float(len(steady_values)),
        max(responses),
        len(active_samples),
        1,
        0,
        _detect_deadzone_reached(responses, min_motion_speed, required_consecutive_samples),
    )


def _aggregate_pwm_map_level_metrics(level_runs, command_pwm, repeat_count):
    return PwmMapLevelMetrics(
        float(command_pwm),
        _median_value([level.steady_encoder for level in level_runs]),
        _median_value([level.peak_encoder for level in level_runs]),
        int(round(_median_value([level.sample_count for level in level_runs]))),
        int(repeat_count),
        0,
        bool(sum([1 for level in level_runs if level.deadzone_reached]) > 0),
    )


def _default_pwm_map_output_path():
    timestamp_text = time.strftime("%Y%m%d_%H%M%S")
    return pathlib.Path(__file__).resolve().parents[1] / "logs" / "pwm_encoder_map_{0}.csv".format(timestamp_text)


def _write_pwm_map_header(writer):
    writer.writeheader()


def _write_pwm_map_row(writer, timestamp_text, wheel_name, metrics, deadzone_break_pwm):
    writer.writerow(
        {
            "timestamp": timestamp_text,
            "wheel": wheel_name,
            "pwm_command": int(metrics.pwm_command),
            "steady_encoder": "{0:.3f}".format(metrics.steady_encoder),
            "peak_encoder": "{0:.3f}".format(metrics.peak_encoder),
            "sample_count": int(metrics.sample_count),
            "repeat_count": int(metrics.repeat_count),
            "stop_flag_seen": int(metrics.stop_flag_seen),
            "deadzone_break_pwm": deadzone_break_pwm,
        }
    )


def _print_pwm_map_summary(wheel_name, metrics, deadzone_break_pwm):
    print(
        "{0} pwm={1} steady={2:.3f} peak={3:.3f} samples={4} repeats={5} stop={6} deadzone={7}".format(
            wheel_name,
            int(metrics.pwm_command),
            metrics.steady_encoder,
            metrics.peak_encoder,
            int(metrics.sample_count),
            int(metrics.repeat_count),
            int(metrics.stop_flag_seen),
            deadzone_break_pwm if deadzone_break_pwm != "" else "-",
        )
    )


def _metrics_to_profile_row(metrics):
    return {
        "pwm_command": int(metrics.pwm_command),
        "steady_encoder": float(metrics.steady_encoder),
        "peak_encoder": float(metrics.peak_encoder),
        "sample_count": int(metrics.sample_count),
        "repeat_count": int(metrics.repeat_count),
        "stop_flag_seen": int(metrics.stop_flag_seen),
    }


def _build_pwm_map_profile_section(output_path, wheel_rows, wheel_deadzones):
    left_rows = wheel_rows.get("left", [])
    right_rows = wheel_rows.get("right", [])
    left_max = 0.0
    right_max = 0.0

    if left_rows:
        left_max = max([row["steady_encoder"] for row in left_rows])
    if right_rows:
        right_max = max([row["steady_encoder"] for row in right_rows])

    return {
        "raw_csv_path": str(output_path),
        "left": {
            "deadzone_break_pwm": wheel_deadzones.get("left"),
            "max_steady_encoder": left_max,
            "map_rows": left_rows,
        },
        "right": {
            "deadzone_break_pwm": wheel_deadzones.get("right"),
            "max_steady_encoder": right_max,
            "map_rows": right_rows,
        },
    }


def run_pwm_map(client, args):
    output_path = None
    writer = None
    wheel_name = ""
    pwm_value = 0
    repeat_index = 0
    run_metrics = []
    aggregated = None
    deadzone_break_pwm = None
    row_deadzone = ""
    handle = None
    last_written_pwm = 0
    effective_pwm_runs = []
    effective_pwm = 0
    stop_after_row = 0
    wheel_rows = {"left": [], "right": []}
    wheel_deadzones = {"left": None, "right": None}
    profile = None

    if args.map_output:
        output_path = pathlib.Path(args.map_output)
    else:
        output_path = _default_pwm_map_output_path()

    output_path.parent.mkdir(parents=True, exist_ok=True)
    print("pwm-map output={0}".format(output_path))

    handle = output_path.open("w", encoding="utf-8", newline="")
    try:
        writer = csv.DictWriter(handle, fieldnames=PWM_MAP_HEADERS)
        _write_pwm_map_header(writer)

        for wheel_name in ("left", "right"):
            deadzone_break_pwm = None
            last_written_pwm = -1

            for pwm_value in build_pwm_map_levels(args.map_pwm_step, args.map_pwm_max):
                run_metrics = []
                effective_pwm_runs = []
                repeat_index = 0

                while repeat_index < int(args.map_repeat):
                    trial_result = None
                    samples = None
                    effective_pwm = 0

                    trial_result = run_pwm_map_trial(
                        client,
                        wheel_name,
                        pwm_value,
                        hold_ms=args.map_hold_ms,
                        tail_zero_ms=args.map_tail_zero_ms,
                        rest_seconds=args.rest_seconds,
                    )
                    samples, effective_pwm = _unpack_pwm_map_trial_result(trial_result, pwm_value)
                    effective_pwm_runs.append(effective_pwm)
                    run_metrics.append(
                        extract_pwm_map_level_metrics(
                            samples,
                            wheel_name,
                            effective_pwm,
                        )
                    )
                    repeat_index += 1

                effective_pwm = int(round(_median_value(effective_pwm_runs)))
                stop_after_row = int(pwm_value > effective_pwm)
                if stop_after_row and effective_pwm <= last_written_pwm:
                    print(
                        "{0} pwm clamp requested={1} effective={2}, stop sweep".format(
                            wheel_name,
                            int(pwm_value),
                            int(effective_pwm),
                        )
                    )
                    break

                aggregated = _aggregate_pwm_map_level_metrics(run_metrics, effective_pwm, args.map_repeat)
                row_deadzone = ""
                if deadzone_break_pwm is None and aggregated.deadzone_reached:
                    deadzone_break_pwm = int(aggregated.pwm_command)
                    row_deadzone = deadzone_break_pwm

                _write_pwm_map_row(
                    writer,
                    time.strftime("%Y-%m-%d %H:%M:%S"),
                    wheel_name,
                    aggregated,
                    row_deadzone,
                )
                _print_pwm_map_summary(wheel_name, aggregated, row_deadzone)
                last_written_pwm = int(aggregated.pwm_command)
                wheel_rows[wheel_name].append(_metrics_to_profile_row(aggregated))
                if row_deadzone != "":
                    wheel_deadzones[wheel_name] = int(row_deadzone)
                    print("{0} deadzone_break_pwm={1}".format(wheel_name, row_deadzone))
                if stop_after_row:
                    print(
                        "{0} pwm clamp requested={1} effective={2}, stop sweep".format(
                            wheel_name,
                            int(pwm_value),
                            int(effective_pwm),
                        )
                    )
                    break
    finally:
        handle.close()

    profile = load_tuning_profile(args.profile_path, required=False)
    profile["pwm_map"] = _build_pwm_map_profile_section(output_path, wheel_rows, wheel_deadzones)
    profile["shared_targets"] = build_shared_target_profile(
        profile["pwm_map"]["left"]["max_steady_encoder"],
        profile["pwm_map"]["right"]["max_steady_encoder"],
    )
    save_tuning_profile(profile, resolve_profile_path(args.profile_path))

    return 0

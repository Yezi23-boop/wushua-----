import argparse
import collections
import sys
import time


PidGains = collections.namedtuple("PidGains", ["kp", "ki", "kd"])
TelemetrySample = collections.namedtuple(
    "TelemetrySample",
    [
        "target",
        "left_speed",
        "right_speed",
        "left_pwm",
        "right_pwm",
        "trial_active",
        "stop_flag",
    ],
)
TrialMetrics = collections.namedtuple(
    "TrialMetrics",
    [
        "score",
        "rise_ratio",
        "settle_ratio",
        "overshoot",
        "steady_error",
        "average_skew",
        "tail_jitter",
    ],
)
GroundLoadTrial = collections.namedtuple(
    "GroundLoadTrial",
    ["name", "segments_ms", "trial_ms"],
)

GROUND_LOAD_TARGET_TOLERANCE = 0.8


def parse_telemetry_line(raw_line):
    text = raw_line.strip()
    if not text:
        return None

    parts = [item.strip() for item in text.split(",") if item.strip() != ""]
    if len(parts) < 3:
        return None

    try:
        target = float(parts[0])
        left_speed = float(parts[1])
        right_speed = float(parts[2])
        left_pwm = float(parts[3]) if len(parts) >= 4 else 0.0
        right_pwm = float(parts[4]) if len(parts) >= 5 else 0.0
        trial_active = float(parts[5]) if len(parts) >= 6 else 0.0
        stop_flag = float(parts[6]) if len(parts) >= 7 else 0.0
    except ValueError:
        return None

    return TelemetrySample(
        target,
        left_speed,
        right_speed,
        left_pwm,
        right_pwm,
        trial_active,
        stop_flag,
    )


def analyze_trial(samples):
    count = len(samples)
    if count == 0:
        return TrialMetrics(float("inf"), 1.0, 1.25, float("inf"), float("inf"), float("inf"), float("inf"))

    target = 0.0
    response_series = []
    skew_sum = 0.0

    for sample in samples:
        current_target = abs(sample.target)
        if current_target > target:
            target = current_target
        response_series.append((sample.left_speed + sample.right_speed) * 0.5)
        skew_sum += abs(sample.left_speed - sample.right_speed)

    if target < 0.001:
        return TrialMetrics(0.0, 0.0, 0.0, 0.0, 0.0, skew_sum / count, 0.0)

    initial_response = response_series[0]
    direction = 1.0 if target >= initial_response else -1.0
    rise_band = max(1.0, target * 0.1)
    settle_band = max(0.8, target * 0.08)
    skew_band = max(0.8, target * 0.06)

    if direction > 0.0:
        rise_threshold = target - rise_band
    else:
        rise_threshold = target + rise_band

    rise_index = count - 1
    for index in range(count):
        response = response_series[index]
        if (direction > 0.0 and response >= rise_threshold) or (direction < 0.0 and response <= rise_threshold):
            rise_index = index
            break

    settle_index = count - 1
    found_settle = 0
    for index in range(count):
        stable = 1
        tail_index = index
        while tail_index < count:
            sample = samples[tail_index]
            if (
                abs(sample.target - sample.left_speed) > settle_band
                or abs(sample.target - sample.right_speed) > settle_band
                or abs(sample.left_speed - sample.right_speed) > skew_band
            ):
                stable = 0
                break
            tail_index += 1
        if stable:
            settle_index = index
            found_settle = 1
            break

    peak_response = response_series[0]
    for value in response_series:
        if direction > 0.0 and value > peak_response:
            peak_response = value
        if direction < 0.0 and value < peak_response:
            peak_response = value
    if direction > 0.0:
        overshoot = max(0.0, peak_response - target)
    else:
        overshoot = max(0.0, target - peak_response)

    tail_start = count // 2
    if tail_start >= count:
        tail_start = count - 1
    tail_samples = samples[tail_start:]

    steady_error = 0.0
    for sample in tail_samples:
        steady_error += abs(sample.target - sample.left_speed)
        steady_error += abs(sample.target - sample.right_speed)
    steady_error = steady_error / (2.0 * len(tail_samples))

    average_skew = skew_sum / count

    jitter_sum = 0.0
    previous = None
    for sample in tail_samples:
        response = (sample.left_speed + sample.right_speed) * 0.5
        if previous is not None:
            jitter_sum += abs(response - previous)
        previous = response
    tail_jitter = jitter_sum / max(1, len(tail_samples) - 1)

    rise_ratio = float(rise_index + 1) / float(count)
    settle_ratio = float(settle_index + 1) / float(count) if found_settle else 1.25

    score = (
        rise_ratio * target * 2.2
        + settle_ratio * target * 3.6
        + overshoot * 2.4
        + steady_error * 3.0
        + average_skew * 0.8
        + tail_jitter * 0.45
    )

    return TrialMetrics(
        score,
        rise_ratio,
        settle_ratio,
        overshoot,
        steady_error,
        average_skew,
        tail_jitter,
    )


def score_trial(samples):
    return analyze_trial(samples).score


def _clamp_non_negative(value):
    if value < 0.0:
        return 0.0
    return value


def _make_gains(values):
    return PidGains(
        _clamp_non_negative(values[0]),
        _clamp_non_negative(values[1]),
        _clamp_non_negative(values[2]),
    )


def twiddle_optimize(initial, deltas, evaluator, iterations=12, tolerance=0.05):
    best = _make_gains([initial.kp, initial.ki, initial.kd])
    best_score = evaluator(best)
    working_deltas = [abs(deltas.kp), abs(deltas.ki), abs(deltas.kd)]
    index_list = [0, 1, 2]

    for _ in range(iterations):
        if sum(working_deltas) <= tolerance:
            break

        improved = 0
        for index in index_list:
            delta = working_deltas[index]
            if delta <= tolerance:
                continue

            values = [best.kp, best.ki, best.kd]
            values[index] += delta
            candidate = _make_gains(values)
            candidate_score = evaluator(candidate)
            if candidate_score < best_score:
                best = candidate
                best_score = candidate_score
                working_deltas[index] = delta * 1.15
                improved = 1
                continue

            values = [best.kp, best.ki, best.kd]
            values[index] -= delta
            candidate = _make_gains(values)
            candidate_score = evaluator(candidate)
            if candidate_score < best_score:
                best = candidate
                best_score = candidate_score
                working_deltas[index] = delta * 1.05
                improved = 1
                continue

            working_deltas[index] = delta * 0.55

        if not improved and sum(working_deltas) <= tolerance:
            break

    return best, best_score


def format_gain(value):
    return "{0:.4f}".format(value)


def _load_pyserial():
    try:
        import serial
        from serial.tools import list_ports
    except ImportError as exc:
        raise RuntimeError(
            "Missing dependency: pyserial. Install it with 'python -m pip install pyserial'."
        ) from exc
    return serial, list_ports


def detect_port(preferred=None):
    serial, list_ports = _load_pyserial()
    del serial
    ports = sorted(list(list_ports.comports()), key=lambda item: item.device)

    if preferred:
        preferred_upper = preferred.upper()
        for port in ports:
            if port.device.upper() == preferred_upper:
                return port.device

    if len(ports) == 1:
        return ports[0].device

    if len(ports) == 0:
        raise RuntimeError("No serial ports detected.")

    raise RuntimeError(
        "Multiple serial ports detected. Use --port to choose one: {0}".format(
            ", ".join([port.device for port in ports])
        )
    )


class VofaSerialClient(object):
    def __init__(self, port, baudrate, timeout):
        serial, _ = _load_pyserial()
        self._serial = serial.Serial(
            port=port,
            baudrate=baudrate,
            timeout=timeout,
            rtscts=False,
            dsrdtr=False,
        )
        self._serial.setRTS(False)
        self._serial.setDTR(False)
        self.command_gap = 0.06
        self.port = port

    def close(self):
        if self._serial and self._serial.is_open:
            self._serial.close()

    def drain_input(self):
        self._serial.reset_input_buffer()

    def send_command(self, command):
        payload = "{0}!".format(command).encode("ascii")
        self._serial.write(payload)
        self._serial.flush()
        time.sleep(self.command_gap)

    def _readline_sample(self):
        raw = self._serial.readline()
        if not raw:
            return None

        try:
            line = raw.decode("ascii", errors="ignore")
        except AttributeError:
            line = str(raw)

        return parse_telemetry_line(line)

    def read_samples(self, duration_seconds):
        samples = []
        deadline = time.monotonic() + duration_seconds

        while time.monotonic() < deadline:
            sample = self._readline_sample()
            if sample is not None:
                samples.append(sample)

        return samples

    def capture_trial(self, duration_seconds, events=None):
        samples = []
        schedule = sorted(list(events or []), key=lambda item: item[0])
        event_index = 0
        start = time.monotonic()
        deadline = start + duration_seconds
        original_timeout = self._serial.timeout
        shorter_timeout = 0.02

        if original_timeout is None or original_timeout > shorter_timeout:
            self._serial.timeout = shorter_timeout

        try:
            while True:
                now = time.monotonic()
                elapsed = now - start

                while event_index < len(schedule) and elapsed >= schedule[event_index][0]:
                    self.send_command(schedule[event_index][1])
                    event_index += 1

                if now >= deadline and event_index >= len(schedule):
                    break

                sample = self._readline_sample()
                if sample is not None:
                    samples.append(sample)
        finally:
            self._serial.timeout = original_timeout

        return samples


def apply_symmetric_speed_gains(client, gains):
    client.send_command("AT_KP={0}".format(format_gain(gains.kp)))
    client.send_command("AT_KI={0}".format(format_gain(gains.ki)))
    client.send_command("AT_KD={0}".format(format_gain(gains.kd)))


def run_step_trial(client, gains, target_speed, rest_seconds, measure_seconds):
    client.send_command("TEST_speed=0")
    time.sleep(rest_seconds)
    client.send_command("AT_RESET")
    apply_symmetric_speed_gains(client, gains)
    client.send_command("START")
    client.send_command("TEST_speed={0}".format(format_gain(target_speed)))
    samples = client.capture_trial(measure_seconds)
    client.send_command("TEST_speed=0")
    return samples


def summarize_samples(samples):
    if not samples:
        return "samples=0"

    metrics = analyze_trial(samples)
    last = samples[-1]
    return (
        "samples={0} score={1:.3f} rise={2:.2f} settle={3:.2f} over={4:.2f} steady={5:.2f} skew={6:.2f} target={7:.3f} left={8:.3f} right={9:.3f} stop={10:.0f}".format(
            len(samples),
            metrics.score,
            metrics.rise_ratio,
            metrics.settle_ratio,
            metrics.overshoot,
            metrics.steady_error,
            metrics.average_skew,
            last.target,
            last.left_speed,
            last.right_speed,
            last.stop_flag,
        )
    )


def build_ground_load_trials():
    return [
        GroundLoadTrial(
            "sequence_25_35_45_35_25",
            ((25.0, 200), (35.0, 200), (45.0, 200), (35.0, 200), (25.0, 200)),
            1000,
        ),
    ]


def build_ground_load_return_trial(forward_trial, speed_scale=0.6, max_speed=30.0):
    if speed_scale <= 0.0:
        return None

    segments = []
    reversed_segments = list(forward_trial.segments_ms)
    reversed_segments.reverse()

    for target_speed, hold_ms in reversed_segments:
        speed_abs = abs(target_speed)
        return_speed = speed_abs * speed_scale
        if max_speed > 0.0 and return_speed > max_speed:
            return_speed = max_speed
        if return_speed < 1.0:
            return_speed = 1.0

        return_hold_ms = int((float(hold_ms) * speed_abs) / return_speed + 0.5)
        if return_hold_ms < 1:
            return_hold_ms = 1

        if target_speed >= 0.0:
            return_speed = -return_speed

        segments.append((return_speed, return_hold_ms))

    return GroundLoadTrial(
        "{0}_return".format(forward_trial.name),
        tuple(segments),
        sum([segment[1] for segment in segments]),
    )


def _build_trial_events(trial):
    events = []
    elapsed_ms = 0
    for target_speed, hold_ms in trial.segments_ms[:-1]:
        elapsed_ms += hold_ms
        next_speed = trial.segments_ms[len(events) + 1][0]
        events.append((elapsed_ms / 1000.0, "AT_SPEED={0}".format(format_gain(next_speed))))
    return events


def _target_matches(actual, expected, tolerance):
    return abs(actual - expected) <= tolerance


def _split_ground_load_segments(samples, trial, tolerance=GROUND_LOAD_TARGET_TOLERANCE):
    targets = [segment[0] for segment in trial.segments_ms]
    if len(targets) <= 1:
        return [samples]

    grouped = []
    current_samples = []
    current_index = 0
    started = 0

    for sample in samples:
        if current_index >= len(targets):
            break

        current_target = targets[current_index]
        next_target = None
        if current_index + 1 < len(targets):
            next_target = targets[current_index + 1]

        if _target_matches(sample.target, current_target, tolerance):
            started = 1
            current_samples.append(sample)
            continue

        if next_target is not None and _target_matches(sample.target, next_target, tolerance):
            if current_samples:
                grouped.append(current_samples)
            else:
                grouped.append([])
            current_samples = [sample]
            current_index += 1
            started = 1
            continue

        if not started:
            continue

        if abs(sample.target) < tolerance:
            continue

        current_samples.append(sample)

    if current_samples and current_index < len(targets):
        grouped.append(current_samples)

    return grouped


def _score_ground_load_trial(samples, trial):
    if not samples:
        return float("inf")

    segment_groups = _split_ground_load_segments(samples, trial)
    total_score = 0.0
    segment_count = len(trial.segments_ms)
    segment_index = 0

    for segment_samples in segment_groups:
        if segment_index >= segment_count:
            break

        if not segment_samples:
            total_score += 500.0
            segment_index += 1
            continue

        metrics = analyze_trial(segment_samples)
        total_score += _average_absolute_error(segment_samples) * 4.0
        total_score += metrics.overshoot * 1.8
        total_score += metrics.settle_ratio * 12.0
        total_score += metrics.rise_ratio * 6.0
        total_score += metrics.average_skew * 1.3
        total_score += metrics.tail_jitter * 0.9
        if len(segment_samples) < 4:
            total_score += 180.0
        segment_index += 1

    if segment_index < segment_count:
        total_score += (segment_count - segment_index) * 500.0

    last = samples[-1]
    cooldown_speed = abs(last.left_speed) + abs(last.right_speed)
    residual_pwm = abs(last.left_pwm) + abs(last.right_pwm)

    if last.stop_flag < 0.5:
        total_score += 600.0
    if last.trial_active > 0.5:
        total_score += 600.0
    total_score += cooldown_speed * 18.0
    if cooldown_speed > 3.0:
        total_score += 220.0
    if residual_pwm > 400.0:
        total_score += 80.0

    return total_score


def summarize_ground_load_trial(samples, trial):
    if not samples:
        return "{0} samples=0".format(trial.name)

    parts = [trial.name]
    segment_groups = _split_ground_load_segments(samples, trial)
    segment_index = 0

    for target_speed, hold_ms in trial.segments_ms:
        if segment_index < len(segment_groups) and segment_groups[segment_index]:
            metrics = analyze_trial(segment_groups[segment_index])
            parts.append(
                "v{0:.0f}/{1}ms score={2:.1f} settle={3:.2f} over={4:.2f}".format(
                    target_speed,
                    hold_ms,
                    metrics.score,
                    metrics.settle_ratio,
                    metrics.overshoot,
                )
            )
        else:
            parts.append("v{0:.0f}/{1}ms miss".format(target_speed, hold_ms))
        segment_index += 1

    parts.append("stop={0:.0f}".format(samples[-1].stop_flag))
    return " ".join(parts)


def _average_absolute_error(samples):
    if not samples:
        return float("inf")

    total = 0.0
    for sample in samples:
        total += abs(sample.target - sample.left_speed)
        total += abs(sample.target - sample.right_speed)
    return total / (2.0 * len(samples))


def score_ground_load_group(trial_sample_groups, trials=None):
    if not trial_sample_groups:
        return float("inf")

    if trials is None:
        trials = build_ground_load_trials()

    total_score = 0.0

    for trial, samples in zip(trials, trial_sample_groups):
        if not samples:
            return float("inf")
        total_score += _score_ground_load_trial(samples, trial)

    return total_score


def run_ground_load_group(
    client,
    gains,
    wait_for_operator=None,
    fuya_pwm=2000,
    cooldown_ms=450,
    precharge_ms=700,
    trials=None,
    return_trial=None,
    sleep_fn=time.sleep,
):
    if trials is None:
        trials = build_ground_load_trials()

    if wait_for_operator is None:
        def wait_for_operator(message):
            input(message)

    wait_for_operator(
        "复位到起点后按回车：kp={0:.2f} ki={1:.2f} kd={2:.2f}".format(
            gains.kp,
            gains.ki,
            gains.kd,
        )
    )

    client.send_command("AT_FUYA={0}".format(int(fuya_pwm)))
    client.send_command("AT_COOLDOWN_MS={0}".format(int(cooldown_ms)))
    apply_symmetric_speed_gains(client, gains)
    client.send_command("AT_ARM")
    if precharge_ms > 0:
        sleep_fn(float(precharge_ms) / 1000.0)

    group_results = []
    for trial in trials:
        first_target = trial.segments_ms[0][0]
        capture_seconds = (trial.trial_ms + cooldown_ms + 150) / 1000.0

        client.send_command("AT_TRIAL_MS={0}".format(int(trial.trial_ms)))
        client.send_command("AT_SPEED={0}".format(format_gain(first_target)))
        client.drain_input()
        client.send_command("AT_FIRE")
        samples = client.capture_trial(capture_seconds, events=_build_trial_events(trial))
        group_results.append(samples)

    if return_trial is not None:
        first_target = return_trial.segments_ms[0][0]
        capture_seconds = (return_trial.trial_ms + cooldown_ms + 150) / 1000.0

        client.send_command("AT_TRIAL_MS={0}".format(int(return_trial.trial_ms)))
        client.send_command("AT_SPEED={0}".format(format_gain(first_target)))
        client.drain_input()
        client.send_command("AT_FIRE")
        client.capture_trial(capture_seconds, events=_build_trial_events(return_trial))

    return score_ground_load_group(group_results, trials=trials), group_results


def _average_group_overshoot(group_results, trials=None):
    if not group_results:
        return 0.0

    if trials is None:
        trials = build_ground_load_trials()

    overshoot_sum = 0.0
    overshoot_count = 0
    for trial, samples in zip(trials, group_results):
        for segment_samples in _split_ground_load_segments(samples, trial):
            if segment_samples:
                overshoot_sum += analyze_trial(segment_samples).overshoot
                overshoot_count += 1

    if overshoot_count == 0:
        return 0.0

    return overshoot_sum / float(overshoot_count)


def _print_group_result(gains, score, group_results, trials=None):
    if trials is None:
        trials = build_ground_load_trials()

    summary_parts = []
    for trial, samples in zip(trials, group_results):
        summary_parts.append(summarize_ground_load_trial(samples, trial))
    print(
        "group kp={0:.4f} ki={1:.4f} kd={2:.4f} score={3:.4f}".format(
            gains.kp,
            gains.ki,
            gains.kd,
            score,
        )
    )
    for part in summary_parts:
        print("  {0}".format(part))


def _evaluate_ground_load_stage(client, candidates, args, trials, wait_for_operator, return_trial):
    best_gains = None
    best_score = float("inf")
    best_results = []
    score_rows = []

    for gains in candidates:
        score, group_results = run_ground_load_group(
            client,
            gains,
            wait_for_operator=wait_for_operator,
            fuya_pwm=args.fuya_pwm,
            cooldown_ms=args.ground_cooldown_ms,
            precharge_ms=args.ground_precharge_ms,
            trials=trials,
            return_trial=return_trial,
        )
        _print_group_result(gains, score, group_results, trials=trials)
        score_rows.append((gains, score))
        if score < best_score:
            best_score = score
            best_gains = gains
            best_results = group_results

    return best_gains, best_score, best_results, score_rows


def run_ground_load_autotune(client, args):
    all_rows = []
    trials = build_ground_load_trials()
    auto_cycle = 1 if args.ground_return_scale > 0.0 else 0
    return_trial = None

    if auto_cycle:
        return_trial = build_ground_load_return_trial(
            trials[-1],
            speed_scale=args.ground_return_scale,
            max_speed=args.ground_return_max_speed,
        )

    wait_state = {"prompt_needed": 1}

    def wait_for_operator(message):
        if wait_state["prompt_needed"]:
            input(message)
            if auto_cycle:
                wait_state["prompt_needed"] = 0
            return
        if not auto_cycle:
            input(message)

    stage1_candidates = [PidGains(kp, 20.0, 0.0) for kp in (95.0, 100.0, 105.0, 110.0)]
    best, best_score, best_results, stage_rows = _evaluate_ground_load_stage(
        client,
        stage1_candidates,
        args,
        trials,
        wait_for_operator,
        return_trial,
    )
    all_rows.extend(stage_rows)

    stage2_candidates = [PidGains(best.kp, ki, 0.0) for ki in (15.0, 20.0, 25.0)]
    best, best_score, best_results, stage_rows = _evaluate_ground_load_stage(
        client,
        stage2_candidates,
        args,
        trials,
        wait_for_operator,
        return_trial,
    )
    all_rows.extend(stage_rows)

    if _average_group_overshoot(best_results, trials=trials) > 1.0:
        stage3_candidates = [PidGains(best.kp, best.ki, kd) for kd in (0.2, 0.5)]
        kd_best, kd_score, kd_results, stage_rows = _evaluate_ground_load_stage(
            client,
            stage3_candidates,
            args,
            trials,
            wait_for_operator,
            return_trial,
        )
        all_rows.extend(stage_rows)
        if kd_score < best_score:
            best = kd_best
            best_score = kd_score
            best_results = kd_results

    client.send_command("AT_FUYA={0}".format(int(args.fuya_pwm)))
    client.send_command("AT_COOLDOWN_MS={0}".format(int(args.ground_cooldown_ms)))
    apply_symmetric_speed_gains(client, best)
    client.send_command("AT_RESET")
    client.send_command("TEST_speed=0")

    if args.save_best:
        client.send_command("SAVE")

    print("scoreboard")
    for gains, score in all_rows:
        print(
            "  kp={0:.4f} ki={1:.4f} kd={2:.4f} score={3:.4f}".format(
                gains.kp,
                gains.ki,
                gains.kd,
                score,
            )
        )

    print(
        "best ground-load kp={0:.4f} ki={1:.4f} kd={2:.4f} score={3:.4f}".format(
            best.kp,
            best.ki,
            best.kd,
            best_score,
        )
    )
    return 0


def build_argument_parser():
    parser = argparse.ArgumentParser(description="VOFA speed-loop autotuner")
    parser.add_argument("--port", default="COM15", help="Serial port name. Defaults to COM15.")
    parser.add_argument("--baudrate", type=int, default=115200, help="Serial baudrate.")
    parser.add_argument("--timeout", type=float, default=0.2, help="Read timeout in seconds.")
    parser.add_argument(
        "--mode",
        choices=["autotune", "ground-load"],
        default="autotune",
        help="Autotune mode.",
    )
    parser.add_argument("--target-speed", type=float, default=35.0, help="Step target for TEST_speed.")
    parser.add_argument("--rest-seconds", type=float, default=0.35, help="Idle time before each trial.")
    parser.add_argument("--measure-seconds", type=float, default=1.2, help="Capture time for each trial.")
    parser.add_argument("--iterations", type=int, default=10, help="Maximum twiddle iterations.")
    parser.add_argument("--initial-kp", type=float, default=120.0, help="Initial Kp.")
    parser.add_argument("--initial-ki", type=float, default=50.0, help="Initial Ki.")
    parser.add_argument("--initial-kd", type=float, default=0.0, help="Initial Kd.")
    parser.add_argument("--delta-kp", type=float, default=20.0, help="Initial Kp search step.")
    parser.add_argument("--delta-ki", type=float, default=10.0, help="Initial Ki search step.")
    parser.add_argument("--delta-kd", type=float, default=5.0, help="Initial Kd search step.")
    parser.add_argument("--fuya-pwm", type=int, default=2000, help="Fixed suction PWM for ground-load mode.")
    parser.add_argument("--ground-cooldown-ms", type=int, default=450, help="Ground-load cooldown time.")
    parser.add_argument("--ground-precharge-ms", type=int, default=700, help="Delay after AT_ARM before AT_FIRE.")
    parser.add_argument("--ground-return-scale", type=float, default=0.6, help="Scale factor for the unscored reverse return run. Use 0 to disable.")
    parser.add_argument("--ground-return-max-speed", type=float, default=30.0, help="Absolute cap for the reverse return speed.")
    parser.add_argument(
        "--capture-only",
        action="store_true",
        help="Only read and summarize telemetry without sending tuning commands.",
    )
    parser.add_argument(
        "--save-best",
        action="store_true",
        help="Send SAVE after applying the best gains.",
    )
    return parser


def run_capture(client, measure_seconds):
    client.drain_input()
    samples = client.read_samples(measure_seconds)
    print("capture {0}".format(summarize_samples(samples)))
    return 0 if samples else 1


def run_autotune(client, args):
    def evaluator(gains):
        samples = run_step_trial(
            client,
            gains,
            args.target_speed,
            args.rest_seconds,
            args.measure_seconds,
        )
        score = score_trial(samples)
        print(
            "trial kp={0:.4f} ki={1:.4f} kd={2:.4f} -> {3}".format(
                gains.kp,
                gains.ki,
                gains.kd,
                summarize_samples(samples),
            )
        )
        return score

    stage1_initial = PidGains(args.initial_kp, 0.0, 0.0)
    stage1_delta = PidGains(args.delta_kp, 0.0, 0.0)
    best, best_score = twiddle_optimize(
        stage1_initial,
        stage1_delta,
        evaluator,
        iterations=max(3, args.iterations // 2),
        tolerance=0.05,
    )

    stage2_initial = PidGains(best.kp, args.initial_ki, 0.0)
    stage2_delta = PidGains(max(2.0, args.delta_kp * 0.5), args.delta_ki, 0.0)
    best, best_score = twiddle_optimize(
        stage2_initial,
        stage2_delta,
        evaluator,
        iterations=args.iterations,
        tolerance=0.05,
    )

    stage3_initial = PidGains(best.kp, best.ki, args.initial_kd)
    stage3_delta = PidGains(
        max(1.0, args.delta_kp * 0.25),
        max(1.0, args.delta_ki * 0.5),
        args.delta_kd,
    )
    best, best_score = twiddle_optimize(
        stage3_initial,
        stage3_delta,
        evaluator,
        iterations=args.iterations,
        tolerance=0.05,
    )

    apply_symmetric_speed_gains(client, best)
    client.send_command("AT_RESET")
    client.send_command("TEST_speed=0")

    if args.save_best:
        client.send_command("SAVE")

    print(
        "best kp={0:.4f} ki={1:.4f} kd={2:.4f} score={3:.4f}".format(
            best.kp,
            best.ki,
            best.kd,
            best_score,
        )
    )
    return 0


def main(argv=None):
    parser = build_argument_parser()
    args = parser.parse_args(argv)

    try:
        port = detect_port(args.port)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    client = None
    try:
        client = VofaSerialClient(port, args.baudrate, args.timeout)
        print("connected port={0} baudrate={1}".format(port, args.baudrate))
        if args.capture_only:
            return run_capture(client, args.measure_seconds)
        if args.mode == "ground-load":
            return run_ground_load_autotune(client, args)
        return run_autotune(client, args)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    finally:
        if client is not None:
            client.close()


if __name__ == "__main__":
    sys.exit(main())

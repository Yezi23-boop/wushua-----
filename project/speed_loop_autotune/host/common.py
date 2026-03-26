import collections
import json
import os
import pathlib
import time


PidGains = collections.namedtuple("PidGains", ["kp", "ki", "kd"])
ZERO_PID_GAINS = PidGains(0.0, 0.0, 0.0)
WheelPidGains = collections.namedtuple("WheelPidGains", ["left", "right"])
ScoreConfig = collections.namedtuple(
    "ScoreConfig",
    [
        "rise_weight",
        "overshoot_weight",
        "settle_weight",
        "steady_weight",
        "overshoot_gate",
        "overshoot_gate_penalty",
    ],
)
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
        "mode_id",
        "left_cmd_pwm",
        "right_cmd_pwm",
    ],
)
TelemetrySample.__new__.__defaults__ = (0.0, 0.0, 0.0)
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
RepeatScoreSummary = collections.namedtuple(
    "RepeatScoreSummary",
    ["median_score", "persistent_overshoot"],
)
WheelCandidateEvaluation = collections.namedtuple(
    "WheelCandidateEvaluation",
    ["median_score", "scores", "overshoot_ratios", "persistent_overshoot"],
)

GROUND_LOAD_TARGET_TOLERANCE = 0.8
DEFAULT_SCORE_CONFIG = ScoreConfig(1.0, 12.0, 2.6, 10.0, 0.08, 3.0)
DEFAULT_MIN_SCORE_TARGET_SPEED = 10.0
DEFAULT_MULTI_SPEED_WORST_WEIGHT = 0.7
DEFAULT_KD_OVERSHOOT_RUNS = 3
DEFAULT_AUTOTUNE_SEARCH_TOLERANCE = 1.0
MODE_AIR_DUAL = "air-dual"
MODE_GROUND_DUAL = "ground-dual"
MODE_PWM_IDENTIFY = "pwm-identify"
MODE_PWM_MAP = "pwm-map"
MODE_AIR_DUAL_STEP = "air-dual-step"
MODE_GROUND_DUAL_STEP = "ground-dual-step"
AGENT_SEARCH_PHASES = ("explore", "shrink", "confirm")
PROFILE_VERSION = 1
DEFAULT_TUNING_PROFILE_PATH = pathlib.Path(__file__).resolve().parents[1] / "logs" / "current_tuning_profile.json"
PROFILE_TOP_REFERENCE_SPEED = 45.0
PROFILE_BAND_SPEEDS = {
    "low": 15.0,
    "mid": 25.0,
    "high": 35.0,
    "top": 45.0,
}
DEFAULT_AIR_PRIMARY_HOLD_MS = 500
DEFAULT_AIR_VERIFY_HOLD_MS = 300
DEFAULT_GROUND_FORWARD_HOLD_MS = 200


def normalize_band_scores(data):
    scores = {
        "low": 0.0,
        "mid": 0.0,
        "high": 0.0,
        "top": 0.0,
    }

    if not isinstance(data, dict):
        return scores

    for key in scores:
        try:
            scores[key] = float(data.get(key, 0.0))
        except (TypeError, ValueError):
            scores[key] = 0.0

    return scores


def _format_profile_float(value):
    text = "{0:.6f}".format(float(value)).rstrip("0").rstrip(".")
    if text == "-0":
        return "0"
    return text


def resolve_profile_path(profile_path_text=None):
    if profile_path_text:
        return pathlib.Path(profile_path_text)
    return DEFAULT_TUNING_PROFILE_PATH


def _empty_profile():
    return {
        "meta": {
            "profile_version": PROFILE_VERSION,
        }
    }


def load_tuning_profile(profile_path_text=None, required=False):
    profile_path = resolve_profile_path(profile_path_text)
    if not profile_path.exists():
        if required:
            raise RuntimeError("Missing tuning profile: {0}. Run pwm-map first.".format(profile_path))
        return _empty_profile()

    try:
        profile = json.loads(profile_path.read_text(encoding="utf-8"))
    except ValueError as exc:
        raise RuntimeError("Invalid tuning profile: {0}".format(profile_path)) from exc

    if not isinstance(profile, dict):
        raise RuntimeError("Invalid tuning profile: {0}".format(profile_path))

    if "meta" not in profile or not isinstance(profile["meta"], dict):
        profile["meta"] = {}
    if "profile_version" not in profile["meta"]:
        profile["meta"]["profile_version"] = PROFILE_VERSION
    return profile


def _order_shared_targets(shared_targets):
    ordered = {}
    preferred_keys = (
        "custom_sequences",
        "bands",
        "default_sequences",
        "policy",
        "shared_max_encoder",
    )

    if not isinstance(shared_targets, dict):
        return shared_targets

    for key in preferred_keys:
        if key in shared_targets:
            ordered[key] = shared_targets[key]

    for key, value in shared_targets.items():
        if key not in ordered:
            ordered[key] = value

    return ordered


def _order_tuning_profile(profile):
    ordered = {}
    preferred_keys = (
        "shared_targets",
        "meta",
        "pwm_map",
        "pwm_identify",
        "air_dual",
        "ground_dual",
    )

    if not isinstance(profile, dict):
        return profile

    for key in preferred_keys:
        if key in profile:
            ordered[key] = profile[key]

    for key, value in profile.items():
        if key not in ordered:
            ordered[key] = value

    return ordered


def save_tuning_profile(profile, profile_path_text=None):
    profile_path = resolve_profile_path(profile_path_text)
    now_text = time.strftime("%Y-%m-%d %H:%M:%S")
    meta = profile.get("meta")
    if not isinstance(meta, dict):
        meta = {}
        profile["meta"] = meta
    if "created_at" not in meta:
        meta["created_at"] = now_text
    meta["updated_at"] = now_text
    meta["profile_version"] = PROFILE_VERSION
    if isinstance(profile.get("shared_targets"), dict):
        profile["shared_targets"] = _order_shared_targets(profile["shared_targets"])
    profile = _order_tuning_profile(profile)

    profile_path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = profile_path.with_name(profile_path.name + ".tmp")
    temp_path.write_text(json.dumps(profile, ensure_ascii=False, indent=2), encoding="utf-8")
    os.replace(str(temp_path), str(profile_path))
    return profile_path


def pid_gains_to_dict(gains):
    return {
        "kp": float(gains.kp),
        "ki": float(gains.ki),
        "kd": float(gains.kd),
    }


def wheel_pid_gains_to_dict(gains):
    return {
        "left": pid_gains_to_dict(gains.left),
        "right": pid_gains_to_dict(gains.right),
    }


def pid_gains_from_dict(data, default_gains=None):
    if not isinstance(data, dict):
        return default_gains

    try:
        return PidGains(
            float(data.get("kp", 0.0)),
            float(data.get("ki", 0.0)),
            float(data.get("kd", 0.0)),
        )
    except (TypeError, ValueError):
        return default_gains


def wheel_pid_gains_from_dict(data, default_gains=None):
    if not isinstance(data, dict):
        return default_gains

    if "left" not in data and "right" not in data:
        gains = pid_gains_from_dict(data)
        if gains is None:
            return default_gains
        return WheelPidGains(gains, gains)

    left = pid_gains_from_dict(data.get("left"))
    right = pid_gains_from_dict(data.get("right"))
    if left is None or right is None:
        return default_gains

    return WheelPidGains(left, right)


def collapse_wheel_pid_gains(gains, default_gains=None):
    if gains is None:
        return default_gains

    return PidGains(gains.left.kp, gains.left.ki, gains.left.kd)


def segments_to_profile_rows(segments_ms):
    rows = []
    for target_speed, hold_ms in segments_ms:
        rows.append(
            {
                "target_speed": float(target_speed),
                "hold_ms": int(hold_ms),
            }
        )
    return rows


def _parse_profile_sequence_text(sequence_text):
    parts = [item.strip() for item in sequence_text.split(",") if item.strip()]
    segments = []

    for item in parts:
        speed_text, hold_text = item.split(":")
        target_speed = float(speed_text.strip())
        hold_ms = int(hold_text.strip())
        if hold_ms <= 0:
            raise ValueError("segment duration must be positive")
        segments.append((target_speed, hold_ms))

    if not segments:
        raise ValueError("sequence must contain at least one segment")

    return tuple(segments)


def profile_rows_to_segments(sequence_rows):
    segments = []

    if isinstance(sequence_rows, str):
        return _parse_profile_sequence_text(sequence_rows)

    if not isinstance(sequence_rows, list):
        return tuple(segments)

    for row in sequence_rows:
        if not isinstance(row, dict):
            continue
        try:
            target_speed = float(row.get("target_speed", 0.0))
            hold_ms = int(row.get("hold_ms", 0))
        except (TypeError, ValueError):
            continue

        if hold_ms <= 0:
            continue
        segments.append((target_speed, hold_ms))

    return tuple(segments)


def profile_rows_to_sequence_text(sequence_rows):
    parts = []

    for target_speed, hold_ms in profile_rows_to_segments(sequence_rows):
        parts.append("{0}:{1}".format(_format_profile_float(target_speed), int(hold_ms)))

    return ",".join(parts)


def _build_profile_sequence(target_values, hold_ms):
    rows = []
    for target_value in target_values:
        rows.append(
            {
                "target_speed": float(target_value),
                "hold_ms": int(hold_ms),
            }
        )
    return rows


def build_shared_target_profile(left_max_steady_encoder, right_max_steady_encoder):
    shared_max = min(float(left_max_steady_encoder), float(right_max_steady_encoder))
    if shared_max < 0.0:
        shared_max = 0.0

    low = shared_max * PROFILE_BAND_SPEEDS["low"] / PROFILE_TOP_REFERENCE_SPEED
    mid = shared_max * PROFILE_BAND_SPEEDS["mid"] / PROFILE_TOP_REFERENCE_SPEED
    high = shared_max * PROFILE_BAND_SPEEDS["high"] / PROFILE_TOP_REFERENCE_SPEED
    top = shared_max * PROFILE_BAND_SPEEDS["top"] / PROFILE_TOP_REFERENCE_SPEED

    return {
        "policy": "min_wheel_max",
        "shared_max_encoder": shared_max,
        "bands": {
            "low": low,
            "mid": mid,
            "high": high,
            "top": top,
        },
        "default_sequences": {
            "air_primary": _build_profile_sequence((low, mid, mid, low, low), DEFAULT_AIR_PRIMARY_HOLD_MS),
            "air_verify": _build_profile_sequence((low, mid, mid, low, low), DEFAULT_AIR_VERIFY_HOLD_MS),
            "ground_forward": _build_profile_sequence((low, mid, low), DEFAULT_GROUND_FORWARD_HOLD_MS),
        },
    }


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
        mode_id = float(parts[7]) if len(parts) >= 8 else 0.0
        left_cmd_pwm = float(parts[8]) if len(parts) >= 9 else 0.0
        right_cmd_pwm = float(parts[9]) if len(parts) >= 10 else 0.0
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
        mode_id,
        left_cmd_pwm,
        right_cmd_pwm,
    )


def _normalize_metric(value, target):
    if target < 0.001:
        return 0.0
    return value / target


def _score_step_metrics(metrics, target, score_config):
    overshoot_ratio = _normalize_metric(metrics.overshoot, target)
    steady_ratio = _normalize_metric(metrics.steady_error, target)

    score = (
        metrics.rise_ratio * score_config.rise_weight
        + metrics.settle_ratio * score_config.settle_weight
        + overshoot_ratio * score_config.overshoot_weight
        + steady_ratio * score_config.steady_weight
    )

    if (
        score_config.overshoot_gate > 0.0
        and overshoot_ratio > score_config.overshoot_gate
        and score_config.overshoot_gate_penalty > 0.0
    ):
        overflow_ratio = (overshoot_ratio - score_config.overshoot_gate) / score_config.overshoot_gate
        score += score_config.overshoot_gate_penalty * (1.0 + overflow_ratio)

    return score


def analyze_trial(samples, score_config=None):
    if score_config is None:
        score_config = DEFAULT_SCORE_CONFIG

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

    score = _score_step_metrics(
        TrialMetrics(
            0.0,
            rise_ratio,
            settle_ratio,
            overshoot,
            steady_error,
            average_skew,
            tail_jitter,
        ),
        target,
        score_config,
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


def score_trial(samples, score_config=None):
    return analyze_trial(samples, score_config=score_config).score


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


def _median_value(values):
    ordered = sorted(list(values))
    count = len(ordered)

    if count == 0:
        return float("inf")

    middle = count // 2
    if count % 2:
        return ordered[middle]

    return (ordered[middle - 1] + ordered[middle]) * 0.5


def summarize_repeat_scores(score_values, overshoot_ratios, overshoot_gate, required_runs):
    consecutive = 0
    persistent_overshoot = 0

    if required_runs < 1:
        required_runs = 1

    if overshoot_gate <= 0.0:
        required_runs = 0

    for ratio in overshoot_ratios:
        if required_runs > 0 and ratio > overshoot_gate:
            consecutive += 1
            if consecutive >= required_runs:
                persistent_overshoot = 1
                break
        else:
            consecutive = 0

    return RepeatScoreSummary(
        _median_value(score_values),
        bool(persistent_overshoot),
    )


def build_score_config(args):
    return ScoreConfig(
        args.score_rise_weight,
        args.score_overshoot_weight,
        args.score_settle_weight,
        args.score_steady_weight,
        args.score_overshoot_gate,
        args.score_overshoot_gate_penalty,
    )


def normalize_mode_name(mode_name):
    if mode_name in (MODE_AIR_DUAL, "autotune"):
        return MODE_AIR_DUAL
    if mode_name in (MODE_GROUND_DUAL, "ground-load"):
        return MODE_GROUND_DUAL
    if mode_name == MODE_PWM_IDENTIFY:
        return MODE_PWM_IDENTIFY
    if mode_name == MODE_PWM_MAP:
        return MODE_PWM_MAP
    raise ValueError("Unsupported mode: {0}".format(mode_name))


def twiddle_optimize(initial, deltas, evaluator, iterations=12, tolerance=DEFAULT_AUTOTUNE_SEARCH_TOLERANCE):
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


def describe_gains(gains):
    if isinstance(gains, WheelPidGains):
        return (
            "lkp={0:.2f} lki={1:.2f} lkd={2:.2f} "
            "rkp={3:.2f} rki={4:.2f} rkd={5:.2f}"
        ).format(
            gains.left.kp,
            gains.left.ki,
            gains.left.kd,
            gains.right.kp,
            gains.right.ki,
            gains.right.kd,
        )

    return "kp={0:.2f} ki={1:.2f} kd={2:.2f}".format(
        gains.kp,
        gains.ki,
        gains.kd,
    )


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


def apply_speed_gains(client, gains):
    if isinstance(gains, WheelPidGains):
        client.send_command("L_KP={0}".format(format_gain(gains.left.kp)))
        client.send_command("L_KI={0}".format(format_gain(gains.left.ki)))
        client.send_command("L_KD={0}".format(format_gain(gains.left.kd)))
        client.send_command("R_KP={0}".format(format_gain(gains.right.kp)))
        client.send_command("R_KI={0}".format(format_gain(gains.right.ki)))
        client.send_command("R_KD={0}".format(format_gain(gains.right.kd)))
        return

    client.send_command("AT_KP={0}".format(format_gain(gains.kp)))
    client.send_command("AT_KI={0}".format(format_gain(gains.ki)))
    client.send_command("AT_KD={0}".format(format_gain(gains.kd)))


def run_step_trial(client, gains, target_speed, rest_seconds, measure_seconds):
    client.send_command("TEST_speed=0")
    time.sleep(rest_seconds)
    client.send_command("AT_RESET")
    apply_speed_gains(client, gains)
    client.send_command("START")
    client.send_command("TEST_speed={0}".format(format_gain(target_speed)))
    samples = client.capture_trial(measure_seconds)
    client.send_command("TEST_speed=0")
    return samples


def summarize_samples(samples, score_config=None):
    if not samples:
        return "samples=0"

    metrics = analyze_trial(samples, score_config=score_config)
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


def combine_multi_speed_scores(segment_scores, worst_weight=DEFAULT_MULTI_SPEED_WORST_WEIGHT):
    if not segment_scores:
        return float("inf")

    if worst_weight < 0.0:
        worst_weight = 0.0
    if worst_weight > 1.0:
        worst_weight = 1.0

    worst_score = max(segment_scores)
    average_score = sum(segment_scores) / float(len(segment_scores))
    return worst_score * worst_weight + average_score * (1.0 - worst_weight)


def _build_trial_events_with_prefix(trial, command_name):
    events = []
    elapsed_ms = 0
    for target_speed, hold_ms in trial.segments_ms[:-1]:
        elapsed_ms += hold_ms
        next_speed = trial.segments_ms[len(events) + 1][0]
        events.append((elapsed_ms / 1000.0, "{0}={1}".format(command_name, format_gain(next_speed))))
    return events


def _build_trial_events(trial):
    return _build_trial_events_with_prefix(trial, "AT_SPEED")


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


def _segment_is_scored(target_speed, min_target_speed):
    return abs(target_speed) >= min_target_speed


def score_multi_speed_trial(
    samples,
    trial,
    score_config=None,
    min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED,
):
    if score_config is None:
        score_config = DEFAULT_SCORE_CONFIG

    if not samples:
        return float("inf")

    segment_groups = _split_ground_load_segments(samples, trial)
    segment_scores = []
    segment_index = 0

    while segment_index < len(trial.segments_ms):
        target_speed = trial.segments_ms[segment_index][0]
        if _segment_is_scored(target_speed, min_target_speed):
            if segment_index >= len(segment_groups) or not segment_groups[segment_index]:
                segment_scores.append(500.0)
            else:
                metrics = analyze_trial(segment_groups[segment_index], score_config=score_config)
                segment_score = metrics.score
                if len(segment_groups[segment_index]) < 3:
                    segment_score += 180.0
                segment_scores.append(segment_score)
        segment_index += 1

    if not segment_scores:
        return float("inf")

    return combine_multi_speed_scores(segment_scores)


def _project_samples_to_wheel(samples, wheel_name):
    projected = []

    for sample in samples:
        if wheel_name == "left":
            wheel_speed = sample.left_speed
            wheel_pwm = sample.left_pwm
        else:
            wheel_speed = sample.right_speed
            wheel_pwm = sample.right_pwm

        projected.append(
            TelemetrySample(
                sample.target,
                wheel_speed,
                wheel_speed,
                wheel_pwm,
                wheel_pwm,
                sample.trial_active,
                sample.stop_flag,
                sample.mode_id,
                sample.left_cmd_pwm,
                sample.right_cmd_pwm,
            )
        )

    return projected


def score_wheel_multi_speed_trial(
    samples,
    trial,
    wheel_name,
    score_config=None,
    min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED,
):
    return score_multi_speed_trial(
        _project_samples_to_wheel(samples, wheel_name),
        trial,
        score_config=score_config,
        min_target_speed=min_target_speed,
    )


def _max_segment_overshoot_ratio(samples, trial, score_config=None, min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED):
    if score_config is None:
        score_config = DEFAULT_SCORE_CONFIG

    segment_groups = _split_ground_load_segments(samples, trial)
    overshoot_ratios = []
    segment_index = 0

    while segment_index < len(trial.segments_ms):
        target_speed = trial.segments_ms[segment_index][0]
        if _segment_is_scored(target_speed, min_target_speed):
            if segment_index >= len(segment_groups) or not segment_groups[segment_index]:
                overshoot_ratios.append(float("inf"))
            else:
                metrics = analyze_trial(segment_groups[segment_index], score_config=score_config)
                overshoot_ratios.append(_normalize_metric(metrics.overshoot, abs(target_speed)))
        segment_index += 1

    if not overshoot_ratios:
        return 0.0

    return max(overshoot_ratios)


def _max_pair_overshoot_ratio(samples, trial, score_config=None, min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED):
    left_ratio = _max_segment_overshoot_ratio(
        _project_samples_to_wheel(samples, "left"),
        trial,
        score_config=score_config,
        min_target_speed=min_target_speed,
    )
    right_ratio = _max_segment_overshoot_ratio(
        _project_samples_to_wheel(samples, "right"),
        trial,
        score_config=score_config,
        min_target_speed=min_target_speed,
    )
    return max(left_ratio, right_ratio)


def _average_absolute_error(samples):
    if not samples:
        return float("inf")

    total = 0.0
    for sample in samples:
        total += abs(sample.target - sample.left_speed)
        total += abs(sample.target - sample.right_speed)
    return total / (2.0 * len(samples))


def run_capture(client, measure_seconds):
    client.drain_input()
    samples = client.read_samples(measure_seconds)
    print("capture {0}".format(summarize_samples(samples)))
    return 0 if samples else 1

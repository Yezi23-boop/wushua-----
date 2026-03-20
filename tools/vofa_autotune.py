import argparse
import collections
import math
import sys
import time


PidGains = collections.namedtuple("PidGains", ["kp", "ki", "kd"])
TelemetrySample = collections.namedtuple(
    "TelemetrySample",
    ["target", "left_speed", "right_speed", "aux_value"],
)


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
        aux_value = float(parts[3]) if len(parts) >= 4 else 0.0
    except ValueError:
        return None

    return TelemetrySample(target, left_speed, right_speed, aux_value)


def score_trial(samples):
    count = len(samples)
    if count == 0:
        return float("inf")

    error_sum = 0.0
    overshoot_sum = 0.0
    skew_sum = 0.0
    jitter_sum = 0.0
    previous = None

    for sample in samples:
        left_error = sample.target - sample.left_speed
        right_error = sample.target - sample.right_speed

        error_sum += abs(left_error) + abs(right_error)
        overshoot_sum += max(0.0, sample.left_speed - sample.target)
        overshoot_sum += max(0.0, sample.right_speed - sample.target)
        skew_sum += abs(sample.left_speed - sample.right_speed)

        if previous is not None:
            jitter_sum += abs(sample.left_speed - previous.left_speed)
            jitter_sum += abs(sample.right_speed - previous.right_speed)
        previous = sample

    last = samples[-1]
    settling_error = abs(last.target - last.left_speed) + abs(last.target - last.right_speed)

    average_error = error_sum / (2.0 * count)
    average_overshoot = overshoot_sum / (2.0 * count)
    average_skew = skew_sum / count
    average_jitter = jitter_sum / (2.0 * max(1, count - 1))

    return (
        average_error
        + average_overshoot * 1.8
        + average_skew * 0.35
        + average_jitter * 0.12
        + settling_error * 0.5
    )


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
        self._serial = serial.Serial(port=port, baudrate=baudrate, timeout=timeout)
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

    def read_samples(self, duration_seconds):
        samples = []
        deadline = time.monotonic() + duration_seconds

        while time.monotonic() < deadline:
            raw = self._serial.readline()
            if not raw:
                continue

            try:
                line = raw.decode("ascii", errors="ignore")
            except AttributeError:
                line = str(raw)

            sample = parse_telemetry_line(line)
            if sample is not None:
                samples.append(sample)

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
    client.drain_input()
    client.send_command("TEST_speed={0}".format(format_gain(target_speed)))
    samples = client.read_samples(measure_seconds)
    client.send_command("TEST_speed=0")
    return samples


def summarize_samples(samples):
    if not samples:
        return "samples=0"

    score = score_trial(samples)
    last = samples[-1]
    return (
        "samples={0} score={1:.3f} target={2:.3f} left={3:.3f} right={4:.3f}".format(
            len(samples),
            score,
            last.target,
            last.left_speed,
            last.right_speed,
        )
    )


def build_argument_parser():
    parser = argparse.ArgumentParser(description="VOFA speed-loop autotuner")
    parser.add_argument("--port", default="COM15", help="Serial port name. Defaults to COM15.")
    parser.add_argument("--baudrate", type=int, default=115200, help="Serial baudrate.")
    parser.add_argument("--timeout", type=float, default=0.2, help="Read timeout in seconds.")
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
    initial = PidGains(args.initial_kp, args.initial_ki, args.initial_kd)
    deltas = PidGains(args.delta_kp, args.delta_ki, args.delta_kd)

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

    best, best_score = twiddle_optimize(
        initial,
        deltas,
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
        return run_autotune(client, args)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    finally:
        if client is not None:
            client.close()


if __name__ == "__main__":
    sys.exit(main())

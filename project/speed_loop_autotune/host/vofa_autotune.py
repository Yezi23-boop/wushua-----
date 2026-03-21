import argparse
import pathlib
import sys


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[3]
PROJECT_ROOT_TEXT = str(PROJECT_ROOT)
if PROJECT_ROOT_TEXT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT_TEXT)

from project.speed_loop_autotune.host import air_dual as _air_dual
from project.speed_loop_autotune.host import common as _common
from project.speed_loop_autotune.host import ground_dual as _ground_dual


def _reexport_module(module):
    for name in dir(module):
        if not name.startswith("__"):
            globals()[name] = getattr(module, name)


_reexport_module(_common)
_reexport_module(_air_dual)
_reexport_module(_ground_dual)


def build_argument_parser():
    parser = argparse.ArgumentParser(description="VOFA speed-loop autotuner")
    parser.add_argument("--port", default="COM8", help="Serial port name. Defaults to COM8.")
    parser.add_argument("--baudrate", type=int, default=115200, help="Serial baudrate.")
    parser.add_argument("--timeout", type=float, default=0.2, help="Read timeout in seconds.")
    parser.add_argument(
        "--mode",
        choices=[MODE_AIR_DUAL, MODE_GROUND_DUAL, "autotune", "ground-load"],
        default=MODE_AIR_DUAL,
        help="Tuning mode. Prefer 'air-dual' or 'ground-dual'; old labels stay available for compatibility.",
    )
    parser.add_argument(
        "--autotune-sequence",
        default=DEFAULT_AUTOTUNE_SEQUENCE,
        help="Autotune multi-speed sequence as speed:duration_ms pairs separated by commas.",
    )
    parser.add_argument(
        "--autotune-verify-sequence",
        default=DEFAULT_AUTOTUNE_VERIFY_SEQUENCE,
        help="Verification multi-speed sequence used after the main autotune search.",
    )
    parser.add_argument("--target-speed", type=float, default=35.0, help="Step target for TEST_speed.")
    parser.add_argument("--rest-seconds", type=float, default=0.35, help="Idle time before each trial.")
    parser.add_argument("--measure-seconds", type=float, default=1.2, help="Capture time for each trial.")
    parser.add_argument("--iterations", type=int, default=10, help="Maximum twiddle iterations.")
    parser.add_argument("--initial-kp", type=float, default=100.0, help="Initial Kp.")
    parser.add_argument("--initial-ki", type=float, default=20.0, help="Initial Ki.")
    parser.add_argument("--initial-kd", type=float, default=0.0, help="Initial Kd.")
    parser.add_argument("--delta-kp", type=float, default=10.0, help="Initial Kp search step.")
    parser.add_argument("--delta-ki", type=float, default=5.0, help="Initial Ki search step.")
    parser.add_argument("--delta-kd", type=float, default=0.5, help="Initial Kd search step.")
    parser.add_argument(
        "--autotune-tail-zero-ms",
        type=int,
        default=DEFAULT_AUTOTUNE_TAIL_ZERO_MS,
        help="Extra capture time after the final TEST_speed=0 command.",
    )
    parser.add_argument(
        "--repeat-each",
        type=int,
        default=DEFAULT_AUTOTUNE_REPEAT_COUNT,
        help="How many times to repeat each candidate and score by the median.",
    )
    parser.add_argument(
        "--search-tolerance",
        type=float,
        default=DEFAULT_AUTOTUNE_SEARCH_TOLERANCE,
        help="Stop shrinking the search once the working step reaches this value.",
    )
    parser.add_argument(
        "--kd-overshoot-runs",
        type=int,
        default=DEFAULT_KD_OVERSHOOT_RUNS,
        help="Required consecutive overshooting runs before trying Kd.",
    )
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
    parser.add_argument(
        "--score-rise-weight",
        type=float,
        default=DEFAULT_SCORE_CONFIG.rise_weight,
        help="Weight for rise-time score contribution.",
    )
    parser.add_argument(
        "--score-overshoot-weight",
        type=float,
        default=DEFAULT_SCORE_CONFIG.overshoot_weight,
        help="Weight for overshoot score contribution.",
    )
    parser.add_argument(
        "--score-settle-weight",
        type=float,
        default=DEFAULT_SCORE_CONFIG.settle_weight,
        help="Weight for settling-time score contribution.",
    )
    parser.add_argument(
        "--score-steady-weight",
        type=float,
        default=DEFAULT_SCORE_CONFIG.steady_weight,
        help="Weight for steady-state error score contribution.",
    )
    parser.add_argument(
        "--score-overshoot-gate",
        type=float,
        default=DEFAULT_SCORE_CONFIG.overshoot_gate,
        help="Overshoot ratio threshold. Set 0 to disable the hard overshoot penalty.",
    )
    parser.add_argument(
        "--score-overshoot-gate-penalty",
        type=float,
        default=DEFAULT_SCORE_CONFIG.overshoot_gate_penalty,
        help="Extra penalty applied once overshoot ratio exceeds the configured gate.",
    )
    parser.add_argument(
        "--score-min-target-speed",
        type=float,
        default=DEFAULT_MIN_SCORE_TARGET_SPEED,
        help="Ignore ground-load segments whose absolute target speed is below this threshold.",
    )
    return parser


def main(argv=None):
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    args.mode = normalize_mode_name(args.mode)

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
        if args.mode == MODE_GROUND_DUAL:
            return run_ground_dual_autotune(client, args)
        return run_air_dual_autotune(client, args)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    finally:
        if client is not None:
            client.close()


if __name__ == "__main__":
    sys.exit(main())

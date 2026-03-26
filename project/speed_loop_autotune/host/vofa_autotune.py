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
from project.speed_loop_autotune.host import pwm_map as _pwm_map
from project.speed_loop_autotune.host import pwm_identify as _pwm_identify


def _reexport_module(module):
    for name in dir(module):
        if not name.startswith("__"):
            globals()[name] = getattr(module, name)


_reexport_module(_common)
_reexport_module(_air_dual)
_reexport_module(_ground_dual)
_reexport_module(_pwm_map)
_reexport_module(_pwm_identify)


def build_argument_parser():
    parser = argparse.ArgumentParser(description="VOFA speed-loop autotuner")
    parser.add_argument("--port", default="COM8", help="Serial port name. Defaults to COM8.")
    parser.add_argument("--baudrate", type=int, default=115200, help="Serial baudrate.")
    parser.add_argument("--timeout", type=float, default=0.2, help="Read timeout in seconds.")
    parser.add_argument(
        "--mode",
        choices=[
            MODE_AIR_DUAL,
            MODE_GROUND_DUAL,
            MODE_AIR_DUAL_STEP,
            MODE_GROUND_DUAL_STEP,
            MODE_PWM_IDENTIFY,
            MODE_PWM_MAP,
            "autotune",
            "ground-load",
        ],
        default=MODE_AIR_DUAL,
        help="Tuning mode. Prefer 'air-dual', 'ground-dual', 'pwm-identify', or 'pwm-map'; old labels stay available for compatibility.",
    )
    parser.add_argument(
        "--autotune-sequence",
        default=DEFAULT_AUTOTUNE_SEQUENCE,
        help="Autotune multi-speed sequence as speed:duration_ms pairs separated by commas.",
    )
    parser.add_argument(
        "--autotune-verify-sequence",
        default=DEFAULT_AUTOTUNE_VERIFY_SEQUENCE,
        help="Compatibility-only secondary sequence. air-dual single-sequence tuning ignores this unless legacy code paths use it.",
    )
    parser.add_argument("--target-speed", type=float, default=35.0, help="Step target for TEST_speed.")
    parser.add_argument("--rest-seconds", type=float, default=0.35, help="Idle time before each trial.")
    parser.add_argument("--measure-seconds", type=float, default=1.2, help="Capture time for each trial.")
    parser.add_argument("--iterations", type=int, default=10, help="Maximum twiddle iterations.")
    parser.add_argument("--initial-kp", type=float, default=100.0, help="Initial Kp.")
    parser.add_argument("--initial-ki", type=float, default=20.0, help="Initial Ki.")
    parser.add_argument("--initial-kd", type=float, default=0.0, help="Initial Kd.")
    parser.add_argument("--delta-kp", type=float, default=10.0, help="Legacy Kp search step. air-dual batch tuning now uses built-in coarse/fine/micro presets.")
    parser.add_argument("--delta-ki", type=float, default=5.0, help="Legacy Ki search step. air-dual batch tuning now uses built-in coarse/fine/micro presets.")
    parser.add_argument("--delta-kd", type=float, default=0.5, help="Legacy Kd search step kept for compatibility.")
    parser.add_argument(
        "--identify-pwm-step",
        type=int,
        default=DEFAULT_IDENTIFY_PWM_STEP,
        help="PWM sweep step used by pwm-identify.",
    )
    parser.add_argument(
        "--identify-pwm-max",
        type=int,
        default=DEFAULT_IDENTIFY_PWM_MAX,
        help="Maximum PWM sweep value used by pwm-identify.",
    )
    parser.add_argument(
        "--identify-repeat",
        type=int,
        default=DEFAULT_IDENTIFY_REPEAT,
        help="How many times to repeat each open-loop PWM level.",
    )
    parser.add_argument(
        "--identify-hold-ms",
        type=int,
        default=DEFAULT_IDENTIFY_HOLD_MS,
        help="How long to hold each PWM step before tail-zero.",
    )
    parser.add_argument(
        "--identify-tail-zero-ms",
        type=int,
        default=DEFAULT_IDENTIFY_TAIL_ZERO_MS,
        help="Extra capture time after PWM identify returns to zero.",
    )
    parser.add_argument(
        "--apply-identify-seed",
        action="store_true",
        help="Apply pwm-identify seed gains to RAM after identify completes.",
    )
    parser.add_argument(
        "--map-pwm-step",
        type=int,
        default=DEFAULT_MAP_PWM_STEP,
        help="PWM sweep step used by pwm-map.",
    )
    parser.add_argument(
        "--map-pwm-max",
        type=int,
        default=DEFAULT_MAP_PWM_MAX,
        help="Maximum PWM sweep value used by pwm-map.",
    )
    parser.add_argument(
        "--map-repeat",
        type=int,
        default=DEFAULT_MAP_REPEAT,
        help="How many times to repeat each pwm-map level.",
    )
    parser.add_argument(
        "--map-hold-ms",
        type=int,
        default=DEFAULT_MAP_HOLD_MS,
        help="How long to hold each pwm-map PWM step before tail-zero.",
    )
    parser.add_argument(
        "--map-tail-zero-ms",
        type=int,
        default=DEFAULT_MAP_TAIL_ZERO_MS,
        help="Extra capture time after pwm-map returns to zero.",
    )
    parser.add_argument(
        "--map-output",
        default="",
        help="Optional CSV path for pwm-map output. Defaults to the speed_loop_autotune logs directory.",
    )
    parser.add_argument(
        "--profile-path",
        default=str(DEFAULT_TUNING_PROFILE_PATH),
        help="Shared tuning profile path used across pwm-map, pwm-identify, air-dual, and ground-dual.",
    )
    parser.add_argument(
        "--candidate-json",
        default="",
        help="Optional JSON file containing the candidate PID pair for a single step worker run.",
    )
    parser.add_argument(
        "--baseline-json",
        default="",
        help="Optional JSON file containing the baseline PID pair for a single step worker run.",
    )
    parser.add_argument(
        "--result-json",
        default="",
        help="Optional JSON output path for single step worker results.",
    )
    parser.add_argument(
        "--waveform-path",
        default="",
        help="Optional JSONL output path for the step worker waveform snapshot.",
    )
    parser.add_argument(
        "--batch-id",
        default="",
        help="Optional batch identifier attached to single step worker outputs.",
    )
    parser.add_argument(
        "--round-index",
        type=int,
        default=1,
        help="1-based round index attached to single step worker outputs.",
    )
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
        help="Legacy repeat count option. air-dual batch tuning now uses built-in coarse/fine/micro repeat counts.",
    )
    parser.add_argument(
        "--candidate-limit",
        type=int,
        default=10,
        help="Maximum candidate PID groups to evaluate per air-dual batch. Defaults to 10.",
    )
    parser.set_defaults(interactive_batches=True)
    parser.add_argument(
        "--interactive-batches",
        dest="interactive_batches",
        action="store_true",
        help="Prompt to continue or stop after each air-dual batch. Enabled by default.",
    )
    parser.add_argument(
        "--no-interactive-batches",
        dest="interactive_batches",
        action="store_false",
        help="Run a single air-dual batch and exit without finalizing best_pid.",
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


def validate_args(args):
    if args.mode == MODE_PWM_IDENTIFY and args.save_best:
        raise RuntimeError("pwm-identify does not support --save-best")
    if args.mode == MODE_PWM_IDENTIFY and args.identify_pwm_step <= 0:
        raise RuntimeError("pwm-identify requires --identify-pwm-step > 0")
    if args.mode == MODE_PWM_IDENTIFY and args.identify_pwm_max < args.identify_pwm_step:
        raise RuntimeError("pwm-identify requires --identify-pwm-max >= --identify-pwm-step")
    if args.mode == MODE_PWM_IDENTIFY and args.identify_repeat < 1:
        raise RuntimeError("pwm-identify requires --identify-repeat >= 1")
    if args.mode == MODE_PWM_IDENTIFY and args.identify_hold_ms < 20:
        raise RuntimeError("pwm-identify requires --identify-hold-ms >= 20")
    if args.mode == MODE_PWM_MAP and args.map_pwm_step <= 0:
        raise RuntimeError("pwm-map requires --map-pwm-step > 0")
    if args.mode == MODE_PWM_MAP and args.map_pwm_max < args.map_pwm_step:
        raise RuntimeError("pwm-map requires --map-pwm-max >= --map-pwm-step")
    if args.mode == MODE_PWM_MAP and args.map_repeat < 1:
        raise RuntimeError("pwm-map requires --map-repeat >= 1")
    if args.mode == MODE_PWM_MAP and args.map_hold_ms < 20:
        raise RuntimeError("pwm-map requires --map-hold-ms >= 20")
    if args.mode == MODE_AIR_DUAL and args.candidate_limit < 0:
        raise RuntimeError("air-dual requires --candidate-limit >= 0")
    if args.mode in (MODE_AIR_DUAL_STEP, MODE_GROUND_DUAL_STEP) and args.round_index < 1:
        raise RuntimeError("step workers require --round-index >= 1")


def main(argv=None):
    parser = build_argument_parser()
    argv_items = argv
    if argv_items is None:
        argv_items = sys.argv[1:]
    else:
        argv_items = list(argv_items)

    args = parser.parse_args(argv_items)
    args.autotune_sequence_explicit = "--autotune-sequence" in argv_items
    args.autotune_verify_sequence_explicit = "--autotune-verify-sequence" in argv_items
    args.mode = normalize_mode_name(args.mode)

    try:
        validate_args(args)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2

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
        if args.mode == MODE_PWM_IDENTIFY:
            return run_pwm_identify(client, args)
        if args.mode == MODE_PWM_MAP:
            return run_pwm_map(client, args)
        if args.mode == MODE_AIR_DUAL_STEP:
            return run_air_dual_step(client, args)
        if args.mode == MODE_GROUND_DUAL_STEP:
            return run_ground_dual_step(client, args)
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

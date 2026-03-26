import csv
import json
import pathlib
import sys
import time

from . import agent_session, common
from .common import (
    DEFAULT_AUTOTUNE_SEARCH_TOLERANCE,
    DEFAULT_KD_OVERSHOOT_RUNS,
    DEFAULT_MIN_SCORE_TARGET_SPEED,
    DEFAULT_SCORE_CONFIG,
    DEFAULT_TUNING_PROFILE_PATH,
    MODE_AIR_DUAL_STEP,
    GroundLoadTrial,
    PidGains,
    RepeatScoreSummary,
    TelemetrySample,
    TrialMetrics,
    WheelCandidateEvaluation,
    WheelPidGains,
    ZERO_PID_GAINS,
    _median_value,
    _clamp_non_negative,
    _max_pair_overshoot_ratio,
    _max_segment_overshoot_ratio,
    _project_samples_to_wheel,
    _split_ground_load_segments,
    apply_speed_gains,
    analyze_trial,
    build_score_config,
    combine_multi_speed_scores,
    format_gain,
    load_wheel_pid_gains_json,
    load_tuning_profile,
    normalize_band_scores,
    profile_rows_to_sequence_text,
    resolve_profile_path,
    score_multi_speed_trial,
    score_wheel_multi_speed_trial,
    summarize_repeat_scores,
    twiddle_optimize,
    save_tuning_profile,
    write_json_file,
    wheel_pid_gains_from_dict,
    wheel_pid_gains_to_dict,
)


DEFAULT_AUTOTUNE_SEQUENCE = "15:500,25:500,35:500,45:500,35:500,25:500,15:500"
DEFAULT_AUTOTUNE_VERIFY_SEQUENCE = "15:300,25:300,35:300,45:300,35:300,25:300,15:300"
DEFAULT_AUTOTUNE_TAIL_ZERO_MS = 200
DEFAULT_AUTOTUNE_REPEAT_COUNT = 3
DEFAULT_DUAL_REFINE_STEP = 2.0
DEFAULT_DUAL_PWM_HIGH_THRESHOLD = 3300.0
DEFAULT_DUAL_SPEED_RATIO_FLOOR = 0.88
MAX_VALID_ENCODER_SPEED = 200.0
DEFAULT_STAGE_CONFIRM_BATCHES = 2
TUNING_PRESETS = (
    {"name": "coarse", "kp_step": 10.0, "ki_step": 5.0, "repeat_each": 1},
    {"name": "fine", "kp_step": 5.0, "ki_step": 2.0, "repeat_each": 2},
    {"name": "micro", "kp_step": 2.0, "ki_step": 1.0, "repeat_each": 3},
)
SUMMARY_KEEP_COMBINED_RATIO = 0.90
SUMMARY_KEEP_TRIAL_RATIO = 1.05
SUMMARY_REJECT_COMBINED_RATIO = 1.15
SUMMARY_REJECT_TRIAL_RATIO = 1.20
SUMMARY_PRIMARY_TARGET_MIN = 15.0
SUMMARY_PRIMARY_SCORE_REJECT_RATIO = 1.50
SUMMARY_PRIMARY_SCORE_REJECT_DELTA = 3.0
SUMMARY_PRIMARY_SPEED_DROP_RATIO = 0.08
SUMMARY_PRIMARY_SPEED_DROP_MIN = 1.0
SUMMARY_HISTORY_PATH = pathlib.Path(__file__).resolve().parents[1] / "logs" / "tuning_history_summary.csv"
SEGMENT_HISTORY_PATH = pathlib.Path(__file__).resolve().parents[1] / "logs" / "tuning_history_segments.csv"
DEFAULT_BATCH_HISTORY_LIMIT = 20

SUMMARY_HISTORY_HEADERS = [
    "timestamp",
    "stage",
    "pid_label",
    "left_kp",
    "left_ki",
    "left_kd",
    "right_kp",
    "right_ki",
    "right_kd",
    "a_score",
    "b_score",
    "combined_score",
    "decision",
]
SEGMENT_HISTORY_HEADERS = [
    "timestamp",
    "stage",
    "pid_label",
    "trial_label",
    "wheel",
    "target_speed",
    "sample_count",
    "mean_speed",
    "segment_score",
    "rise_ratio",
    "settle_ratio",
    "overshoot",
    "steady_error",
]


class CandidateLimitReached(Exception):
    def __init__(self, count, stage_name, best_pair, best_score):
        Exception.__init__(self, "candidate limit reached")
        self.count = count
        self.stage_name = stage_name
        self.best_pair = best_pair
        self.best_score = best_score


class CandidateLimitTracker(object):
    def __init__(self, limit):
        self.limit = int(limit or 0)
        self.count = 0
        self.current_stage_name = ""
        self.best_pair = None
        self.best_score = float("inf")

    def observe(self, stage_name, reported_gains, combined_score):
        if self.limit <= 0:
            return

        if self.current_stage_name != stage_name:
            self.current_stage_name = stage_name
            self.best_pair = reported_gains
            self.best_score = combined_score
        elif combined_score < self.best_score:
            self.best_pair = reported_gains
            self.best_score = combined_score

        self.count += 1
        if self.count >= self.limit:
            raise CandidateLimitReached(
                self.count,
                self.current_stage_name,
                self.best_pair,
                self.best_score,
            )

def resolve_air_dual_profile_defaults(args):
    profile = load_tuning_profile(getattr(args, "profile_path", str(DEFAULT_TUNING_PROFILE_PATH)), required=False)
    autotune_sequence = args.autotune_sequence
    verify_sequence = args.autotune_verify_sequence
    initial_pair = WheelPidGains(
        PidGains(args.initial_kp, args.initial_ki, args.initial_kd),
        PidGains(args.initial_kp, args.initial_ki, args.initial_kd),
    )
    shared_targets = profile.get("shared_targets")
    default_sequences = {}
    custom_sequences = {}
    seed_pair = None

    if isinstance(shared_targets, dict):
        default_sequences = shared_targets.get("default_sequences", {})
        custom_sequences = shared_targets.get("custom_sequences", {})

    if not getattr(args, "autotune_sequence_explicit", 0):
        sequence_rows = custom_sequences.get("air_primary")
        if not sequence_rows:
            sequence_rows = default_sequences.get("air_primary")
        sequence_text = profile_rows_to_sequence_text(sequence_rows)
        if sequence_text:
            autotune_sequence = sequence_text

    if not getattr(args, "autotune_verify_sequence_explicit", 0):
        verify_rows = custom_sequences.get("air_verify")
        if not verify_rows:
            verify_rows = default_sequences.get("air_verify")
        verify_text = profile_rows_to_sequence_text(verify_rows)
        if verify_text:
            verify_sequence = verify_text

    best_pair = wheel_pid_gains_from_dict(profile.get("air_dual", {}).get("best_pid"))
    if best_pair is not None:
        initial_pair = best_pair

    seed_pair = wheel_pid_gains_from_dict(profile.get("pwm_identify", {}).get("seed_pi"))
    if seed_pair is not None and best_pair is None:
        initial_pair = seed_pair

    return {
        "profile": profile,
        "profile_path": resolve_profile_path(getattr(args, "profile_path", str(DEFAULT_TUNING_PROFILE_PATH))),
        "autotune_sequence": autotune_sequence,
        "verify_sequence": verify_sequence,
        "initial_pair": initial_pair,
        "shared_targets": shared_targets,
    }


def resolve_air_dual_step_start_pair(profile, default_pair):
    air_dual_state = profile.get("air_dual", {})
    active_batch = air_dual_state.get("active_batch", {})
    if not isinstance(active_batch, dict):
        active_batch = {}
    active_best = wheel_pid_gains_from_dict(active_batch.get("current_best_pid"))
    if active_best is not None:
        return active_best

    last_batch_best_block = air_dual_state.get("last_batch_best", {})
    if not isinstance(last_batch_best_block, dict):
        last_batch_best_block = {}
    last_batch_best = wheel_pid_gains_from_dict(last_batch_best_block.get("best_pid"))
    if last_batch_best is not None:
        return last_batch_best

    best_pair = wheel_pid_gains_from_dict(air_dual_state.get("best_pid"))
    if best_pair is not None:
        return best_pair

    seed_pair = wheel_pid_gains_from_dict(profile.get("pwm_identify", {}).get("seed_pi"))
    if seed_pair is not None:
        return seed_pair

    return default_pair


def resolve_air_dual_step_baseline_pair(profile, fallback_pair):
    baseline_pair = wheel_pid_gains_from_dict(profile.get("air_dual", {}).get("baseline_pid"))
    if baseline_pair is not None:
        return baseline_pair
    return fallback_pair


def _resolve_air_step_result_path(args, profile_path):
    if getattr(args, "result_json", ""):
        return pathlib.Path(args.result_json)
    return agent_session.build_round_result_path(profile_path.parent, "air_dual", getattr(args, "batch_id", "air_step"), getattr(args, "round_index", 1))


def _resolve_air_step_waveform_path(args, profile_path):
    if getattr(args, "waveform_path", ""):
        return pathlib.Path(args.waveform_path)
    return agent_session.build_waveform_path(profile_path.parent, "air_dual", getattr(args, "batch_id", "air_step"), getattr(args, "round_index", 1))


def _resolve_band_targets(shared_targets):
    bands = {}
    targets = {}

    if isinstance(shared_targets, dict):
        bands = shared_targets.get("bands", {})

    for band_name, default_target in common.PROFILE_BAND_SPEEDS.items():
        try:
            targets[band_name] = abs(float(bands.get(band_name, default_target)))
        except (TypeError, ValueError):
            targets[band_name] = abs(float(default_target))

    return targets


def _build_band_scores_from_display(display, shared_targets):
    segment_scores = {}
    band_targets = _resolve_band_targets(shared_targets)

    for wheel_summary in display.get("wheels", {}).values():
        for segment in wheel_summary.get("segments", []):
            score = segment.get("score")
            target_speed = abs(float(segment.get("target_speed", 0.0)))
            if score is None or target_speed <= 0.0:
                continue
            segment_scores.setdefault(target_speed, []).append(float(score))

    if not segment_scores:
        return normalize_band_scores(None)

    averaged = {}
    for target_speed, scores in segment_scores.items():
        averaged[target_speed] = sum(scores) / float(len(scores))

    resolved = {}
    for band_name, target_speed in band_targets.items():
        nearest_speed = min(averaged.keys(), key=lambda value: abs(value - target_speed))
        resolved[band_name] = averaged[nearest_speed]

    return normalize_band_scores(resolved)


def _sample_to_dict(sample):
    if hasattr(sample, "_asdict"):
        return dict(sample._asdict())
    return {
        "target": float(getattr(sample, "target", 0.0)),
        "left_speed": float(getattr(sample, "left_speed", 0.0)),
        "right_speed": float(getattr(sample, "right_speed", 0.0)),
        "left_pwm": float(getattr(sample, "left_pwm", 0.0)),
        "right_pwm": float(getattr(sample, "right_pwm", 0.0)),
        "trial_active": float(getattr(sample, "trial_active", 0.0)),
        "stop_flag": float(getattr(sample, "stop_flag", 0.0)),
        "mode_id": float(getattr(sample, "mode_id", 0.0)),
        "left_cmd_pwm": float(getattr(sample, "left_cmd_pwm", 0.0)),
        "right_cmd_pwm": float(getattr(sample, "right_cmd_pwm", 0.0)),
    }


def _write_waveform_jsonl(path, sample_runs):
    waveform_path = pathlib.Path(path)
    waveform_path.parent.mkdir(parents=True, exist_ok=True)

    with waveform_path.open("w", encoding="utf-8") as handle:
        run_index = 0
        for samples in sample_runs:
            sample_index = 0
            for sample in samples:
                row = _sample_to_dict(sample)
                row["run_index"] = run_index + 1
                row["sample_index"] = sample_index
                handle.write(json.dumps(row, ensure_ascii=False))
                handle.write("\n")
                sample_index += 1
            run_index += 1

    return waveform_path


def _build_waveform_digest(sample_runs):
    peak_windows = []
    tail_jitter = 0.0
    run_index = 0

    for samples in sample_runs:
        if not samples:
            run_index += 1
            continue

        peak_speed = max([max(abs(sample.left_speed), abs(sample.right_speed)) for sample in samples])
        tail_samples = samples[-3:]
        tail_speeds = [max(abs(sample.left_speed), abs(sample.right_speed)) for sample in tail_samples]
        if tail_speeds:
            tail_jitter = max(tail_jitter, max(tail_speeds) - min(tail_speeds))

        peak_windows.append(
            {
                "run_index": run_index + 1,
                "peak_speed": float(peak_speed),
            }
        )
        run_index += 1

    return {
        "tail_jitter": float(tail_jitter),
        "peak_windows": peak_windows,
    }


def _compute_pwm_saturation_ratio(sample_runs, threshold=DEFAULT_DUAL_PWM_HIGH_THRESHOLD):
    total_count = 0
    saturated_count = 0

    for samples in sample_runs:
        for sample in samples:
            total_count += 1
            if max(abs(sample.left_pwm), abs(sample.right_pwm), abs(sample.left_cmd_pwm), abs(sample.right_cmd_pwm)) >= threshold:
                saturated_count += 1

    if total_count == 0:
        return 0.0

    return saturated_count / float(total_count)


def _compute_stop_clean_flag(sample_runs):
    if not sample_runs:
        return 0

    for samples in sample_runs:
        if not samples:
            return 0
        last = samples[-1]
        residual_speed = max(abs(last.left_speed), abs(last.right_speed))
        residual_pwm = max(abs(last.left_pwm), abs(last.right_pwm))
        if last.stop_flag < 0.5 or residual_speed > 3.0 or residual_pwm > 400.0:
            return 0

    return 1


def _compute_speed_drop_flag(display, min_target_speed):
    for wheel_summary in display.get("wheels", {}).values():
        for segment in wheel_summary.get("segments", []):
            target_speed = abs(float(segment.get("target_speed", 0.0)))
            mean_speed = segment.get("mean_speed")
            if target_speed < min_target_speed or mean_speed is None:
                continue
            if abs(float(mean_speed)) < target_speed * DEFAULT_DUAL_SPEED_RATIO_FLOOR:
                return 1
    return 0


def _compute_overshoot_flag(display, overshoot_gate):
    for wheel_summary in display.get("wheels", {}).values():
        for segment in wheel_summary.get("segments", []):
            overshoot = segment.get("overshoot")
            if overshoot is not None and float(overshoot) > overshoot_gate:
                return 1
    return 0


def _evaluate_air_dual_step_candidate(client, args, trial, verify_trial, score_config, gains_pair):
    del verify_trial
    sample_runs = []
    repeat_each = max(1, int(getattr(args, "repeat_each", DEFAULT_AUTOTUNE_REPEAT_COUNT) or 1))

    run_index = 0
    while run_index < repeat_each:
        sample_runs.append(
            run_autotune_trial(
                client,
                gains_pair,
                trial,
                args.rest_seconds,
                tail_zero_ms=args.autotune_tail_zero_ms,
            )
        )
        run_index += 1

    result = evaluate_repeated_dual_candidate(
        sample_runs,
        trial,
        score_config=score_config,
        min_target_speed=args.score_min_target_speed,
        overshoot_gate=score_config.overshoot_gate,
        required_overshoot_runs=args.kd_overshoot_runs,
    )
    display = _build_trial_display_summary(
        sample_runs,
        trial,
        ["left", "right"],
        score_config=score_config,
        min_target_speed=args.score_min_target_speed,
    )
    score = result.median_score

    return (
        {
            "gains": gains_pair,
            "score": score,
            "combined_score": score,
            "a_score": score,
            "b_score": score,
            "left_score": display["wheels"]["left"]["total_score"],
            "right_score": display["wheels"]["right"]["total_score"],
            "display": display,
            "persistent_overshoot": int(result.persistent_overshoot),
        },
        sample_runs,
    )


def run_air_dual_step(client, args):
    resolved = resolve_air_dual_profile_defaults(args)
    trial = build_autotune_trial(resolved["autotune_sequence"])
    verify_trial = build_autotune_verify_trial(resolved["verify_sequence"])
    score_config = build_score_config(args)
    profile = resolved["profile"]
    default_pair = resolved["initial_pair"]
    candidate_pair = load_wheel_pid_gains_json(getattr(args, "candidate_json", ""), None)
    if candidate_pair is None:
        candidate_pair = resolve_air_dual_step_start_pair(profile, default_pair)

    baseline_pair = load_wheel_pid_gains_json(getattr(args, "baseline_json", ""), None)
    if baseline_pair is None:
        baseline_pair = resolve_air_dual_step_baseline_pair(profile, candidate_pair)

    evaluation, sample_runs = _evaluate_air_dual_step_candidate(
        client,
        args,
        trial,
        verify_trial,
        score_config,
        candidate_pair,
    )
    result_path = _resolve_air_step_result_path(args, resolved["profile_path"])
    waveform_path = _resolve_air_step_waveform_path(args, resolved["profile_path"])
    _write_waveform_jsonl(waveform_path, sample_runs)

    display = evaluation.get("display", {})
    payload = {
        "mode": MODE_AIR_DUAL_STEP,
        "batch_id": getattr(args, "batch_id", ""),
        "round_index": int(getattr(args, "round_index", 1) or 1),
        "candidate_pid": wheel_pid_gains_to_dict(candidate_pair),
        "baseline_pid": wheel_pid_gains_to_dict(baseline_pair),
        "a_score": float(evaluation.get("a_score", evaluation.get("combined_score", float("inf")))),
        "b_score": float(evaluation.get("b_score", evaluation.get("combined_score", float("inf")))),
        "combined_score": float(evaluation.get("combined_score", evaluation.get("score", float("inf")))),
        "left_score": float(evaluation.get("left_score", float("inf"))),
        "right_score": float(evaluation.get("right_score", float("inf"))),
        "band_scores": _build_band_scores_from_display(display, resolved.get("shared_targets")),
        "stage_reached": "step_completed",
        "overshoot_flag": _compute_overshoot_flag(display, score_config.overshoot_gate),
        "persistent_overshoot_flag": int(evaluation.get("persistent_overshoot", 0)),
        "speed_drop_flag": _compute_speed_drop_flag(display, args.score_min_target_speed),
        "stop_clean_flag": _compute_stop_clean_flag(sample_runs),
        "pwm_saturation_ratio": _compute_pwm_saturation_ratio(sample_runs),
        "waveform_path": waveform_path.as_posix(),
        "waveform_digest": _build_waveform_digest(sample_runs),
        "result_path": result_path.as_posix(),
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
    }
    write_json_file(result_path, payload)
    return payload

def _filter_projected_samples(projected_samples):
    filtered = []

    for sample in projected_samples:
        if abs(sample.left_speed) > MAX_VALID_ENCODER_SPEED:
            continue
        filtered.append(sample)

    return filtered


def _build_empty_segment_summary(target_speed):
    return {
        "target_speed": target_speed,
        "sample_count": 0,
        "mean_speed": None,
        "score": None,
        "rise_ratio": None,
        "settle_ratio": None,
        "overshoot": None,
        "steady_error": None,
    }


def _summarize_trial_for_wheel(samples, trial, wheel_name, score_config=None, min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED):
    projected = _filter_projected_samples(_project_samples_to_wheel(samples, wheel_name))
    groups = _split_ground_load_segments(projected, trial)
    segment_summaries = []
    segment_scores = []
    segment_index = 0

    if score_config is None:
        score_config = DEFAULT_SCORE_CONFIG

    while segment_index < len(trial.segments_ms):
        target_speed = trial.segments_ms[segment_index][0]
        group = []
        if segment_index < len(groups):
            group = groups[segment_index]

        if not group:
            segment_summaries.append(_build_empty_segment_summary(target_speed))
            segment_index += 1
            continue

        mean_speed = sum([sample.left_speed for sample in group]) / float(len(group))
        if len(group) < 3:
            segment_summaries.append(
                {
                    "target_speed": target_speed,
                    "sample_count": len(group),
                    "mean_speed": mean_speed,
                    "score": None,
                    "rise_ratio": None,
                    "settle_ratio": None,
                    "overshoot": None,
                    "steady_error": None,
                }
            )
            segment_index += 1
            continue

        metrics = analyze_trial(group, score_config=score_config)
        segment_summaries.append(
            {
                "target_speed": target_speed,
                "sample_count": len(group),
                "mean_speed": mean_speed,
                "score": metrics.score,
                "rise_ratio": metrics.rise_ratio,
                "settle_ratio": metrics.settle_ratio,
                "overshoot": metrics.overshoot,
                "steady_error": metrics.steady_error,
            }
        )
        if abs(target_speed) >= min_target_speed:
            segment_scores.append(metrics.score)
        segment_index += 1

    total_score = float("inf")
    if segment_scores:
        total_score = combine_multi_speed_scores(segment_scores)

    return {
        "wheel_name": wheel_name,
        "total_score": total_score,
        "segments": segment_summaries,
    }


def _aggregate_segment_values(values, integer_value=0):
    valid_values = [value for value in values if value is not None]

    if not valid_values:
        return None
    if integer_value:
        return int(round(_median_value(valid_values)))
    return _median_value(valid_values)


def _aggregate_repeated_trial_segments(run_summaries):
    if not run_summaries:
        return []

    aggregated = []
    segment_index = 0
    segment_count = len(run_summaries[0]["segments"])

    while segment_index < segment_count:
        values = [summary["segments"][segment_index] for summary in run_summaries]
        aggregated.append(
            {
                "target_speed": values[0]["target_speed"],
                "sample_count": _aggregate_segment_values([value["sample_count"] for value in values], integer_value=1),
                "mean_speed": _aggregate_segment_values([value["mean_speed"] for value in values]),
                "score": _aggregate_segment_values([value["score"] for value in values]),
                "rise_ratio": _aggregate_segment_values([value["rise_ratio"] for value in values]),
                "settle_ratio": _aggregate_segment_values([value["settle_ratio"] for value in values]),
                "overshoot": _aggregate_segment_values([value["overshoot"] for value in values]),
                "steady_error": _aggregate_segment_values([value["steady_error"] for value in values]),
            }
        )
        segment_index += 1

    return aggregated


def _build_trial_display_summary(sample_runs, trial, wheel_names, score_config=None, min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED):
    run_summaries = {}
    wheel_scores = []
    wheel_name_index = 0

    if score_config is None:
        score_config = DEFAULT_SCORE_CONFIG

    while wheel_name_index < len(wheel_names):
        wheel_name = wheel_names[wheel_name_index]
        run_summaries[wheel_name] = []

        for samples in sample_runs:
            run_summaries[wheel_name].append(
                _summarize_trial_for_wheel(
                    samples,
                    trial,
                    wheel_name,
                    score_config=score_config,
                    min_target_speed=min_target_speed,
                )
            )

        wheel_scores.append(_median_value([summary["total_score"] for summary in run_summaries[wheel_name]]))
        wheel_name_index += 1

    display_score = wheel_scores[0]
    if len(wheel_scores) > 1:
        display_score = combine_multi_speed_scores(wheel_scores, worst_weight=0.6)

    aggregated_wheels = {}
    for wheel_name in wheel_names:
        aggregated_wheels[wheel_name] = {
            "total_score": _median_value([summary["total_score"] for summary in run_summaries[wheel_name]]),
            "segments": _aggregate_repeated_trial_segments(run_summaries[wheel_name]),
        }

    return {
        "total_score": display_score,
        "wheels": aggregated_wheels,
    }

def _parse_sequence_text(sequence_text):
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


def build_autotune_trial(sequence_text=None):
    if sequence_text is None:
        sequence_text = DEFAULT_AUTOTUNE_SEQUENCE

    segments = _parse_sequence_text(sequence_text)
    if sequence_text == DEFAULT_AUTOTUNE_SEQUENCE:
        name = "autotune_15_25_35_45_35_25_15"
    else:
        name = "autotune_custom"

    return GroundLoadTrial(
        name,
        segments,
        sum([segment[1] for segment in segments]),
    )


def build_autotune_verify_trial(sequence_text=None):
    if sequence_text is None:
        sequence_text = DEFAULT_AUTOTUNE_VERIFY_SEQUENCE

    segments = _parse_sequence_text(sequence_text)
    if sequence_text == DEFAULT_AUTOTUNE_VERIFY_SEQUENCE:
        name = "autotune_verify_15_25_35_45_35_25_15"
    else:
        name = "autotune_verify_custom"

    return GroundLoadTrial(
        name,
        segments,
        sum([segment[1] for segment in segments]),
    )


def _build_autotune_capture_events(trial, tail_zero_ms):
    events = []
    elapsed_ms = 0
    for target_speed, hold_ms in trial.segments_ms[:-1]:
        elapsed_ms += hold_ms
        next_speed = trial.segments_ms[len(events) + 1][0]
        events.append((elapsed_ms / 1000.0, "TEST_speed={0}".format(format_gain(next_speed))))
    if tail_zero_ms > 0:
        events.append(
            (
                float(trial.trial_ms) / 1000.0,
                "TEST_speed={0}".format(format_gain(0.0)),
            )
        )
    return events


def run_autotune_trial(
    client,
    gains,
    trial,
    rest_seconds,
    tail_zero_ms=DEFAULT_AUTOTUNE_TAIL_ZERO_MS,
    sleep_fn=time.sleep,
):
    client.send_command("TEST_speed=0")
    sleep_fn(rest_seconds)
    client.send_command("AT_RESET")
    apply_speed_gains(client, gains)
    client.send_command("START")
    client.send_command("TEST_speed={0}".format(format_gain(trial.segments_ms[0][0])))
    samples = client.capture_trial(
        float(trial.trial_ms + tail_zero_ms) / 1000.0,
        events=_build_autotune_capture_events(trial, tail_zero_ms),
    )
    client.send_command("TEST_speed=0")
    return samples

def _dual_pwm_margin_penalty(
    samples,
    min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED,
    pwm_high_threshold=DEFAULT_DUAL_PWM_HIGH_THRESHOLD,
    speed_ratio_floor=DEFAULT_DUAL_SPEED_RATIO_FLOOR,
):
    penalty = 0.0

    if pwm_high_threshold <= 0.0:
        return 0.0

    for sample in samples:
        target_abs = abs(sample.target)
        if target_abs < min_target_speed or target_abs < 0.001:
            continue

        if (
            abs(sample.left_pwm) >= pwm_high_threshold
            and abs(sample.right_pwm) >= pwm_high_threshold
        ):
            speed_ratio = (
                abs(sample.left_speed) + abs(sample.right_speed)
            ) / (2.0 * target_abs)
            if speed_ratio < speed_ratio_floor:
                deficit = speed_ratio_floor - speed_ratio
                pwm_ratio = (
                    abs(sample.left_pwm) + abs(sample.right_pwm)
                ) / (2.0 * pwm_high_threshold)
                penalty += 160.0 + 520.0 * deficit * pwm_ratio

    return penalty


def score_dual_wheel_multi_speed_trial(
    samples,
    trial,
    score_config=None,
    min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED,
):
    left_score = score_wheel_multi_speed_trial(
        samples,
        trial,
        "left",
        score_config=score_config,
        min_target_speed=min_target_speed,
    )
    right_score = score_wheel_multi_speed_trial(
        samples,
        trial,
        "right",
        score_config=score_config,
        min_target_speed=min_target_speed,
    )

    return (
        combine_multi_speed_scores([left_score, right_score], worst_weight=0.6)
        + _dual_pwm_margin_penalty(
            samples,
            min_target_speed=min_target_speed,
        )
    )


def evaluate_repeated_wheel_candidate(
    sample_runs,
    trial,
    wheel_name,
    score_config=None,
    min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED,
    overshoot_gate=DEFAULT_SCORE_CONFIG.overshoot_gate,
    required_overshoot_runs=DEFAULT_KD_OVERSHOOT_RUNS,
):
    projected_runs = []
    scores = []
    overshoot_ratios = []

    if score_config is None:
        score_config = DEFAULT_SCORE_CONFIG

    for samples in sample_runs:
        projected_runs.append(_project_samples_to_wheel(samples, wheel_name))

    for samples in projected_runs:
        scores.append(
            score_multi_speed_trial(
                samples,
                trial,
                score_config=score_config,
                min_target_speed=min_target_speed,
            )
        )
        overshoot_ratios.append(
            _max_segment_overshoot_ratio(
                samples,
                trial,
                score_config=score_config,
                min_target_speed=min_target_speed,
            )
        )

    repeat_summary = summarize_repeat_scores(
        scores,
        overshoot_ratios,
        overshoot_gate,
        required_overshoot_runs,
    )

    return WheelCandidateEvaluation(
        repeat_summary.median_score,
        scores,
        overshoot_ratios,
        repeat_summary.persistent_overshoot,
    )


def evaluate_repeated_dual_candidate(
    sample_runs,
    trial,
    score_config=None,
    min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED,
    overshoot_gate=DEFAULT_SCORE_CONFIG.overshoot_gate,
    required_overshoot_runs=DEFAULT_KD_OVERSHOOT_RUNS,
):
    scores = []
    overshoot_ratios = []

    if score_config is None:
        score_config = DEFAULT_SCORE_CONFIG

    for samples in sample_runs:
        scores.append(
            score_dual_wheel_multi_speed_trial(
                samples,
                trial,
                score_config=score_config,
                min_target_speed=min_target_speed,
            )
        )
        overshoot_ratios.append(
            _max_pair_overshoot_ratio(
                samples,
                trial,
                score_config=score_config,
                min_target_speed=min_target_speed,
            )
        )

    repeat_summary = summarize_repeat_scores(
        scores,
        overshoot_ratios,
        overshoot_gate,
        required_overshoot_runs,
    )

    return WheelCandidateEvaluation(
        repeat_summary.median_score,
        scores,
        overshoot_ratios,
        repeat_summary.persistent_overshoot,
    )


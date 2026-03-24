import csv
import pathlib
import time

from .common import (
    DEFAULT_AUTOTUNE_SEARCH_TOLERANCE,
    DEFAULT_KD_OVERSHOOT_RUNS,
    DEFAULT_MIN_SCORE_TARGET_SPEED,
    DEFAULT_SCORE_CONFIG,
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
    score_multi_speed_trial,
    score_wheel_multi_speed_trial,
    summarize_repeat_scores,
    twiddle_optimize,
)


DEFAULT_AUTOTUNE_SEQUENCE = "15:500,25:500,35:500,45:500,35:500,25:500,15:500"
DEFAULT_AUTOTUNE_VERIFY_SEQUENCE = "15:300,25:300,35:300,45:300,35:300,25:300,15:300"
DEFAULT_AUTOTUNE_TAIL_ZERO_MS = 200
DEFAULT_AUTOTUNE_REPEAT_COUNT = 3
DEFAULT_DUAL_REFINE_STEP = 2.0
DEFAULT_DUAL_PWM_HIGH_THRESHOLD = 3300.0
DEFAULT_DUAL_SPEED_RATIO_FLOOR = 0.88
MAX_VALID_ENCODER_SPEED = 200.0
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


def _format_compact_gain(value):
    text = "{0:.4f}".format(value).rstrip("0").rstrip(".")
    if text == "-0":
        return "0"
    return text


def format_pid_pair_label(pair):
    return "L{0}/{1}/{2} R{3}/{4}/{5}".format(
        _format_compact_gain(pair.left.kp),
        _format_compact_gain(pair.left.ki),
        _format_compact_gain(pair.left.kd),
        _format_compact_gain(pair.right.kp),
        _format_compact_gain(pair.right.ki),
        _format_compact_gain(pair.right.kd),
    )


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


def _build_primary_segment_index(trial_display):
    segment_index = {}

    for wheel_name, wheel_summary in trial_display["wheels"].items():
        for segment in wheel_summary["segments"]:
            target_speed = abs(segment["target_speed"])
            if target_speed < SUMMARY_PRIMARY_TARGET_MIN:
                continue
            segment_index[(wheel_name, target_speed)] = segment

    return segment_index


def _has_primary_segment_regression(candidate_display, baseline_display):
    candidate_index = _build_primary_segment_index(candidate_display)
    baseline_index = _build_primary_segment_index(baseline_display)

    for key, baseline_segment in baseline_index.items():
        candidate_segment = candidate_index.get(key)
        if candidate_segment is None:
            return 1

        baseline_score = baseline_segment["score"]
        candidate_score = candidate_segment["score"]
        baseline_speed = baseline_segment["mean_speed"]
        candidate_speed = candidate_segment["mean_speed"]
        target_speed = key[1]

        if baseline_score is not None and candidate_score is None:
            return 1

        if baseline_score is not None and candidate_score is not None:
            if (
                candidate_score > baseline_score + SUMMARY_PRIMARY_SCORE_REJECT_DELTA
                and candidate_score > baseline_score * SUMMARY_PRIMARY_SCORE_REJECT_RATIO
            ):
                return 1

        if baseline_speed is not None and candidate_speed is not None:
            if candidate_speed < baseline_speed - max(SUMMARY_PRIMARY_SPEED_DROP_MIN, target_speed * SUMMARY_PRIMARY_SPEED_DROP_RATIO):
                return 1

    return 0


def build_stage_baseline_reference(a_score, b_score, combined_score, a_display, b_display):
    return {
        "a_score": a_score,
        "b_score": b_score,
        "combined_score": combined_score,
        "a_display": a_display,
        "b_display": b_display,
    }


def decide_candidate_status(a_score, b_score, combined_score, a_display, b_display, baseline_reference=None):
    if baseline_reference is None:
        return "复验"

    if (
        a_score == baseline_reference["a_score"]
        and b_score == baseline_reference["b_score"]
        and combined_score == baseline_reference["combined_score"]
    ):
        return "基线"

    primary_regression = _has_primary_segment_regression(a_display, baseline_reference["a_display"])
    if not primary_regression:
        primary_regression = _has_primary_segment_regression(b_display, baseline_reference["b_display"])

    if (
        combined_score <= baseline_reference["combined_score"] * SUMMARY_KEEP_COMBINED_RATIO
        and a_score <= baseline_reference["a_score"] * SUMMARY_KEEP_TRIAL_RATIO
        and b_score <= baseline_reference["b_score"] * SUMMARY_KEEP_TRIAL_RATIO
        and not primary_regression
    ):
        return "保留"

    if (
        combined_score > baseline_reference["combined_score"] * SUMMARY_REJECT_COMBINED_RATIO
        or a_score > baseline_reference["a_score"] * SUMMARY_REJECT_TRIAL_RATIO
        or b_score > baseline_reference["b_score"] * SUMMARY_REJECT_TRIAL_RATIO
        or primary_regression
    ):
        return "淘汰"

    return "复验"


def _append_csv_row(csv_path, headers, row):
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    write_header = not csv_path.exists()

    with csv_path.open("a", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=headers)
        if write_header:
            writer.writeheader()
        writer.writerow(row)


def append_candidate_history(stage_name, gains, a_score, b_score, combined_score, decision, a_display, b_display, timestamp_text=None):
    pid_label = format_pid_pair_label(gains)
    if timestamp_text is None:
        timestamp_text = time.strftime("%Y-%m-%d %H:%M:%S")

    _append_csv_row(
        SUMMARY_HISTORY_PATH,
        SUMMARY_HISTORY_HEADERS,
        {
            "timestamp": timestamp_text,
            "stage": stage_name,
            "pid_label": pid_label,
            "left_kp": gains.left.kp,
            "left_ki": gains.left.ki,
            "left_kd": gains.left.kd,
            "right_kp": gains.right.kp,
            "right_ki": gains.right.ki,
            "right_kd": gains.right.kd,
            "a_score": a_score,
            "b_score": b_score,
            "combined_score": combined_score,
            "decision": decision,
        },
    )

    for trial_label, display in (("A", a_display), ("B", b_display)):
        for wheel_name, wheel_summary in display["wheels"].items():
            for segment in wheel_summary["segments"]:
                _append_csv_row(
                    SEGMENT_HISTORY_PATH,
                    SEGMENT_HISTORY_HEADERS,
                    {
                        "timestamp": timestamp_text,
                        "stage": stage_name,
                        "pid_label": pid_label,
                        "trial_label": trial_label,
                        "wheel": wheel_name,
                        "target_speed": segment["target_speed"],
                        "sample_count": segment["sample_count"],
                        "mean_speed": segment["mean_speed"],
                        "segment_score": segment["score"],
                        "rise_ratio": segment["rise_ratio"],
                        "settle_ratio": segment["settle_ratio"],
                        "overshoot": segment["overshoot"],
                        "steady_error": segment["steady_error"],
                    },
                )


def format_candidate_summary_row(gains, a_score, b_score, combined_score, decision):
    return "{0:<31} {1:>6.2f} {2:>6.2f} {3:>6.2f}    {4}".format(
        format_pid_pair_label(gains),
        a_score,
        b_score,
        combined_score,
        decision,
    )


def emit_candidate_summary(stage_name, gains, a_score, b_score, combined_score, decision, header_state):
    if not header_state.get("printed"):
        print("{0} summary".format(stage_name))
        print("PID                             A总分   B总分   综合    结论")
        header_state["printed"] = 1

    print(format_candidate_summary_row(gains, a_score, b_score, combined_score, decision))


def build_isolated_wheel_pair(candidate_gains, wheel_name):
    if wheel_name == "left":
        return WheelPidGains(candidate_gains, ZERO_PID_GAINS)
    if wheel_name == "right":
        return WheelPidGains(ZERO_PID_GAINS, candidate_gains)
    raise ValueError("wheel_name must be 'left' or 'right'")


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


def _build_dual_pair_from_values(values, template_pair):
    return WheelPidGains(
        PidGains(
            _clamp_non_negative(values[0]),
            _clamp_non_negative(values[1]),
            template_pair.left.kd,
        ),
        PidGains(
            _clamp_non_negative(values[2]),
            _clamp_non_negative(values[3]),
            template_pair.right.kd,
        ),
    )


def _flatten_dual_pair_values(pair):
    return [pair.left.kp, pair.left.ki, pair.right.kp, pair.right.ki]


def twiddle_optimize_dual_pair(
    initial_pair,
    delta_pair,
    evaluator,
    iterations=10,
    tolerance=DEFAULT_AUTOTUNE_SEARCH_TOLERANCE,
):
    best = _build_dual_pair_from_values(_flatten_dual_pair_values(initial_pair), initial_pair)
    best_score = evaluator(best)
    working_deltas = [
        abs(delta_pair.left.kp),
        abs(delta_pair.left.ki),
        abs(delta_pair.right.kp),
        abs(delta_pair.right.ki),
    ]
    index_list = [0, 1, 2, 3]

    for _ in range(iterations):
        if sum(working_deltas) <= tolerance:
            break

        improved = 0
        for index in index_list:
            delta = working_deltas[index]
            if delta <= tolerance:
                continue

            values = _flatten_dual_pair_values(best)
            values[index] += delta
            candidate = _build_dual_pair_from_values(values, best)
            candidate_score = evaluator(candidate)
            if candidate_score < best_score:
                best = candidate
                best_score = candidate_score
                working_deltas[index] = delta * 1.15
                improved = 1
                continue

            values = _flatten_dual_pair_values(best)
            values[index] -= delta
            candidate = _build_dual_pair_from_values(values, best)
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


def _optimize_single_wheel(client, args, trial, verify_trial, score_config, wheel_name, fixed_pair):
    if wheel_name == "left":
        wheel_initial = fixed_pair.left
    else:
        wheel_initial = fixed_pair.right

    def collect_candidate(candidate_gains):
        run_index = 0
        sample_runs = []
        verify_runs = []
        applied_gains = build_isolated_wheel_pair(candidate_gains, wheel_name)

        while run_index < args.repeat_each:
            sample_runs.append(
                run_autotune_trial(
                    client,
                    applied_gains,
                    trial,
                    args.rest_seconds,
                    tail_zero_ms=args.autotune_tail_zero_ms,
                )
            )
            verify_runs.append(
                run_autotune_trial(
                    client,
                    applied_gains,
                    verify_trial,
                    args.rest_seconds,
                    tail_zero_ms=args.autotune_tail_zero_ms,
                )
            )
            run_index += 1

        a_result = evaluate_repeated_wheel_candidate(
            sample_runs,
            trial,
            wheel_name,
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
            overshoot_gate=score_config.overshoot_gate,
            required_overshoot_runs=args.kd_overshoot_runs,
        )
        b_result = evaluate_repeated_wheel_candidate(
            verify_runs,
            verify_trial,
            wheel_name,
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
            overshoot_gate=score_config.overshoot_gate,
            required_overshoot_runs=args.kd_overshoot_runs,
        )
        a_display = _build_trial_display_summary(
            sample_runs,
            trial,
            [wheel_name],
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
        )
        b_display = _build_trial_display_summary(
            verify_runs,
            verify_trial,
            [wheel_name],
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
        )
        combined_score = (a_result.median_score + b_result.median_score) * 0.5

        return {
            "applied_gains": applied_gains,
            "a_result": a_result,
            "b_result": b_result,
            "a_display": a_display,
            "b_display": b_display,
            "combined_score": combined_score,
        }

    def optimize_stage(stage_name, stage_initial, stage_delta, iterations):
        header_state = {"printed": 0}
        baseline_bundle = collect_candidate(stage_initial)
        baseline_reference = build_stage_baseline_reference(
            baseline_bundle["a_result"].median_score,
            baseline_bundle["b_result"].median_score,
            baseline_bundle["combined_score"],
            baseline_bundle["a_display"],
            baseline_bundle["b_display"],
        )

        emit_candidate_summary(
            stage_name,
            baseline_bundle["applied_gains"],
            baseline_bundle["a_result"].median_score,
            baseline_bundle["b_result"].median_score,
            baseline_bundle["combined_score"],
            "基线",
            header_state,
        )
        append_candidate_history(
            stage_name,
            baseline_bundle["applied_gains"],
            baseline_bundle["a_result"].median_score,
            baseline_bundle["b_result"].median_score,
            baseline_bundle["combined_score"],
            "基线",
            baseline_bundle["a_display"],
            baseline_bundle["b_display"],
        )

        def evaluator(candidate_gains):
            if (
                candidate_gains.kp == stage_initial.kp
                and candidate_gains.ki == stage_initial.ki
                and candidate_gains.kd == stage_initial.kd
            ):
                return baseline_bundle["a_result"].median_score

            candidate_bundle = collect_candidate(candidate_gains)
            decision = decide_candidate_status(
                candidate_bundle["a_result"].median_score,
                candidate_bundle["b_result"].median_score,
                candidate_bundle["combined_score"],
                candidate_bundle["a_display"],
                candidate_bundle["b_display"],
                baseline_reference=baseline_reference,
            )
            emit_candidate_summary(
                stage_name,
                candidate_bundle["applied_gains"],
                candidate_bundle["a_result"].median_score,
                candidate_bundle["b_result"].median_score,
                candidate_bundle["combined_score"],
                decision,
                header_state,
            )
            append_candidate_history(
                stage_name,
                candidate_bundle["applied_gains"],
                candidate_bundle["a_result"].median_score,
                candidate_bundle["b_result"].median_score,
                candidate_bundle["combined_score"],
                decision,
                candidate_bundle["a_display"],
                candidate_bundle["b_display"],
            )
            return candidate_bundle["a_result"].median_score

        best_candidate, best_score = twiddle_optimize(
            stage_initial,
            stage_delta,
            evaluator,
            iterations=iterations,
            tolerance=args.search_tolerance,
        )

        return best_candidate, best_score, collect_candidate(best_candidate)

    stage1_initial = PidGains(wheel_initial.kp, 0.0, 0.0)
    stage1_delta = PidGains(args.delta_kp, 0.0, 0.0)
    best, best_score, _ = optimize_stage(
        "{0}-isolated-kp".format(wheel_name),
        stage1_initial,
        stage1_delta,
        max(3, args.iterations // 2),
    )

    stage2_initial = PidGains(best.kp, wheel_initial.ki, 0.0)
    stage2_delta = PidGains(max(2.0, args.delta_kp * 0.5), args.delta_ki, 0.0)
    best, best_score, stage2_bundle = optimize_stage(
        "{0}-isolated-ki".format(wheel_name),
        stage2_initial,
        stage2_delta,
        args.iterations,
    )

    stage2_result = stage2_bundle["a_result"]
    if stage2_result.persistent_overshoot and args.delta_kd > 0.0:
        stage3_initial = PidGains(best.kp, best.ki, wheel_initial.kd)
        stage3_delta = PidGains(
            max(1.0, args.delta_kp * 0.25),
            max(1.0, args.delta_ki * 0.5),
            args.delta_kd,
        )
        best, best_score, _ = optimize_stage(
            "{0}-isolated-kd".format(wheel_name),
            stage3_initial,
            stage3_delta,
            args.iterations,
        )

    return best, best_score


def _optimize_dual_pair(client, args, trial, score_config, base_pair):
    verify_trial = build_autotune_verify_trial(args.autotune_verify_sequence)
    stage_name = "dual-refine"

    def collect_candidate(candidate_pair):
        run_index = 0
        sample_runs = []
        verify_runs = []

        while run_index < args.repeat_each:
            sample_runs.append(
                run_autotune_trial(
                    client,
                    candidate_pair,
                    trial,
                    args.rest_seconds,
                    tail_zero_ms=args.autotune_tail_zero_ms,
                )
            )
            verify_runs.append(
                run_autotune_trial(
                    client,
                    candidate_pair,
                    verify_trial,
                    args.rest_seconds,
                    tail_zero_ms=args.autotune_tail_zero_ms,
                )
            )
            run_index += 1

        a_result = evaluate_repeated_dual_candidate(
            sample_runs,
            trial,
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
            overshoot_gate=score_config.overshoot_gate,
            required_overshoot_runs=args.kd_overshoot_runs,
        )
        b_result = evaluate_repeated_dual_candidate(
            verify_runs,
            verify_trial,
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
            overshoot_gate=score_config.overshoot_gate,
            required_overshoot_runs=args.kd_overshoot_runs,
        )
        a_display = _build_trial_display_summary(
            sample_runs,
            trial,
            ["left", "right"],
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
        )
        b_display = _build_trial_display_summary(
            verify_runs,
            verify_trial,
            ["left", "right"],
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
        )
        combined_score = (a_result.median_score + b_result.median_score) * 0.5
        return {
            "a_result": a_result,
            "b_result": b_result,
            "a_display": a_display,
            "b_display": b_display,
            "combined_score": combined_score,
        }

    header_state = {"printed": 0}
    baseline_bundle = collect_candidate(base_pair)
    baseline_reference = build_stage_baseline_reference(
        baseline_bundle["a_result"].median_score,
        baseline_bundle["b_result"].median_score,
        baseline_bundle["combined_score"],
        baseline_bundle["a_display"],
        baseline_bundle["b_display"],
    )

    emit_candidate_summary(
        stage_name,
        base_pair,
        baseline_bundle["a_result"].median_score,
        baseline_bundle["b_result"].median_score,
        baseline_bundle["combined_score"],
        "基线",
        header_state,
    )
    append_candidate_history(
        stage_name,
        base_pair,
        baseline_bundle["a_result"].median_score,
        baseline_bundle["b_result"].median_score,
        baseline_bundle["combined_score"],
        "基线",
        baseline_bundle["a_display"],
        baseline_bundle["b_display"],
    )

    def evaluator(candidate_pair):
        if candidate_pair == base_pair:
            return baseline_bundle["a_result"].median_score

        candidate_bundle = collect_candidate(candidate_pair)
        decision = decide_candidate_status(
            candidate_bundle["a_result"].median_score,
            candidate_bundle["b_result"].median_score,
            candidate_bundle["combined_score"],
            candidate_bundle["a_display"],
            candidate_bundle["b_display"],
            baseline_reference=baseline_reference,
        )
        emit_candidate_summary(
            stage_name,
            candidate_pair,
            candidate_bundle["a_result"].median_score,
            candidate_bundle["b_result"].median_score,
            candidate_bundle["combined_score"],
            decision,
            header_state,
        )
        append_candidate_history(
            stage_name,
            candidate_pair,
            candidate_bundle["a_result"].median_score,
            candidate_bundle["b_result"].median_score,
            candidate_bundle["combined_score"],
            decision,
            candidate_bundle["a_display"],
            candidate_bundle["b_display"],
        )
        return candidate_bundle["a_result"].median_score

    dual_step = max(DEFAULT_DUAL_REFINE_STEP, args.search_tolerance * 2.0)
    delta_pair = WheelPidGains(
        PidGains(dual_step, dual_step, base_pair.left.kd),
        PidGains(dual_step, dual_step, base_pair.right.kd),
    )

    return twiddle_optimize_dual_pair(
        base_pair,
        delta_pair,
        evaluator,
        iterations=max(4, args.iterations // 2),
        tolerance=args.search_tolerance,
    )


def run_autotune(client, args):
    score_config = build_score_config(args)
    trial = build_autotune_trial(args.autotune_sequence)
    verify_trial = build_autotune_verify_trial(args.autotune_verify_sequence)
    initial_pair = WheelPidGains(
        PidGains(args.initial_kp, args.initial_ki, args.initial_kd),
        PidGains(args.initial_kp, args.initial_ki, args.initial_kd),
    )

    best_left, left_score = _optimize_single_wheel(
        client,
        args,
        trial,
        verify_trial,
        score_config,
        "left",
        initial_pair,
    )
    best_pair = WheelPidGains(best_left, initial_pair.right)

    best_right, right_score = _optimize_single_wheel(
        client,
        args,
        trial,
        verify_trial,
        score_config,
        "right",
        best_pair,
    )
    best_pair = WheelPidGains(best_pair.left, best_right)
    best_pair, pair_score = _optimize_dual_pair(
        client,
        args,
        trial,
        score_config,
        best_pair,
    )

    verification_samples = run_autotune_trial(
        client,
        best_pair,
        trial,
        args.rest_seconds,
        tail_zero_ms=args.autotune_tail_zero_ms,
    )
    left_score = score_wheel_multi_speed_trial(
        verification_samples,
        trial,
        "left",
        score_config=score_config,
        min_target_speed=args.score_min_target_speed,
    )
    verify_samples = run_autotune_trial(
        client,
        best_pair,
        verify_trial,
        args.rest_seconds,
        tail_zero_ms=args.autotune_tail_zero_ms,
    )
    left_score = max(
        left_score,
        score_wheel_multi_speed_trial(
            verify_samples,
            verify_trial,
            "left",
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
        ),
    )
    right_score = score_wheel_multi_speed_trial(
        verification_samples,
        trial,
        "right",
        score_config=score_config,
        min_target_speed=args.score_min_target_speed,
    )
    right_score = max(
        right_score,
        score_wheel_multi_speed_trial(
            verify_samples,
            verify_trial,
            "right",
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
        ),
    )

    del pair_score
    apply_speed_gains(client, best_pair)
    client.send_command("AT_RESET")
    client.send_command("TEST_speed=0")

    if args.save_best:
        client.send_command("SAVE")

    print(
        "best left kp={0:.4f} ki={1:.4f} kd={2:.4f} score={3:.4f}".format(
            best_pair.left.kp,
            best_pair.left.ki,
            best_pair.left.kd,
            left_score,
        )
    )
    print(
        "best right kp={0:.4f} ki={1:.4f} kd={2:.4f} score={3:.4f}".format(
            best_pair.right.kp,
            best_pair.right.ki,
            best_pair.right.kd,
            right_score,
        )
    )
    return 0


def run_air_dual_autotune(client, args):
    return run_autotune(client, args)

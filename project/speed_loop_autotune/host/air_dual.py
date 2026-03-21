import time

from .common import (
    DEFAULT_AUTOTUNE_SEARCH_TOLERANCE,
    DEFAULT_KD_OVERSHOOT_RUNS,
    DEFAULT_MIN_SCORE_TARGET_SPEED,
    DEFAULT_SCORE_CONFIG,
    GroundLoadTrial,
    PidGains,
    WheelCandidateEvaluation,
    WheelPidGains,
    ZERO_PID_GAINS,
    _clamp_non_negative,
    _max_pair_overshoot_ratio,
    _max_segment_overshoot_ratio,
    _project_samples_to_wheel,
    apply_speed_gains,
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


def _optimize_single_wheel(client, args, trial, score_config, wheel_name, fixed_pair):
    if wheel_name == "left":
        wheel_initial = fixed_pair.left
    else:
        wheel_initial = fixed_pair.right

    def evaluate_candidate(candidate_gains):
        run_index = 0
        sample_runs = []
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
            run_index += 1

        return applied_gains, evaluate_repeated_wheel_candidate(
            sample_runs,
            trial,
            wheel_name,
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
            overshoot_gate=score_config.overshoot_gate,
            required_overshoot_runs=args.kd_overshoot_runs,
        )

    def evaluator(candidate_gains):
        applied_gains, result = evaluate_candidate(candidate_gains)
        score_text = ",".join(["{0:.3f}".format(value) for value in result.scores])
        overshoot_text = ",".join(["{0:.3f}".format(value) for value in result.overshoot_ratios])
        print(
            "{0} trial lk={1:.4f}/{2:.4f}/{3:.4f} rk={4:.4f}/{5:.4f}/{6:.4f} score={7:.4f} runs=[{8}] overs=[{9}]".format(
                wheel_name,
                applied_gains.left.kp,
                applied_gains.left.ki,
                applied_gains.left.kd,
                applied_gains.right.kp,
                applied_gains.right.ki,
                applied_gains.right.kd,
                result.median_score,
                score_text,
                overshoot_text,
            )
        )
        return result.median_score

    stage1_initial = PidGains(wheel_initial.kp, 0.0, 0.0)
    stage1_delta = PidGains(args.delta_kp, 0.0, 0.0)
    best, best_score = twiddle_optimize(
        stage1_initial,
        stage1_delta,
        evaluator,
        iterations=max(3, args.iterations // 2),
        tolerance=args.search_tolerance,
    )

    stage2_initial = PidGains(best.kp, wheel_initial.ki, 0.0)
    stage2_delta = PidGains(max(2.0, args.delta_kp * 0.5), args.delta_ki, 0.0)
    best, best_score = twiddle_optimize(
        stage2_initial,
        stage2_delta,
        evaluator,
        iterations=args.iterations,
        tolerance=args.search_tolerance,
    )

    stage2_result = evaluate_candidate(best)[1]
    if stage2_result.persistent_overshoot and args.delta_kd > 0.0:
        stage3_initial = PidGains(best.kp, best.ki, wheel_initial.kd)
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
            tolerance=args.search_tolerance,
        )

    return best, best_score


def _optimize_dual_pair(client, args, trial, score_config, base_pair):
    def evaluate_candidate(candidate_pair):
        run_index = 0
        sample_runs = []

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
            run_index += 1

        return evaluate_repeated_dual_candidate(
            sample_runs,
            trial,
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
            overshoot_gate=score_config.overshoot_gate,
            required_overshoot_runs=args.kd_overshoot_runs,
        )

    def evaluator(candidate_pair):
        result = evaluate_candidate(candidate_pair)
        score_text = ",".join(["{0:.3f}".format(value) for value in result.scores])
        overshoot_text = ",".join(["{0:.3f}".format(value) for value in result.overshoot_ratios])
        print(
            "dual trial lk={0:.4f}/{1:.4f}/{2:.4f} rk={3:.4f}/{4:.4f}/{5:.4f} score={6:.4f} runs=[{7}] overs=[{8}]".format(
                candidate_pair.left.kp,
                candidate_pair.left.ki,
                candidate_pair.left.kd,
                candidate_pair.right.kp,
                candidate_pair.right.ki,
                candidate_pair.right.kd,
                result.median_score,
                score_text,
                overshoot_text,
            )
        )
        return result.median_score

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
        score_config,
        "left",
        initial_pair,
    )
    best_pair = WheelPidGains(best_left, initial_pair.right)

    best_right, right_score = _optimize_single_wheel(
        client,
        args,
        trial,
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

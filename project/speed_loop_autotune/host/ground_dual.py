import time

from .common import (
    DEFAULT_MIN_SCORE_TARGET_SPEED,
    DEFAULT_SCORE_CONFIG,
    GroundLoadTrial,
    PidGains,
    _build_trial_events,
    _segment_is_scored,
    _split_ground_load_segments,
    analyze_trial,
    apply_speed_gains,
    build_score_config,
    combine_multi_speed_scores,
    describe_gains,
    format_gain,
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


def _score_ground_load_trial(
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

    total_score = combine_multi_speed_scores(segment_scores)

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


def summarize_ground_load_trial(
    samples,
    trial,
    score_config=None,
    min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED,
):
    if score_config is None:
        score_config = DEFAULT_SCORE_CONFIG

    if not samples:
        return "{0} samples=0".format(trial.name)

    parts = [trial.name]
    segment_groups = _split_ground_load_segments(samples, trial)
    segment_index = 0

    for target_speed, hold_ms in trial.segments_ms:
        if not _segment_is_scored(target_speed, min_target_speed):
            parts.append("v{0:.0f}/{1}ms skip".format(target_speed, hold_ms))
        elif segment_index < len(segment_groups) and segment_groups[segment_index]:
            metrics = analyze_trial(segment_groups[segment_index], score_config=score_config)
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


def score_ground_load_group(
    trial_sample_groups,
    trials=None,
    score_config=None,
    min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED,
):
    if not trial_sample_groups:
        return float("inf")

    if trials is None:
        trials = build_ground_load_trials()

    if len(trial_sample_groups) != len(trials):
        return float("inf")

    total_score = 0.0

    for trial, samples in zip(trials, trial_sample_groups):
        if not samples:
            return float("inf")
        total_score += _score_ground_load_trial(
            samples,
            trial,
            score_config=score_config,
            min_target_speed=min_target_speed,
        )

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
    score_config=None,
    min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED,
    sleep_fn=time.sleep,
):
    if trials is None:
        trials = build_ground_load_trials()

    if wait_for_operator is None:
        def wait_for_operator(message):
            input(message)

    wait_for_operator(
        "澶嶄綅鍒拌捣鐐瑰悗鎸夊洖杞︼細{0}".format(describe_gains(gains))
    )

    client.send_command("AT_FUYA={0}".format(int(fuya_pwm)))
    client.send_command("AT_COOLDOWN_MS={0}".format(int(cooldown_ms)))
    apply_speed_gains(client, gains)
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

    return (
        score_ground_load_group(
            group_results,
            trials=trials,
            score_config=score_config,
            min_target_speed=min_target_speed,
        ),
        group_results,
    )


def _average_group_overshoot(
    group_results,
    trials=None,
    min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED,
):
    if not group_results:
        return 0.0

    if trials is None:
        trials = build_ground_load_trials()

    overshoot_sum = 0.0
    overshoot_count = 0
    for trial, samples in zip(trials, group_results):
        segment_groups = _split_ground_load_segments(samples, trial)
        segment_index = 0
        while segment_index < len(trial.segments_ms):
            target_speed = trial.segments_ms[segment_index][0]
            if (
                _segment_is_scored(target_speed, min_target_speed)
                and segment_index < len(segment_groups)
                and segment_groups[segment_index]
            ):
                segment_samples = segment_groups[segment_index]
                overshoot_sum += analyze_trial(segment_samples).overshoot
                overshoot_count += 1
            segment_index += 1

    if overshoot_count == 0:
        return 0.0

    return overshoot_sum / float(overshoot_count)


def _print_group_result(
    gains,
    score,
    group_results,
    trials=None,
    score_config=None,
    min_target_speed=DEFAULT_MIN_SCORE_TARGET_SPEED,
):
    if trials is None:
        trials = build_ground_load_trials()

    summary_parts = []
    for trial, samples in zip(trials, group_results):
        summary_parts.append(
            summarize_ground_load_trial(
                samples,
                trial,
                score_config=score_config,
                min_target_speed=min_target_speed,
            )
        )
    print(
        "group {0} score={1:.4f}".format(
            describe_gains(gains),
            score,
        )
    )
    for part in summary_parts:
        print("  {0}".format(part))


def _evaluate_ground_load_stage(
    client,
    candidates,
    args,
    trials,
    wait_for_operator,
    return_trial,
    score_config,
):
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
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
        )
        _print_group_result(
            gains,
            score,
            group_results,
            trials=trials,
            score_config=score_config,
            min_target_speed=args.score_min_target_speed,
        )
        score_rows.append((gains, score))
        if score < best_score:
            best_score = score
            best_gains = gains
            best_results = group_results

    return best_gains, best_score, best_results, score_rows


def run_ground_load_autotune(client, args):
    all_rows = []
    trials = build_ground_load_trials()
    score_config = build_score_config(args)
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
        score_config,
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
        score_config,
    )
    all_rows.extend(stage_rows)

    if _average_group_overshoot(
        best_results,
        trials=trials,
        min_target_speed=args.score_min_target_speed,
    ) > 1.0:
        stage3_candidates = [PidGains(best.kp, best.ki, kd) for kd in (0.2, 0.5)]
        kd_best, kd_score, kd_results, stage_rows = _evaluate_ground_load_stage(
            client,
            stage3_candidates,
            args,
            trials,
            wait_for_operator,
            return_trial,
            score_config,
        )
        all_rows.extend(stage_rows)
        if kd_score < best_score:
            best = kd_best
            best_score = kd_score
            best_results = kd_results

    client.send_command("AT_FUYA={0}".format(int(args.fuya_pwm)))
    client.send_command("AT_COOLDOWN_MS={0}".format(int(args.ground_cooldown_ms)))
    apply_speed_gains(client, best)
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


def run_ground_dual_autotune(client, args):
    return run_ground_load_autotune(client, args)

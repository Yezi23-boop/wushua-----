import copy
import json
import pathlib
import time

from . import air_dual, agent_session, common, ground_dual, llm_decision_contract, pwm_identify, pwm_map


DEFAULT_BATCH_SIZE = 10
DEFAULT_TRACE_FILENAME = "agent_decision_trace.jsonl"
DEFAULT_AIR_ACTIONS = ["continue_air", "enter_ground", "stop_air"]
DEFAULT_GROUND_ACTIONS = ["continue_ground", "save", "stop_without_save"]
DEFAULT_AIR_FAILURE_ACTIONS = ["continue_air", "stop_air"]
DEFAULT_GROUND_FAILURE_ACTIONS = ["continue_ground", "stop_without_save"]
DEFAULT_RUNTIME_GUARDRAILS = {
    "absolute_pid_limits": {
        "kp_min": 0.0,
        "kp_max": 500.0,
        "ki_min": 0.0,
        "ki_max": 200.0,
        "kd_min": 0.0,
        "kd_max": 50.0,
    },
    "precision": {"kp_decimals": 2, "ki_decimals": 2, "kd_decimals": 2},
    "allow_large_jump": True,
    "single_candidate_only": True,
    "worker_timeout_seconds": 20,
    "decision_retry_budget": 2,
    "timeout_retry_budget": 1,
    "significant_regression_abs": 5.0,
    "significant_regression_ratio": 0.03,
    "plateau_abs_threshold": 2.0,
    "plateau_rounds": 3,
    "abnormal_persistent_rounds": 2,
    "waveform_severe_flag_threshold": 2,
}


def _default_pair_from_args(args):
    return common.WheelPidGains(
        common.PidGains(
            float(getattr(args, "initial_kp", 0.0)),
            float(getattr(args, "initial_ki", 0.0)),
            float(getattr(args, "initial_kd", 0.0)),
        ),
        common.PidGains(
            float(getattr(args, "initial_kp", 0.0)),
            float(getattr(args, "initial_ki", 0.0)),
            float(getattr(args, "initial_kd", 0.0)),
        ),
    )


def _copy_pid_pair(pid_pair, fallback_pair=None):
    fallback = common.wheel_pid_gains_from_dict(fallback_pair)
    gains = common.wheel_pid_gains_from_dict(pid_pair, fallback)
    if gains is None:
        gains = fallback
    if gains is None:
        gains = common.WheelPidGains(common.ZERO_PID_GAINS, common.ZERO_PID_GAINS)
    return common.wheel_pid_gains_to_dict(gains)


def _mutate_pid_pair(pid_pair, field_name, delta, wheel_name="shared"):
    gains = common.wheel_pid_gains_from_dict(pid_pair)
    if gains is None:
        return _copy_pid_pair(pid_pair)

    left = gains.left
    right = gains.right

    if field_name not in ("kp", "ki", "kd"):
        raise ValueError("Unsupported field_name: {0}".format(field_name))

    def _adjust(gains_value):
        if field_name == "kp":
            return common.PidGains(max(0.0, gains_value.kp + delta), gains_value.ki, gains_value.kd)
        if field_name == "ki":
            return common.PidGains(gains_value.kp, max(0.0, gains_value.ki + delta), gains_value.kd)
        return common.PidGains(gains_value.kp, gains_value.ki, max(0.0, gains_value.kd + delta))

    if wheel_name in ("shared", "left"):
        left = _adjust(left)
    if wheel_name in ("shared", "right"):
        right = _adjust(right)

    return common.wheel_pid_gains_to_dict(common.WheelPidGains(left, right))


def _phase_from_round(round_index):
    round_value = int(round_index or 0)
    if round_value <= 3:
        return common.AGENT_SEARCH_PHASES[0]
    if round_value <= 7:
        return common.AGENT_SEARCH_PHASES[1]
    return common.AGENT_SEARCH_PHASES[2]


def _coerce_score(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _parse_request_id(request_id):
    request_text = str(request_id or "")
    if "_r" not in request_text:
        return ("", 0)
    prefix, round_text = request_text.rsplit("_r", 1)
    try:
        return (prefix, int(round_text))
    except (TypeError, ValueError):
        return ("", 0)


def _load_profile(profile_path):
    profile = common.load_tuning_profile(str(profile_path), required=False)
    profile = agent_session.ensure_agent_profile_defaults(profile)
    agent_tuning = profile.get("agent_tuning", {})
    if not agent_tuning.get("session_id"):
        agent_tuning["session_id"] = time.strftime("%Y%m%d_%H%M%S")
    return profile


def _save_profile(profile, profile_path):
    common.save_tuning_profile(profile, str(profile_path))
    return profile


def _trace_path_for_profile(profile_path):
    return profile_path.parent / DEFAULT_TRACE_FILENAME


def _batch_id_for_stage(profile, stage_name):
    stage_block = profile.get(stage_name, {})
    next_round = int(stage_block.get("batch_round", 0) or 0) + 1
    prefix = "air"
    if stage_name == "ground_dual":
        prefix = "ground"
    return "{0}_{1:04d}".format(prefix, next_round)


def _profile_needs_pwm_map(profile):
    shared_targets = profile.get("shared_targets")
    pwm_map_state = profile.get("pwm_map")
    return not isinstance(shared_targets, dict) or not isinstance(pwm_map_state, dict) or not pwm_map_state


def _profile_needs_pwm_identify(profile):
    seed_pair = common.wheel_pid_gains_from_dict(profile.get("pwm_identify", {}).get("seed_pi"))
    return seed_pair is None


def _copy_args(args):
    return copy.copy(args)


def _default_stage_runner(client, run_args, mode_name, _context):
    if mode_name == common.MODE_PWM_MAP:
        return pwm_map.run_pwm_map(client, run_args)
    if mode_name == common.MODE_PWM_IDENTIFY:
        return pwm_identify.run_pwm_identify(client, run_args)
    if mode_name == common.MODE_AIR_DUAL_STEP:
        return air_dual.run_air_dual_step(client, run_args)
    if mode_name == common.MODE_GROUND_DUAL_STEP:
        return ground_dual.run_ground_dual_step(client, run_args)
    raise RuntimeError("Unsupported agent stage runner mode: {0}".format(mode_name))


def default_decision_policy(stage_name, round_index, _profile, stage_block, last_result, round_history):
    active_batch = stage_block.get("active_batch", {})
    start_pid = None
    current_best = None
    if isinstance(active_batch, dict):
        start_pid = active_batch.get("start_pid")
        current_best = active_batch.get("current_best_pid")
    phase = _phase_from_round(round_index)
    best_pid = current_best or start_pid or stage_block.get("best_pid")
    if best_pid is None:
        best_pid = _copy_pid_pair(None)

    if int(round_index) == 1:
        return {
            "candidate_pid": _copy_pid_pair(best_pid),
            "decision_type": "keep_and_verify",
            "base_pid_source": "batch_start",
            "target_param": "kp",
            "delta": 0.0,
            "phase": phase,
            "reason": "start from current batch baseline",
        }

    last_score = _coerce_score(getattr(last_result, "get", lambda *_args, **_kwargs: None)("combined_score"))
    best_score = _coerce_score(getattr(active_batch, "get", lambda *_args, **_kwargs: None)("current_best_score"))
    overshoot_flag = int(getattr(last_result, "get", lambda *_args, **_kwargs: 0)("overshoot_flag") or 0)
    persistent_overshoot = int(getattr(last_result, "get", lambda *_args, **_kwargs: 0)("persistent_overshoot_flag") or 0)
    speed_drop_flag = int(getattr(last_result, "get", lambda *_args, **_kwargs: 0)("speed_drop_flag") or 0)
    base_pid = best_pid
    base_source = "current_batch_best"

    if last_result and last_score is not None and best_score is not None and last_score <= best_score:
        base_pid = last_result.get("candidate_pid", best_pid)
        base_source = "last_round"

    if stage_name == "ground_dual":
        kp_step = 4.0
        ki_step = 1.5
        kd_step = 0.2
    else:
        kp_step = 6.0
        ki_step = 2.0
        kd_step = 0.1

    if phase == "explore":
        if overshoot_flag:
            return {
                "candidate_pid": _mutate_pid_pair(base_pid, "kp", -kp_step),
                "decision_type": "rollback_overshoot",
                "base_pid_source": base_source,
                "target_param": "kp",
                "delta": -kp_step,
                "phase": phase,
                "reason": "overshoot detected, reduce kp",
            }
        return {
            "candidate_pid": _mutate_pid_pair(base_pid, "kp", kp_step),
            "decision_type": "mutate_kp",
            "base_pid_source": base_source,
            "target_param": "kp",
            "delta": kp_step,
            "phase": phase,
            "reason": "explore more response with higher kp",
        }

    recent_scores = []
    for row in round_history[-3:]:
        score_value = _coerce_score(row.get("combined_score"))
        if score_value is not None:
            recent_scores.append(score_value)
    plateau = len(recent_scores) >= 2 and abs(recent_scores[0] - recent_scores[-1]) < 0.5

    if persistent_overshoot and phase == "confirm":
        return {
            "candidate_pid": _mutate_pid_pair(base_pid, "kd", kd_step),
            "decision_type": "mutate_kd",
            "base_pid_source": base_source,
            "target_param": "kd",
            "delta": kd_step,
            "phase": phase,
            "reason": "persistent overshoot keeps showing up",
        }
    if overshoot_flag:
        return {
            "candidate_pid": _mutate_pid_pair(base_pid, "kp", -0.5 * kp_step),
            "decision_type": "mutate_kp",
            "base_pid_source": base_source,
            "target_param": "kp",
            "delta": -0.5 * kp_step,
            "phase": phase,
            "reason": "shrink kp after overshoot",
        }
    if speed_drop_flag or plateau or phase == "confirm":
        return {
            "candidate_pid": _mutate_pid_pair(base_pid, "ki", ki_step),
            "decision_type": "mutate_ki",
            "base_pid_source": base_source,
            "target_param": "ki",
            "delta": ki_step,
            "phase": phase,
            "reason": "settling near best, adjust ki for steady-state",
        }
    return {
        "candidate_pid": _mutate_pid_pair(base_pid, "kp", 0.5 * kp_step),
        "decision_type": "mutate_kp",
        "base_pid_source": base_source,
        "target_param": "kp",
        "delta": 0.5 * kp_step,
        "phase": phase,
        "reason": "continue local kp refinement",
    }


def _write_pid_json(path, pid_pair):
    return common.write_json_file(path, _copy_pid_pair(pid_pair))


def _load_round_history(profile_path, stage_name, batch_id, current_round_index):
    history = []
    committed_request_id = agent_session.resume_session_state(_load_profile(profile_path)).get("last_committed_request_id", "")
    committed_batch_id, committed_round_index = _parse_request_id(committed_request_id)
    round_index = 1
    while round_index <= int(current_round_index or 0):
        result_path = agent_session.build_round_result_path(profile_path.parent, stage_name, batch_id, round_index)
        if pathlib.Path(result_path).exists():
            result_payload = common.load_json_dict(str(result_path))
            result_request_id = result_payload.get("request_id", "")
            result_batch_id, result_round_index = _parse_request_id(result_request_id)
            if (
                committed_request_id
                and result_request_id
                and result_batch_id == committed_batch_id
                and result_round_index > committed_round_index
            ):
                round_index += 1
                continue
            history.append(result_payload)
        round_index += 1
    return history


def _summarize_batch(stage_name, batch_id, start_pid, round_history):
    best_result = None
    batch_summary = {
        "batch_id": batch_id,
        "evaluated_groups": len(round_history),
        "start_pid": _copy_pid_pair(start_pid),
        "best_pid": _copy_pid_pair(start_pid),
        "next_start_pid": _copy_pid_pair(start_pid),
        "combined_score": None,
        "a_score": None,
        "b_score": None,
        "best_combined_score": None,
        "best_a_score": None,
        "best_b_score": None,
        "band_scores": common.normalize_band_scores(None),
        "stage_reached": "",
    }

    for result in round_history:
        result_score = _coerce_score(result.get("combined_score"))
        best_score = _coerce_score(getattr(best_result, "get", lambda *_args, **_kwargs: None)("combined_score"))
        if best_result is None or (result_score is not None and (best_score is None or result_score < best_score)):
            best_result = result

    if best_result is None:
        return batch_summary

    batch_summary["best_pid"] = _copy_pid_pair(best_result.get("candidate_pid"))
    batch_summary["next_start_pid"] = _copy_pid_pair(best_result.get("candidate_pid"))
    batch_summary["combined_score"] = _coerce_score(best_result.get("combined_score"))
    batch_summary["a_score"] = _coerce_score(best_result.get("a_score"))
    batch_summary["b_score"] = _coerce_score(best_result.get("b_score"))
    batch_summary["best_combined_score"] = _coerce_score(best_result.get("combined_score"))
    batch_summary["best_a_score"] = _coerce_score(best_result.get("a_score"))
    batch_summary["best_b_score"] = _coerce_score(best_result.get("b_score"))
    batch_summary["band_scores"] = common.normalize_band_scores(best_result.get("band_scores"))
    batch_summary["stage_reached"] = best_result.get("stage_reached", "")
    batch_summary["waveform_path"] = best_result.get("waveform_path", "")
    batch_summary["result_path"] = best_result.get("result_path", "")
    batch_summary["stage_name"] = stage_name
    return batch_summary


def _recommend_batch_action(profile, stage_name, batch_summary, round_history):
    previous_summary = profile.get(stage_name, {}).get("last_summary")
    previous_score = _coerce_score(getattr(previous_summary, "get", lambda *_args, **_kwargs: None)("combined_score"))
    best_score = _coerce_score(batch_summary.get("combined_score"))
    recent_scores = []
    for row in round_history[-3:]:
        score_value = _coerce_score(row.get("combined_score"))
        if score_value is not None:
            recent_scores.append(score_value)

    plateau = len(recent_scores) >= 2 and abs(recent_scores[0] - recent_scores[-1]) < 0.5
    improved_vs_previous = False
    if previous_score is not None and best_score is not None:
        improved_vs_previous = best_score < previous_score - 0.5

    if stage_name == "air_dual":
        if improved_vs_previous or (previous_score is None and not plateau):
            return ("continue_air", DEFAULT_AIR_ACTIONS, "recent rounds still improved, keep refining in air stage")
        return ("enter_ground", DEFAULT_AIR_ACTIONS, "air-stage gains have plateaued, move to ground verification")

    if improved_vs_previous and not plateau:
        return ("continue_ground", DEFAULT_GROUND_ACTIONS, "loaded stage still improves, run another ground batch")
    return ("save", DEFAULT_GROUND_ACTIONS, "ground-stage batch looks stable enough to save explicitly")


def _resolve_stage_start_pair(profile, stage_name, args):
    default_pair = _default_pair_from_args(args)
    if stage_name == "ground_dual":
        start_pair = ground_dual.resolve_ground_dual_step_start_pair(profile, default_pair)
        baseline_pair = ground_dual.resolve_ground_dual_step_baseline_pair(profile, start_pair)
    else:
        start_pair = air_dual.resolve_air_dual_step_start_pair(profile, default_pair)
        baseline_pair = air_dual.resolve_air_dual_step_baseline_pair(profile, start_pair)
    return (common.wheel_pid_gains_to_dict(start_pair), common.wheel_pid_gains_to_dict(baseline_pair))


def _build_round_paths(profile_path, stage_name, batch_id, round_index):
    root = profile_path.parent
    round_result = agent_session.build_round_result_path(root, stage_name, batch_id, round_index)
    waveform_path = agent_session.build_waveform_path(root, stage_name, batch_id, round_index)
    candidate_path = root / "agent_candidates" / stage_name / "{0}_r{1:02d}_candidate.json".format(batch_id, int(round_index))
    baseline_path = root / "agent_candidates" / stage_name / "{0}_r{1:02d}_baseline.json".format(batch_id, int(round_index))
    return {
        "result_path": round_result,
        "waveform_path": waveform_path,
        "candidate_path": candidate_path,
        "baseline_path": baseline_path,
    }


def _run_prerequisites(client, args, profile_path, profile, stage_runner):
    auto_completed = list(profile.get("agent_tuning", {}).get("auto_completed_stages", []))

    if _profile_needs_pwm_map(profile):
        run_args = _copy_args(args)
        run_args.mode = common.MODE_PWM_MAP
        stage_runner(client, run_args, common.MODE_PWM_MAP, {"profile_path": profile_path})
        profile = _load_profile(profile_path)
        if common.MODE_PWM_MAP not in auto_completed:
            auto_completed.append(common.MODE_PWM_MAP)
        profile["agent_tuning"]["auto_completed_stages"] = auto_completed
        _save_profile(profile, profile_path)

    if _profile_needs_pwm_identify(profile):
        run_args = _copy_args(args)
        run_args.mode = common.MODE_PWM_IDENTIFY
        stage_runner(client, run_args, common.MODE_PWM_IDENTIFY, {"profile_path": profile_path})
        profile = _load_profile(profile_path)
        if common.MODE_PWM_IDENTIFY not in auto_completed:
            auto_completed.append(common.MODE_PWM_IDENTIFY)
        profile["agent_tuning"]["auto_completed_stages"] = auto_completed
        _save_profile(profile, profile_path)

    return profile


def _ensure_trace_path(profile, profile_path):
    trace_path = _trace_path_for_profile(profile_path)
    profile["agent_tuning"]["last_decision_trace_path"] = trace_path.as_posix()
    return trace_path


def _ensure_request_state(profile):
    agent_tuning = profile.get("agent_tuning", {})
    if "pending_decision_request" not in agent_tuning:
        agent_tuning["pending_decision_request"] = None
    elif agent_tuning.get("pending_decision_request") is not None and not isinstance(agent_tuning.get("pending_decision_request"), dict):
        raise ValueError("agent_tuning.pending_decision_request must be an object or null")
    if "consumed_request_ids" not in agent_tuning:
        agent_tuning["consumed_request_ids"] = []
    elif not isinstance(agent_tuning.get("consumed_request_ids"), list):
        raise ValueError("agent_tuning.consumed_request_ids must be an array")
    else:
        for request_id in agent_tuning.get("consumed_request_ids", []):
            if not isinstance(request_id, str):
                raise ValueError("agent_tuning.consumed_request_ids[] must be a string")
    return agent_tuning


def _copy_runtime_guardrails():
    return copy.deepcopy(DEFAULT_RUNTIME_GUARDRAILS)


def _last_batch_best_pid(stage_block):
    last_batch_best = stage_block.get("last_batch_best")
    if isinstance(last_batch_best, dict):
        return last_batch_best.get("best_pid")
    return None


def _dominant_issue_from_result(result):
    if int(result.get("overshoot_flag", 0) or 0):
        return "overshoot"
    if int(result.get("speed_drop_flag", 0) or 0):
        return "slow_response"
    if float(result.get("pwm_saturation_ratio", 0.0) or 0.0) >= 0.95:
        return "saturation_limited"
    return None


def _waveform_summary_from_result(result):
    digest = result.get("waveform_digest")
    if not isinstance(digest, dict):
        return {
            "available": False,
            "waveform_digest": {
                "tail_jitter": None,
                "peak_windows": None,
                "settling_tail_shape": None,
                "stop_tail_residual": None,
                "oscillation_hint": None,
            },
            "waveform_flags": {
                "looks_noisy": False,
                "looks_underdamped": False,
                "looks_saturated": False,
                "looks_measurement_limited": False,
            },
        }

    peak_windows = digest.get("peak_windows")
    peak_window_count = None
    if isinstance(peak_windows, list):
        peak_window_count = float(len(peak_windows))

    pwm_ratio = float(result.get("pwm_saturation_ratio", 0.0) or 0.0)
    return {
        "available": True,
        "waveform_digest": {
            "tail_jitter": _coerce_score(digest.get("tail_jitter")),
            "peak_windows": peak_window_count,
            "settling_tail_shape": None,
            "stop_tail_residual": None,
            "oscillation_hint": None,
        },
        "waveform_flags": {
            "looks_noisy": bool((digest.get("tail_jitter") or 0.0) > 0.5),
            "looks_underdamped": bool(result.get("persistent_overshoot_flag", 0)),
            "looks_saturated": pwm_ratio >= 0.95,
            "looks_measurement_limited": False,
        },
    }


def _recent_round_row(result, previous_result, best_score, plateau_abs_threshold):
    combined_score = _coerce_score(result.get("combined_score"))
    left_score = _coerce_score(result.get("left_score"))
    right_score = _coerce_score(result.get("right_score"))
    if left_score is None:
        left_score = combined_score
    if right_score is None:
        right_score = combined_score

    delta_vs_previous = None
    score_trend = None
    plateau_detected = False
    if previous_result is not None:
        previous_score = _coerce_score(previous_result.get("combined_score"))
        if combined_score is not None and previous_score is not None:
            delta_vs_previous = combined_score - previous_score
            if delta_vs_previous < -plateau_abs_threshold:
                score_trend = "improving"
            elif delta_vs_previous > plateau_abs_threshold:
                score_trend = "worsening"
            else:
                score_trend = "flat"
                plateau_detected = True

    delta_vs_batch_best = None
    if combined_score is not None and best_score is not None:
        delta_vs_batch_best = combined_score - best_score

    pwm_ratio = float(result.get("pwm_saturation_ratio", 0.0) or 0.0)
    return {
        "round_index": int(result.get("round_index", 0) or 0),
        "candidate_pid": _copy_pid_pair(result.get("candidate_pid")),
        "combined_score": combined_score,
        "a_score": _coerce_score(result.get("a_score")),
        "b_score": _coerce_score(result.get("b_score")),
        "left_score": left_score,
        "right_score": right_score,
        "band_scores": common.normalize_band_scores(result.get("band_scores")),
        "overshoot_flag": bool(result.get("overshoot_flag", 0)),
        "persistent_overshoot_flag": bool(result.get("persistent_overshoot_flag", 0)),
        "speed_drop_flag": bool(result.get("speed_drop_flag", 0)),
        "stop_clean_flag": bool(result.get("stop_clean_flag", 0)),
        "pwm_saturation_ratio": pwm_ratio,
        "current_limit_or_headroom_flag": pwm_ratio >= 0.95,
        "dominant_issue": _dominant_issue_from_result(result),
        "delta_vs_previous_combined": delta_vs_previous,
        "delta_vs_batch_best_combined": delta_vs_batch_best,
        "left_right_gap": abs(float(left_score or 0.0) - float(right_score or 0.0)),
        "score_trend": score_trend,
        "plateau_detected": plateau_detected,
        "decision_hints": [],
        "advisory_only": True,
        "result_path": result.get("result_path", ""),
        "waveform_path": result.get("waveform_path"),
    }


def _build_decision_context(profile, profile_path, stage_name, batch_id, round_index, batch_size):
    stage_block = profile.get(stage_name, {})
    active_batch = stage_block.get("active_batch", {})
    session_state = agent_session.resume_session_state(profile)
    recovery_state = session_state.get("recovery_state", {})
    if not isinstance(active_batch, dict):
        active_batch = {}
    last_summary = stage_block.get("last_summary")
    if not isinstance(last_summary, dict):
        last_summary = {}

    round_history = _load_round_history(profile_path, stage_name, batch_id, int(round_index) - 1)
    runtime_guardrails = _copy_runtime_guardrails()
    best_score = None
    if isinstance(active_batch, dict):
        best_score = _coerce_score(active_batch.get("current_best_score"))
    if best_score is None:
        best_score = _coerce_score(last_summary.get("combined_score"))

    recent_rounds = []
    previous_result = None
    for result in round_history[-5:]:
        recent_rounds.append(
            _recent_round_row(
                result,
                previous_result,
                best_score,
                runtime_guardrails["plateau_abs_threshold"],
            )
        )
        previous_result = result

    waveform_summary = _waveform_summary_from_result(round_history[-1]) if round_history else _waveform_summary_from_result({})
    context = {
        "schema_version": llm_decision_contract.SCHEMA_VERSION,
        "stage_context": {
            "stage_name": stage_name,
            "batch_id": batch_id,
            "round_index": int(round_index),
            "batch_size": int(batch_size),
            "current_status": "running",
            "score_direction": "lower_is_better",
            "waveform_role": "secondary_evidence",
            "disallowed_actions": list(llm_decision_contract.DISALLOWED_ACTION_VALUES),
        },
        "pid_anchors": {
            "batch_start_pid": _copy_pid_pair(active_batch.get("start_pid")),
            "baseline_pid": _copy_pid_pair(stage_block.get("baseline_pid"), active_batch.get("start_pid")),
            "current_batch_best_pid": _copy_pid_pair(active_batch.get("current_best_pid")),
            "current_batch_best_score": _coerce_score(active_batch.get("current_best_score")),
            "historical_stage_best_pid": _copy_pid_pair(stage_block.get("best_pid")),
            "historical_stage_best_score": _coerce_score(last_summary.get("combined_score")),
            "last_batch_best": _copy_pid_pair(_last_batch_best_pid(stage_block)),
            "seed_pi": _copy_pid_pair(profile.get("pwm_identify", {}).get("seed_pi")),
        },
        "recent_rounds": recent_rounds,
        "waveform_summary": waveform_summary,
        "runtime_guardrails": runtime_guardrails,
        "recovery_state": {
            "must_recover": bool(recovery_state.get("must_recover", False)),
            "high_risk_round_seen": bool(recovery_state.get("high_risk_round_seen", False)),
            "recovery_reason": recovery_state.get("recovery_reason"),
        },
        "advisory_hints": {
            "search_phase": _phase_from_round(round_index),
            "advisory_only": True,
        },
    }
    return llm_decision_contract.validate_decision_context(context)


def _store_pending_request(profile, request_payload):
    agent_tuning = _ensure_request_state(profile)
    agent_tuning["pending_decision_request"] = json.loads(json.dumps(request_payload, ensure_ascii=False))
    agent_tuning["workflow_status"] = "decision_required"
    return agent_tuning["pending_decision_request"]


def _pending_request(profile):
    agent_tuning = _ensure_request_state(profile)
    pending_request = agent_tuning.get("pending_decision_request")
    if isinstance(pending_request, dict):
        return pending_request
    return None


def _clear_pending_request(profile):
    agent_tuning = _ensure_request_state(profile)
    agent_tuning["pending_decision_request"] = None
    return profile


def _mark_request_consumed(profile, request_id):
    agent_tuning = _ensure_request_state(profile)
    consumed_ids = agent_tuning.get("consumed_request_ids", [])
    if request_id not in consumed_ids:
        consumed_ids.append(request_id)
    agent_tuning["consumed_request_ids"] = consumed_ids
    return consumed_ids


def _build_decision_request(profile, profile_path, stage_name, batch_id, round_index, batch_size):
    payload = {
        "request_id": "{0}_r{1:02d}".format(batch_id, int(round_index)),
        "context": _build_decision_context(profile, profile_path, stage_name, batch_id, round_index, batch_size),
        "retry_counters": {
            "decision_errors_used": 0,
            "timeouts_used": 0,
        },
    }
    return llm_decision_contract.validate_decision_request_payload(payload, batch_id, round_index)


def _ensure_batch_for_stage(profile, profile_path, stage_name, args, batch_size):
    state = agent_session.resume_session_state(profile)
    stage_block = profile.get(stage_name, {})
    active_batch = stage_block.get("active_batch")
    batch_id = state.get("current_batch_id", "")
    current_round_index = int(state.get("current_round_index", 0) or 0)

    if (
        state.get("workflow_stage") == stage_name
        and isinstance(active_batch, dict)
        and active_batch.get("batch_id") == batch_id
        and current_round_index < int(batch_size)
    ):
        if stage_block.get("baseline_pid") is None:
            stage_block["baseline_pid"] = _copy_pid_pair(active_batch.get("start_pid"))
        return (profile, batch_id, current_round_index)

    start_pid, baseline_pid = _resolve_stage_start_pair(profile, stage_name, args)
    batch_id = _batch_id_for_stage(profile, stage_name)
    profile = agent_session.start_batch(profile, stage_name, batch_id, start_pid)
    stage_block = profile.get(stage_name, {})
    stage_block["baseline_pid"] = _copy_pid_pair(baseline_pid)
    profile["agent_tuning"]["workflow_stage"] = stage_name
    profile["agent_tuning"]["workflow_status"] = "running"
    return (profile, batch_id, 0)


def _request_failure_boundary(profile, stage_name, batch_id, reason):
    recommended_action = "stop_air"
    allowed_actions = DEFAULT_AIR_FAILURE_ACTIONS
    if stage_name == "ground_dual":
        recommended_action = "stop_without_save"
        allowed_actions = DEFAULT_GROUND_FAILURE_ACTIONS
    profile = _clear_pending_request(profile)
    profile = agent_session.set_pending_user_action(
        profile,
        stage_name,
        batch_id,
        recommended_action,
        allowed_actions,
        reason,
    )
    profile["agent_tuning"]["workflow_status"] = "waiting_user"
    return profile


def _is_failure_boundary_reason(reason):
    reason_text = str(reason or "")
    return (
        reason_text.startswith("agent ")
        or reason_text.startswith("post-execution persistence failed")
        or reason_text.startswith("worker circuit break")
    )


def _decision_required_result(profile, profile_path, decision_request=None):
    if decision_request is None:
        decision_request = _pending_request(profile)
    profile["agent_tuning"]["workflow_status"] = "decision_required"
    return _build_workflow_result(profile, profile_path, decision_request=decision_request)


def _restore_profile_state(profile, profile_path):
    state = agent_session.resume_session_state(profile)
    stage_name = state.get("workflow_stage") or "air_dual"
    batch_id = state.get("current_batch_id", "")
    current_round_index = int(state.get("current_round_index", 0) or 0)
    restored_round_index = current_round_index
    committed_request_id = state.get("last_committed_request_id", "")
    failure_trace = state.get("failure_trace")
    committed_batch_id, committed_round_index = _parse_request_id(committed_request_id)

    if not batch_id or stage_name not in ("air_dual", "ground_dual"):
        return profile
    if committed_batch_id != batch_id or committed_round_index >= current_round_index:
        if isinstance(failure_trace, dict) and failure_trace.get("request_consumed"):
            failure_batch_id, failure_round_index = _parse_request_id(failure_trace.get("request_id"))
            if failure_batch_id == batch_id and failure_round_index > current_round_index:
                restored_round_index = failure_round_index
            else:
                return profile
        else:
            return profile
    else:
        restored_round_index = committed_round_index

    stage_block = profile.get(stage_name, {})
    active_batch = stage_block.get("active_batch")
    if not isinstance(active_batch, dict) or active_batch.get("batch_id") != batch_id:
        return profile

    history = _load_round_history(profile_path, stage_name, batch_id, committed_round_index)
    active_batch["rounds_completed"] = committed_round_index
    active_batch["search_phase"] = _phase_from_round(committed_round_index or 1)
    if history:
        last_result = history[-1]
        active_batch["last_round_pid"] = _copy_pid_pair(last_result.get("candidate_pid"))
        active_batch["last_round_score"] = _coerce_score(last_result.get("combined_score"))
        active_batch["last_round_result_path"] = pathlib.Path(
            agent_session.build_round_result_path(profile_path.parent, stage_name, batch_id, committed_round_index)
        ).as_posix()
        best_result = history[0]
        for row in history[1:]:
            if _coerce_score(row.get("combined_score")) < _coerce_score(best_result.get("combined_score")):
                best_result = row
        active_batch["current_best_pid"] = _copy_pid_pair(best_result.get("candidate_pid"))
        active_batch["current_best_score"] = _coerce_score(best_result.get("combined_score"))
    else:
        active_batch["last_round_pid"] = None
        active_batch["last_round_score"] = None
        active_batch["last_round_result_path"] = None
        active_batch["current_best_pid"] = _copy_pid_pair(active_batch.get("start_pid"))
        active_batch["current_best_score"] = None

    profile["agent_tuning"]["current_round_index"] = restored_round_index
    return profile


def _retry_pending_request(profile, profile_path, request_payload, counter_key):
    stage_context = request_payload["context"]["stage_context"]
    runtime_guardrails = request_payload["context"]["runtime_guardrails"]
    request_payload["retry_counters"][counter_key] = int(request_payload["retry_counters"].get(counter_key, 0) or 0) + 1
    budget_key = "decision_retry_budget"
    if counter_key == "timeouts_used":
        budget_key = "timeout_retry_budget"
    budget = int(runtime_guardrails.get(budget_key, 0) or 0)
    if request_payload["retry_counters"][counter_key] <= budget:
        request_payload = llm_decision_contract.validate_decision_request_payload(
            request_payload,
            stage_context["batch_id"],
            stage_context["round_index"],
        )
        _store_pending_request(profile, request_payload)
        return _decision_required_result(profile, profile_path, decision_request=request_payload)

    failure_type = "decision_error_exhausted"
    if counter_key == "timeouts_used":
        failure_type = "decision_timeout_exhausted"
    reason = "agent {0} budget exhausted for {1}".format(counter_key, request_payload["request_id"])
    profile = agent_session.record_failure_trace(
        profile,
        failure_type,
        request_id=request_payload["request_id"],
        stage_name=stage_context["stage_name"],
        batch_id=stage_context["batch_id"],
        round_index=stage_context["round_index"],
        message=reason,
        request_consumed=False,
    )
    profile = _request_failure_boundary(profile, stage_context["stage_name"], stage_context["batch_id"], reason)
    return _build_workflow_result(profile, profile_path)


def _run_stage_batch(client, args, profile_path, profile, stage_name, stage_runner, decision_policy, batch_size):
    state = agent_session.resume_session_state(profile)
    stage_block = profile.get(stage_name, {})
    active_batch = stage_block.get("active_batch")
    batch_id = state.get("current_batch_id", "")
    current_round_index = int(state.get("current_round_index", 0) or 0)
    round_history = []
    start_pid = None
    baseline_pid = None

    if (
        state.get("workflow_status") == "running"
        and state.get("workflow_stage") == stage_name
        and isinstance(active_batch, dict)
        and active_batch.get("batch_id") == batch_id
        and current_round_index < int(batch_size)
    ):
        start_pid = _copy_pid_pair(active_batch.get("start_pid"))
        baseline_pid = _copy_pid_pair(stage_block.get("baseline_pid"), active_batch.get("start_pid"))
        round_history = _load_round_history(profile_path, stage_name, batch_id, current_round_index)
    else:
        start_pid, baseline_pid = _resolve_stage_start_pair(profile, stage_name, args)
        batch_id = _batch_id_for_stage(profile, stage_name)
        profile = agent_session.start_batch(profile, stage_name, batch_id, start_pid)
        profile["agent_tuning"]["workflow_stage"] = stage_name
        _save_profile(profile, profile_path)
        stage_block = profile.get(stage_name, {})
        current_round_index = 0

    trace_path = _ensure_trace_path(profile, profile_path)
    round_index = current_round_index + 1
    last_result = None
    if round_history:
        last_result = round_history[-1]

    while round_index <= int(batch_size):
        stage_block = profile.get(stage_name, {})
        decision = decision_policy(stage_name, round_index, profile, stage_block, last_result, round_history)
        candidate_pid = _copy_pid_pair(decision.get("candidate_pid"), start_pid)
        paths = _build_round_paths(profile_path, stage_name, batch_id, round_index)
        _write_pid_json(paths["candidate_path"], candidate_pid)
        _write_pid_json(paths["baseline_path"], baseline_pid)

        run_args = _copy_args(args)
        run_args.profile_path = str(profile_path)
        run_args.batch_id = batch_id
        run_args.round_index = int(round_index)
        run_args.candidate_json = str(paths["candidate_path"])
        run_args.baseline_json = str(paths["baseline_path"])
        run_args.result_json = str(paths["result_path"])
        run_args.waveform_path = str(paths["waveform_path"])
        if stage_name == "ground_dual":
            mode_name = common.MODE_GROUND_DUAL_STEP
        else:
            mode_name = common.MODE_AIR_DUAL_STEP
        run_args.mode = mode_name

        result = stage_runner(
            client,
            run_args,
            mode_name,
            {
                "stage_name": stage_name,
                "batch_id": batch_id,
                "round_index": int(round_index),
                "candidate_pid": candidate_pid,
                "baseline_pid": baseline_pid,
            },
        )

        profile = agent_session.record_round_result(
            profile,
            stage_name,
            batch_id,
            round_index,
            paths["result_path"],
            result,
        )
        trace_row = {
            "stage_name": stage_name,
            "batch_id": batch_id,
            "round_index": int(round_index),
            "candidate_pid": candidate_pid,
            "result_path": pathlib.Path(paths["result_path"]).as_posix(),
            "combined_score": _coerce_score(result.get("combined_score")),
            "decision_type": decision.get("decision_type", ""),
            "base_pid_source": decision.get("base_pid_source", ""),
            "target_param": decision.get("target_param", ""),
            "delta": decision.get("delta"),
            "phase": decision.get("phase", _phase_from_round(round_index)),
            "reason": decision.get("reason", ""),
        }
        agent_session.append_decision_trace(trace_path, trace_row)
        profile["agent_tuning"]["last_decision_trace_path"] = trace_path.as_posix()
        _save_profile(profile, profile_path)

        round_history.append(result)
        last_result = result
        round_index += 1

    batch_summary = _summarize_batch(stage_name, batch_id, start_pid, round_history)
    recommended_action, allowed_actions, reason = _recommend_batch_action(profile, stage_name, batch_summary, round_history)
    profile = agent_session.finish_batch_summary(
        profile,
        stage_name,
        batch_summary,
        recommended_action,
        allowed_actions,
        reason,
    )
    profile["agent_tuning"]["last_decision_trace_path"] = trace_path.as_posix()
    _save_profile(profile, profile_path)
    return (profile, batch_summary)


def _best_pid_from_stage(profile, stage_name):
    stage_block = profile.get(stage_name, {})
    last_batch_best = stage_block.get("last_batch_best", {})
    if isinstance(last_batch_best, dict) and last_batch_best.get("best_pid") is not None:
        return last_batch_best.get("best_pid")
    return stage_block.get("best_pid")


def _clear_waiting_state(profile):
    agent_tuning = profile.get("agent_tuning", {})
    agent_tuning["pending_user_action"] = None
    agent_tuning["current_batch_id"] = ""
    agent_tuning["current_round_index"] = 0
    return profile


def _apply_explicit_action(profile, explicit_action):
    state = agent_session.resume_session_state(profile)
    pending = state.get("pending_user_action")
    if not isinstance(pending, dict):
        raise RuntimeError("No pending user action to apply")

    allowed_actions = pending.get("allowed_actions", [])
    if explicit_action not in allowed_actions:
        raise RuntimeError("Unsupported action word: {0}".format(explicit_action))

    stage_name = pending.get("stage")
    pending_batch_id = pending.get("batch_id", "")
    pending_round_index = int(state.get("current_round_index", 0) or 0)
    reason = pending.get("reason", "")
    summary = profile.get(stage_name, {}).get("last_batch_summary", {})
    best_pid = _best_pid_from_stage(profile, stage_name)
    profile = _clear_waiting_state(profile)

    if explicit_action == "continue_air":
        if _is_failure_boundary_reason(reason):
            profile = agent_session.mark_manual_resume(profile, failure_trace=reason)
        profile["agent_tuning"]["workflow_stage"] = "air_dual"
        profile["agent_tuning"]["workflow_status"] = "running"
        profile["agent_tuning"]["current_batch_id"] = pending_batch_id
        profile["agent_tuning"]["current_round_index"] = pending_round_index
        return profile

    if explicit_action == "continue_ground":
        if _is_failure_boundary_reason(reason):
            profile = agent_session.mark_manual_resume(profile, failure_trace=reason)
        profile["agent_tuning"]["workflow_stage"] = "ground_dual"
        profile["agent_tuning"]["workflow_status"] = "running"
        profile["agent_tuning"]["current_batch_id"] = pending_batch_id
        profile["agent_tuning"]["current_round_index"] = pending_round_index
        return profile

    if explicit_action == "enter_ground":
        profile = agent_session.finalize_stage_best(profile, "air_dual", best_pid, summary, explicit_action)
        profile["agent_tuning"]["workflow_stage"] = "ground_dual"
        profile["agent_tuning"]["workflow_status"] = "running"
        profile["agent_tuning"]["pending_user_action"] = None
        return profile

    if explicit_action == "stop_air":
        return agent_session.finalize_stage_best(profile, "air_dual", best_pid, summary, explicit_action)

    if explicit_action == "save":
        return agent_session.finalize_stage_best(profile, "ground_dual", best_pid, summary, explicit_action)

    profile = agent_session.finalize_stage_best(profile, "ground_dual", best_pid, summary, explicit_action)
    profile["agent_tuning"]["workflow_status"] = "completed"
    profile["agent_tuning"]["pending_user_action"] = None
    return profile


def _build_workflow_result(profile, profile_path, batch_summary=None, decision_request=None):
    state = agent_session.resume_session_state(profile)
    if decision_request is None:
        decision_request = _pending_request(profile)
    result = {
        "profile_path": pathlib.Path(profile_path).as_posix(),
        "workflow_stage": state.get("workflow_stage", ""),
        "workflow_status": state.get("workflow_status", ""),
        "current_batch_id": state.get("current_batch_id", ""),
        "current_round_index": state.get("current_round_index", 0),
        "pending_user_action": state.get("pending_user_action"),
        "last_result_path": state.get("last_result_path", ""),
        "last_decision_trace_path": state.get("last_decision_trace_path", ""),
        "auto_completed_stages": list(profile.get("agent_tuning", {}).get("auto_completed_stages", [])),
    }
    if batch_summary is not None:
        result["batch_summary"] = batch_summary
    if decision_request is not None:
        result["decision_request"] = decision_request
    return result


def _execute_decision_round(client, args, profile_path, profile, request_payload, decision, raw_agent_output, stage_runner, batch_size):
    stage_context = request_payload["context"]["stage_context"]
    stage_name = stage_context["stage_name"]
    batch_id = stage_context["batch_id"]
    round_index = int(stage_context["round_index"])
    baseline_pid = request_payload["context"]["pid_anchors"]["baseline_pid"]
    start_pid = request_payload["context"]["pid_anchors"]["batch_start_pid"]
    trace_path = _ensure_trace_path(profile, profile_path)
    paths = _build_round_paths(profile_path, stage_name, batch_id, round_index)
    candidate_pid = _copy_pid_pair(decision.get("candidate_pid"), start_pid)
    baseline_pid = _copy_pid_pair(baseline_pid, start_pid)
    _write_pid_json(paths["candidate_path"], candidate_pid)
    _write_pid_json(paths["baseline_path"], baseline_pid)

    run_args = _copy_args(args)
    run_args.profile_path = str(profile_path)
    run_args.batch_id = batch_id
    run_args.round_index = round_index
    run_args.candidate_json = str(paths["candidate_path"])
    run_args.baseline_json = str(paths["baseline_path"])
    run_args.result_json = str(paths["result_path"])
    run_args.waveform_path = str(paths["waveform_path"])
    if stage_name == "ground_dual":
        mode_name = common.MODE_GROUND_DUAL_STEP
    else:
        mode_name = common.MODE_AIR_DUAL_STEP
    run_args.mode = mode_name

    result = stage_runner(
        client,
        run_args,
        mode_name,
        {
            "stage_name": stage_name,
            "batch_id": batch_id,
            "round_index": round_index,
            "candidate_pid": candidate_pid,
            "baseline_pid": baseline_pid,
        },
    )

    _mark_request_consumed(profile, request_payload["request_id"])
    _clear_pending_request(profile)
    _save_profile(profile, profile_path)

    try:
        profile = agent_session.persist_round_transaction(
            profile,
            stage_name,
            batch_id,
            round_index,
            request_payload["request_id"],
            paths["result_path"],
            result,
            trace_path,
            {
                "stage_name": stage_name,
                "batch_id": batch_id,
                "round_index": round_index,
                "request_id": request_payload["request_id"],
                "candidate_pid": candidate_pid,
                "combined_score": _coerce_score(result.get("combined_score")),
                "decision_mode": decision.get("decision_mode", ""),
                "base_reference": decision.get("base_reference", ""),
                "risk_level": decision.get("risk_level", ""),
                "reason": decision.get("primary_reason", ""),
                "result_path": pathlib.Path(paths["result_path"]).as_posix(),
                "raw_agent_output": raw_agent_output,
            },
        )
        profile["agent_tuning"]["last_decision_trace_path"] = trace_path.as_posix()
    except Exception:
        profile = agent_session.record_failure_trace(
            profile,
            "worker_circuit_break",
            request_id=request_payload["request_id"],
            stage_name=stage_name,
            batch_id=batch_id,
            round_index=round_index,
            message="post-execution persistence failed for {0}".format(request_payload["request_id"]),
            request_consumed=True,
        )
        profile = _request_failure_boundary(
            profile,
            stage_name,
            batch_id,
            "post-execution persistence failed for {0}".format(request_payload["request_id"]),
        )
        _save_profile(profile, profile_path)
        raise

    if round_index >= int(batch_size):
        round_history = _load_round_history(profile_path, stage_name, batch_id, round_index)
        batch_summary = _summarize_batch(stage_name, batch_id, start_pid, round_history)
        recommended_action, allowed_actions, reason = _recommend_batch_action(profile, stage_name, batch_summary, round_history)
        profile = agent_session.finish_batch_summary(
            profile,
            stage_name,
            batch_summary,
            recommended_action,
            allowed_actions,
            reason,
        )
        profile["agent_tuning"]["last_decision_trace_path"] = trace_path.as_posix()
        _save_profile(profile, profile_path)
        return _build_workflow_result(profile, profile_path, batch_summary=batch_summary)

    next_request = _build_decision_request(profile, profile_path, stage_name, batch_id, round_index + 1, batch_size)
    _store_pending_request(profile, next_request)
    _save_profile(profile, profile_path)
    return _decision_required_result(profile, profile_path, decision_request=next_request)


def start_or_resume_workflow(
    client,
    args,
    explicit_action="",
    requested_stage="",
    stage_runner=None,
    batch_size=DEFAULT_BATCH_SIZE,
):
    profile_path = common.resolve_profile_path(getattr(args, "profile_path", str(common.DEFAULT_TUNING_PROFILE_PATH)))
    stage_runner_fn = stage_runner or _default_stage_runner
    profile = _load_profile(profile_path)
    _ensure_request_state(profile)
    profile = _restore_profile_state(profile, profile_path)
    _save_profile(profile, profile_path)
    initial_state = agent_session.resume_session_state(profile)

    if requested_stage and requested_stage not in ("air_dual", "ground_dual"):
        raise ValueError("Unsupported requested_stage: {0}".format(requested_stage))

    if requested_stage == "ground_dual" and explicit_action != "enter_ground":
        if initial_state.get("workflow_stage") != "ground_dual":
            raise RuntimeError("ground_dual requires enter_ground before starting")

    if explicit_action:
        profile = _apply_explicit_action(profile, explicit_action)
        _clear_pending_request(profile)
        _save_profile(profile, profile_path)
        state = agent_session.resume_session_state(profile)
        if state.get("workflow_status") == "completed":
            return _build_workflow_result(profile, profile_path)

    state = agent_session.resume_session_state(profile)
    if state.get("workflow_status") == "waiting_user" and not explicit_action:
        return _build_workflow_result(profile, profile_path)

    profile = _run_prerequisites(client, args, profile_path, profile, stage_runner_fn)
    _ensure_request_state(profile)
    state = agent_session.resume_session_state(profile)
    if state.get("workflow_status") == "waiting_user":
        _save_profile(profile, profile_path)
        return _build_workflow_result(profile, profile_path)
    if state.get("workflow_status") == "completed":
        _save_profile(profile, profile_path)
        return _build_workflow_result(profile, profile_path)

    pending_request = _pending_request(profile)
    if pending_request is not None:
        stage_context = pending_request["context"]["stage_context"]
        llm_decision_contract.validate_decision_request_payload(
            pending_request,
            stage_context["batch_id"],
            stage_context["round_index"],
        )
        _save_profile(profile, profile_path)
        return _decision_required_result(profile, profile_path, decision_request=pending_request)

    stage_name = requested_stage or state.get("workflow_stage") or "air_dual"
    if stage_name not in ("air_dual", "ground_dual"):
        stage_name = "air_dual"
    profile, batch_id, current_round_index = _ensure_batch_for_stage(profile, profile_path, stage_name, args, batch_size)
    request_payload = _build_decision_request(profile, profile_path, stage_name, batch_id, current_round_index + 1, batch_size)
    _store_pending_request(profile, request_payload)
    _save_profile(profile, profile_path)
    return _decision_required_result(profile, profile_path, decision_request=request_payload)


def submit_agent_response(
    client,
    args,
    request_id,
    response_status,
    raw_agent_output,
    stage_runner=None,
    batch_size=DEFAULT_BATCH_SIZE,
):
    profile_path = common.resolve_profile_path(getattr(args, "profile_path", str(common.DEFAULT_TUNING_PROFILE_PATH)))
    stage_runner_fn = stage_runner or _default_stage_runner
    profile = _load_profile(profile_path)
    agent_tuning = _ensure_request_state(profile)
    pending_request = _pending_request(profile)
    consumed_ids = set(agent_tuning.get("consumed_request_ids", []))

    if request_id in consumed_ids and (pending_request is None or pending_request.get("request_id") != request_id):
        raise RuntimeError("request_id has already been consumed")
    if pending_request is None:
        raise RuntimeError("No pending decision request to consume")
    if pending_request.get("request_id") != request_id:
        raise RuntimeError("request_id does not match the current pending request")

    stage_context = pending_request["context"]["stage_context"]
    llm_decision_contract.validate_decision_request_payload(
        pending_request,
        stage_context["batch_id"],
        stage_context["round_index"],
    )

    if response_status == "timeout":
        result = _retry_pending_request(profile, profile_path, pending_request, "timeouts_used")
        _save_profile(profile, profile_path)
        return result

    if response_status != "ok":
        raise ValueError("Unsupported response_status: {0}".format(response_status))

    try:
        decision = json.loads(raw_agent_output)
    except (TypeError, ValueError):
        result = _retry_pending_request(profile, profile_path, pending_request, "decision_errors_used")
        _save_profile(profile, profile_path)
        return result

    if not isinstance(decision, dict):
        result = _retry_pending_request(profile, profile_path, pending_request, "decision_errors_used")
        _save_profile(profile, profile_path)
        return result

    try:
        llm_decision_contract.validate_llm_decision(
            decision,
            pending_request["context"]["runtime_guardrails"],
        )
    except ValueError:
        result = _retry_pending_request(profile, profile_path, pending_request, "decision_errors_used")
        _save_profile(profile, profile_path)
        return result

    return _execute_decision_round(
        client,
        args,
        profile_path,
        profile,
        pending_request,
        decision,
        raw_agent_output,
        stage_runner_fn,
        batch_size,
    )


def run_agent_workflow(
    client,
    args,
    explicit_action="",
    stage_runner=None,
    decision_policy=None,
    batch_size=DEFAULT_BATCH_SIZE,
):
    if decision_policy is not None and decision_policy is not default_decision_policy:
        raise RuntimeError("run_agent_workflow no longer supports direct decision_policy batch execution")
    return start_or_resume_workflow(
        client,
        args,
        explicit_action=explicit_action,
        stage_runner=stage_runner,
        batch_size=batch_size,
    )

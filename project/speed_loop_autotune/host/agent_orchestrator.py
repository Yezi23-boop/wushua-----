import copy
import pathlib
import time

from . import air_dual, agent_session, common, ground_dual, pwm_identify, pwm_map


DEFAULT_BATCH_SIZE = 10
DEFAULT_TRACE_FILENAME = "agent_decision_trace.jsonl"
DEFAULT_AIR_ACTIONS = ["continue_air", "enter_ground", "stop_air"]
DEFAULT_GROUND_ACTIONS = ["continue_ground", "save", "stop_without_save"]


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
    round_index = 1
    while round_index <= int(current_round_index or 0):
        result_path = agent_session.build_round_result_path(profile_path.parent, stage_name, batch_id, round_index)
        if pathlib.Path(result_path).exists():
            history.append(common.load_json_dict(str(result_path)))
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
    summary = profile.get(stage_name, {}).get("last_batch_summary", {})
    best_pid = _best_pid_from_stage(profile, stage_name)
    profile = _clear_waiting_state(profile)

    if explicit_action == "continue_air":
        profile["agent_tuning"]["workflow_stage"] = "air_dual"
        profile["agent_tuning"]["workflow_status"] = "running"
        return profile

    if explicit_action == "continue_ground":
        profile["agent_tuning"]["workflow_stage"] = "ground_dual"
        profile["agent_tuning"]["workflow_status"] = "running"
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


def _build_workflow_result(profile, profile_path, batch_summary=None):
    state = agent_session.resume_session_state(profile)
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
    return result


def run_agent_workflow(
    client,
    args,
    explicit_action="",
    stage_runner=None,
    decision_policy=None,
    batch_size=DEFAULT_BATCH_SIZE,
):
    profile_path = common.resolve_profile_path(getattr(args, "profile_path", str(common.DEFAULT_TUNING_PROFILE_PATH)))
    stage_runner_fn = stage_runner or _default_stage_runner
    decision_policy_fn = decision_policy or default_decision_policy
    batch_summary = None

    profile = _load_profile(profile_path)
    _save_profile(profile, profile_path)

    if explicit_action:
        profile = _apply_explicit_action(profile, explicit_action)
        _save_profile(profile, profile_path)
        state = agent_session.resume_session_state(profile)
        if state.get("workflow_status") == "completed":
            return _build_workflow_result(profile, profile_path)

    state = agent_session.resume_session_state(profile)
    if state.get("workflow_status") == "waiting_user" and not explicit_action:
        return _build_workflow_result(profile, profile_path)

    profile = _run_prerequisites(client, args, profile_path, profile, stage_runner_fn)
    state = agent_session.resume_session_state(profile)

    if state.get("workflow_status") == "waiting_user":
        return _build_workflow_result(profile, profile_path)
    if state.get("workflow_status") == "completed":
        return _build_workflow_result(profile, profile_path)

    stage_name = state.get("workflow_stage") or "air_dual"
    if stage_name not in ("air_dual", "ground_dual"):
        stage_name = "air_dual"

    profile, batch_summary = _run_stage_batch(
        client,
        args,
        profile_path,
        profile,
        stage_name,
        stage_runner_fn,
        decision_policy_fn,
        batch_size,
    )
    return _build_workflow_result(profile, profile_path, batch_summary=batch_summary)

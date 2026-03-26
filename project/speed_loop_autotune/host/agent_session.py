import json
import pathlib

from . import common


def _copy_pid_pair(pid_pair):
    if hasattr(pid_pair, "left") and hasattr(pid_pair, "right"):
        try:
            return common.wheel_pid_gains_to_dict(pid_pair)
        except (AttributeError, TypeError, ValueError):
            return None

    if not isinstance(pid_pair, dict):
        return None

    left = pid_pair.get("left", {})
    right = pid_pair.get("right", {})
    if not isinstance(left, dict):
        left = {}
    if not isinstance(right, dict):
        right = {}

    return {
        "left": {
            "kp": float(left.get("kp", 0.0)),
            "ki": float(left.get("ki", 0.0)),
            "kd": float(left.get("kd", 0.0)),
        },
        "right": {
            "kp": float(right.get("kp", 0.0)),
            "ki": float(right.get("ki", 0.0)),
            "kd": float(right.get("kd", 0.0)),
        },
    }


def _default_stage_block():
    return {
        "baseline_pid": None,
        "best_pid": None,
        "last_summary": None,
        "last_batch_best": None,
        "batch_round": 0,
        "last_batch_summary": None,
        "active_batch": None,
    }


def _allowed_actions_for_stage(stage_name):
    _validate_stage_name(stage_name)
    if stage_name == "air_dual":
        return ("continue_air", "enter_ground", "stop_air")
    return ("continue_ground", "save", "stop_without_save")


def _validate_stage_name(stage_name):
    if stage_name not in ("air_dual", "ground_dual"):
        raise ValueError("Unsupported stage_name: {0}".format(stage_name))
    return stage_name


def _stage_worker_mode(stage_name):
    _validate_stage_name(stage_name)
    if stage_name == "ground_dual":
        return common.MODE_GROUND_DUAL_STEP
    return common.MODE_AIR_DUAL_STEP


def _ensure_stage_block(profile, stage_name):
    _validate_stage_name(stage_name)
    block = profile.get(stage_name)
    if not isinstance(block, dict):
        block = {}

    default_block = _default_stage_block()
    for key in default_block:
        if key not in block:
            block[key] = default_block[key]

    profile[stage_name] = block
    return block


def _phase_from_round_index(round_index):
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


def _copy_path_text(path_value):
    if not path_value:
        return ""
    return pathlib.Path(path_value).as_posix()


def _extract_candidate_pid(result_payload):
    if not isinstance(result_payload, dict):
        return None

    copied = _copy_pid_pair(result_payload.get("candidate_pid"))
    if copied is not None:
        return copied

    copied = _copy_pid_pair(result_payload.get("best_pid"))
    if copied is not None:
        return copied

    return _copy_pid_pair(result_payload.get("gains"))


def _copy_summary(summary):
    if not isinstance(summary, dict):
        return {}
    return json.loads(json.dumps(summary, ensure_ascii=False))


def _extract_batch_best(summary):
    best_pid = _copy_pid_pair(summary.get("best_pid"))
    payload = {
        "best_pid": best_pid,
        "combined_score": _coerce_score(summary.get("combined_score")),
    }
    if "a_score" in summary:
        payload["a_score"] = _coerce_score(summary.get("a_score"))
    if "b_score" in summary:
        payload["b_score"] = _coerce_score(summary.get("b_score"))
    if "band_scores" in summary:
        payload["band_scores"] = common.normalize_band_scores(summary.get("band_scores"))
    return payload


def _extract_summary_score(summary):
    if not isinstance(summary, dict):
        return None
    return _coerce_score(summary.get("combined_score", summary.get("score")))


def _validate_allowed_actions(stage_name, recommended_action, allowed_actions):
    valid_actions = _allowed_actions_for_stage(stage_name)
    normalized_allowed = list(allowed_actions)

    if not normalized_allowed:
        raise ValueError("allowed_actions must not be empty")
    if recommended_action not in normalized_allowed:
        raise ValueError("recommended_action must be included in allowed_actions")

    for action in normalized_allowed:
        if action not in valid_actions:
            raise ValueError("Unsupported action for {0}: {1}".format(stage_name, action))

    return normalized_allowed


def ensure_agent_profile_defaults(profile):
    if not isinstance(profile, dict):
        profile = {}

    meta = profile.get("meta")
    if not isinstance(meta, dict):
        meta = {}
        profile["meta"] = meta
    if "profile_version" not in meta:
        meta["profile_version"] = common.PROFILE_VERSION

    agent_tuning = profile.get("agent_tuning")
    if not isinstance(agent_tuning, dict):
        agent_tuning = {}

    if "session_id" not in agent_tuning:
        agent_tuning["session_id"] = ""
    if "workflow_stage" not in agent_tuning:
        agent_tuning["workflow_stage"] = "air_dual"
    if "workflow_status" not in agent_tuning:
        agent_tuning["workflow_status"] = "running"
    if "current_batch_id" not in agent_tuning:
        agent_tuning["current_batch_id"] = ""
    if "current_round_index" not in agent_tuning:
        agent_tuning["current_round_index"] = 0
    if "auto_completed_stages" not in agent_tuning:
        agent_tuning["auto_completed_stages"] = []
    if "pending_user_action" not in agent_tuning:
        agent_tuning["pending_user_action"] = None
    if "last_worker_mode" not in agent_tuning:
        agent_tuning["last_worker_mode"] = common.MODE_AIR_DUAL_STEP
    if "last_result_path" not in agent_tuning:
        agent_tuning["last_result_path"] = ""
    if "last_decision_trace_path" not in agent_tuning:
        agent_tuning["last_decision_trace_path"] = ""

    profile["agent_tuning"] = agent_tuning
    _ensure_stage_block(profile, "air_dual")
    _ensure_stage_block(profile, "ground_dual")
    return profile


def start_batch(profile, stage_name, batch_id, start_pid):
    profile = ensure_agent_profile_defaults(profile)
    stage_name = _validate_stage_name(stage_name)
    stage_block = _ensure_stage_block(profile, stage_name)
    worker_mode = _stage_worker_mode(stage_name)
    copied_start_pid = _copy_pid_pair(start_pid)

    stage_block["active_batch"] = {
        "batch_id": batch_id,
        "start_pid": copied_start_pid,
        "current_best_pid": _copy_pid_pair(copied_start_pid),
        "current_best_score": None,
        "rounds_completed": 0,
        "search_phase": common.AGENT_SEARCH_PHASES[0],
        "last_round_pid": None,
        "last_round_score": None,
        "last_round_result_path": None,
    }

    agent_tuning = profile["agent_tuning"]
    agent_tuning["workflow_stage"] = stage_name
    agent_tuning["workflow_status"] = "running"
    agent_tuning["current_batch_id"] = batch_id
    agent_tuning["current_round_index"] = 0
    agent_tuning["pending_user_action"] = None
    agent_tuning["last_worker_mode"] = worker_mode

    return profile


def set_pending_user_action(profile, stage_name, batch_id, recommended_action, allowed_actions, reason):
    profile = ensure_agent_profile_defaults(profile)
    stage_name = _validate_stage_name(stage_name)
    allowed_actions = _validate_allowed_actions(stage_name, recommended_action, allowed_actions)
    agent_tuning = profile["agent_tuning"]
    agent_tuning["workflow_stage"] = stage_name
    agent_tuning["workflow_status"] = "waiting_user"
    agent_tuning["current_batch_id"] = batch_id
    agent_tuning["pending_user_action"] = {
        "stage": stage_name,
        "batch_id": batch_id,
        "recommended_action": recommended_action,
        "allowed_actions": allowed_actions,
        "reason": reason,
    }
    return profile


def build_round_result_path(log_root, stage_name, batch_id, round_index):
    _validate_stage_name(stage_name)
    return (
        pathlib.Path(log_root)
        / "agent_rounds"
        / stage_name
        / "{0}_r{1:02d}.json".format(batch_id, int(round_index))
    )


def build_waveform_path(log_root, stage_name, batch_id, round_index):
    _validate_stage_name(stage_name)
    return (
        pathlib.Path(log_root)
        / "agent_waveforms"
        / stage_name
        / "{0}_r{1:02d}.jsonl".format(batch_id, int(round_index))
    )


def record_round_result(profile, stage_name, batch_id, round_index, result_path, result_payload):
    profile = ensure_agent_profile_defaults(profile)
    stage_name = _validate_stage_name(stage_name)
    stage_block = _ensure_stage_block(profile, stage_name)
    round_value = int(round_index or 0)
    score_value = _coerce_score(getattr(result_payload, "get", lambda *_args, **_kwargs: None)("combined_score"))
    candidate_pid = _extract_candidate_pid(result_payload)
    result_path_text = _copy_path_text(result_path)

    active_batch = stage_block.get("active_batch")
    if not isinstance(active_batch, dict) or active_batch.get("batch_id") != batch_id:
        active_batch = {
            "batch_id": batch_id,
            "start_pid": candidate_pid,
            "current_best_pid": candidate_pid,
            "current_best_score": None,
            "rounds_completed": 0,
            "search_phase": _phase_from_round_index(round_value),
            "last_round_pid": None,
            "last_round_score": None,
            "last_round_result_path": None,
        }
        stage_block["active_batch"] = active_batch

    active_batch["rounds_completed"] = max(int(active_batch.get("rounds_completed", 0) or 0), round_value)
    active_batch["search_phase"] = _phase_from_round_index(round_value)
    active_batch["last_round_pid"] = candidate_pid
    active_batch["last_round_score"] = score_value
    active_batch["last_round_result_path"] = result_path_text

    best_score = _coerce_score(active_batch.get("current_best_score"))
    if score_value is not None and (best_score is None or score_value < best_score):
        active_batch["current_best_score"] = score_value
        if candidate_pid is not None:
            active_batch["current_best_pid"] = candidate_pid

    agent_tuning = profile["agent_tuning"]
    agent_tuning["workflow_stage"] = stage_name
    agent_tuning["workflow_status"] = "running"
    agent_tuning["current_batch_id"] = batch_id
    agent_tuning["current_round_index"] = round_value
    agent_tuning["pending_user_action"] = None
    agent_tuning["last_worker_mode"] = _stage_worker_mode(stage_name)
    agent_tuning["last_result_path"] = result_path_text

    return profile


def append_decision_trace(trace_path, trace_row):
    trace_file = pathlib.Path(trace_path)
    trace_file.parent.mkdir(parents=True, exist_ok=True)
    with trace_file.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(trace_row, ensure_ascii=False, sort_keys=True))
        handle.write("\n")
    return trace_file


def resume_session_state(profile):
    profile = ensure_agent_profile_defaults(profile)
    agent_tuning = profile["agent_tuning"]
    return {
        "workflow_stage": agent_tuning.get("workflow_stage", ""),
        "workflow_status": agent_tuning.get("workflow_status", ""),
        "current_batch_id": agent_tuning.get("current_batch_id", ""),
        "current_round_index": int(agent_tuning.get("current_round_index", 0) or 0),
        "pending_user_action": agent_tuning.get("pending_user_action"),
        "last_worker_mode": agent_tuning.get("last_worker_mode", ""),
        "last_result_path": agent_tuning.get("last_result_path", ""),
        "last_decision_trace_path": agent_tuning.get("last_decision_trace_path", ""),
    }


def finish_batch_summary(profile, stage_name, batch_summary, recommended_action, allowed_actions, reason):
    profile = ensure_agent_profile_defaults(profile)
    stage_name = _validate_stage_name(stage_name)
    stage_block = _ensure_stage_block(profile, stage_name)
    summary = _copy_summary(batch_summary)
    best_payload = _extract_batch_best(summary)
    active_batch = stage_block.get("active_batch")
    batch_id = summary.get("batch_id", "")
    previous_best_score = _extract_summary_score(stage_block.get("last_summary"))

    if isinstance(active_batch, dict):
        if not batch_id:
            batch_id = active_batch.get("batch_id", "")
        if stage_block.get("baseline_pid") is None and active_batch.get("start_pid") is not None:
            stage_block["baseline_pid"] = _copy_pid_pair(active_batch.get("start_pid"))
        stage_block["active_batch"] = active_batch
    stage_block["last_batch_summary"] = summary
    stage_block["last_summary"] = summary
    stage_block["last_batch_best"] = best_payload
    stage_block["batch_round"] = int(stage_block.get("batch_round", 0) or 0) + 1

    if stage_name == "air_dual" and best_payload.get("best_pid") is not None:
        if previous_best_score is None or (
            best_payload.get("combined_score") is not None and best_payload.get("combined_score") <= previous_best_score
        ):
            stage_block["best_pid"] = _copy_pid_pair(best_payload.get("best_pid"))

    profile = set_pending_user_action(profile, stage_name, batch_id, recommended_action, allowed_actions, reason)
    return profile


def finalize_stage_best(profile, stage_name, best_pid, summary, explicit_action):
    profile = ensure_agent_profile_defaults(profile)
    stage_name = _validate_stage_name(stage_name)
    stage_block = _ensure_stage_block(profile, stage_name)
    copied_summary = _copy_summary(summary)
    copied_best_pid = _copy_pid_pair(best_pid)
    agent_tuning = profile["agent_tuning"]

    stage_block["last_summary"] = copied_summary
    if stage_block.get("baseline_pid") is None:
        stage_block["baseline_pid"] = copied_best_pid

    if stage_name == "ground_dual":
        if explicit_action == "save" and copied_best_pid is not None:
            stage_block["best_pid"] = copied_best_pid
            agent_tuning["workflow_status"] = "completed"
            agent_tuning["pending_user_action"] = None
        return profile

    if copied_best_pid is not None:
        stage_block["best_pid"] = copied_best_pid
    if explicit_action in ("enter_ground", "stop_air"):
        agent_tuning["workflow_status"] = "completed"
        agent_tuning["pending_user_action"] = None
    return profile

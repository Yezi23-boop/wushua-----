from . import common


def _copy_pid_pair(pid_pair):
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


def _stage_worker_mode(stage_name):
    if stage_name == "ground_dual":
        return common.MODE_GROUND_DUAL_STEP
    return common.MODE_AIR_DUAL_STEP


def _ensure_stage_block(profile, stage_name):
    block = profile.get(stage_name)
    if not isinstance(block, dict):
        block = {}

    default_block = _default_stage_block()
    for key in default_block:
        if key not in block:
            block[key] = default_block[key]

    profile[stage_name] = block
    return block


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
    stage_block = _ensure_stage_block(profile, stage_name)
    worker_mode = _stage_worker_mode(stage_name)
    copied_start_pid = _copy_pid_pair(start_pid)

    stage_block["active_batch"] = {
        "batch_id": batch_id,
        "start_pid": copied_start_pid,
        "current_best_pid": copied_start_pid,
        "current_best_score": None,
        "rounds_completed": 0,
        "search_phase": common.AGENT_SEARCH_PHASES[0],
        "last_round_pid": None,
        "last_round_score": None,
        "last_round_result_path": None,
    }
    stage_block["batch_round"] = 0
    stage_block["last_batch_best"] = None
    stage_block["last_batch_summary"] = None

    agent_tuning = profile["agent_tuning"]
    agent_tuning["workflow_stage"] = stage_name
    agent_tuning["workflow_status"] = "running"
    agent_tuning["current_batch_id"] = batch_id
    agent_tuning["current_round_index"] = 1
    agent_tuning["pending_user_action"] = None
    agent_tuning["last_worker_mode"] = worker_mode

    return profile


def set_pending_user_action(profile, stage_name, batch_id, recommended_action, allowed_actions, reason):
    profile = ensure_agent_profile_defaults(profile)
    agent_tuning = profile["agent_tuning"]
    agent_tuning["workflow_stage"] = stage_name
    agent_tuning["workflow_status"] = "waiting_user"
    agent_tuning["current_batch_id"] = batch_id
    agent_tuning["pending_user_action"] = {
        "stage": stage_name,
        "batch_id": batch_id,
        "recommended_action": recommended_action,
        "allowed_actions": list(allowed_actions),
        "reason": reason,
    }
    return profile

import json


def _to_float(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return float(default)


def _to_int(value, default=0):
    try:
        return int(value)
    except (TypeError, ValueError):
        return int(default)


def _clone(value):
    return json.loads(json.dumps(value))


def _clamp(value, lower, upper):
    if value < lower:
        return lower
    if value > upper:
        return upper
    return value


def _apply_precision_and_limits(pid_pair, limits, precision):
    result = _clone(pid_pair)
    kp_decimals = _to_int(precision.get("kp_decimals"), 2)
    ki_decimals = _to_int(precision.get("ki_decimals"), 2)
    kd_decimals = _to_int(precision.get("kd_decimals"), 2)
    kp_min = _to_float(limits.get("kp_min"), 0.0)
    kp_max = _to_float(limits.get("kp_max"), 500.0)
    ki_min = _to_float(limits.get("ki_min"), 0.0)
    ki_max = _to_float(limits.get("ki_max"), 200.0)
    kd_min = _to_float(limits.get("kd_min"), 0.0)
    kd_max = _to_float(limits.get("kd_max"), 50.0)

    for side_name in ("left", "right"):
        side = result[side_name]
        side["kp"] = round(_clamp(_to_float(side.get("kp")), kp_min, kp_max), kp_decimals)
        side["ki"] = round(_clamp(_to_float(side.get("ki")), ki_min, ki_max), ki_decimals)
        side["kd"] = round(_clamp(_to_float(side.get("kd")), kd_min, kd_max), kd_decimals)
    return result


def _pick_anchor_pid(anchors):
    candidate = anchors.get("current_batch_best_pid")
    if candidate is not None:
        return _clone(candidate)
    candidate = anchors.get("batch_start_pid")
    if candidate is not None:
        return _clone(candidate)
    candidate = anchors.get("baseline_pid")
    if candidate is not None:
        return _clone(candidate)
    candidate = anchors.get("seed_pi")
    if candidate is not None:
        return _clone(candidate)
    return {
        "left": {"kp": 50.0, "ki": 10.0, "kd": 0.0},
        "right": {"kp": 50.0, "ki": 10.0, "kd": 0.0},
    }


def _step_from_gain(gain_value, ratio, minimum):
    return max(float(minimum), round(_to_float(gain_value) * float(ratio), 2))


def _kp_step(side_gain, stage_name, aggressive=False):
    base = 1.0
    ratio = 0.02
    if stage_name == "air_dual":
        base = 1.5
        ratio = 0.025
    if aggressive:
        base += 0.5
        ratio += 0.01
    return _step_from_gain(side_gain, ratio, base)


def _ki_step(side_gain, stage_name, aggressive=False):
    base = 1.0
    ratio = 0.12
    if stage_name == "air_dual":
        base = 1.5
        ratio = 0.15
    if stage_name == "ground_dual":
        base = 2.0
        ratio = 0.18
    if aggressive:
        base += 1.0
        ratio += 0.07
    return _step_from_gain(side_gain, ratio, base)


def _significant_regression(last_score, best_score, guardrails):
    if best_score is None:
        return False
    absolute_guard = _to_float(guardrails.get("significant_regression_abs"), 5.0)
    ratio_guard = abs(best_score) * _to_float(guardrails.get("significant_regression_ratio"), 0.03)
    return last_score > best_score + max(absolute_guard, ratio_guard)


def _worse_side(last_round):
    left_score = _to_float(last_round.get("left_score"), _to_float(last_round.get("a_score")))
    right_score = _to_float(last_round.get("right_score"), _to_float(last_round.get("b_score")))
    if left_score >= right_score:
        return "left"
    return "right"


def _apply_kp_change(candidate, side_name, delta_value):
    candidate[side_name]["kp"] = _to_float(candidate[side_name].get("kp")) + float(delta_value)


def _apply_ki_change(candidate, side_name, delta_value):
    candidate[side_name]["ki"] = _to_float(candidate[side_name].get("ki")) + float(delta_value)


def build_llm_decision(decision_request):
    context = decision_request.get("context", {})
    stage_context = context.get("stage_context", {})
    stage_name = stage_context.get("stage_name", "air_dual")
    anchors = context.get("pid_anchors", {})
    recent_rounds = context.get("recent_rounds", [])
    guardrails = context.get("runtime_guardrails", {})
    limits = guardrails.get("absolute_pid_limits", {})
    precision = guardrails.get("precision", {})

    candidate_pid = _pick_anchor_pid(anchors)
    decision_mode = "hold"
    primary_reason = "measurement_conflict"
    expected_outcome = "verify_plateau"
    confidence = "medium"
    risk_level = "low"
    decision_summary = "hold the current batch-best PID as the reference point"
    supporting_signals = [
        "structured metrics remain the primary evidence",
        "keep the current batch-best anchor until a dominant issue is clear",
    ]

    if recent_rounds:
        last_round = recent_rounds[-1]
        last_score = _to_float(last_round.get("combined_score"))
        best_score = anchors.get("current_batch_best_score")
        if best_score is not None:
            best_score = _to_float(best_score)
        overshoot = bool(last_round.get("overshoot_flag")) or bool(last_round.get("persistent_overshoot_flag"))
        speed_drop = bool(last_round.get("speed_drop_flag"))
        saturation = _to_float(last_round.get("pwm_saturation_ratio"))
        headroom_limited = bool(last_round.get("current_limit_or_headroom_flag")) or saturation >= 0.95
        left_right_gap = _to_float(last_round.get("left_right_gap"))
        plateau_detected = bool(last_round.get("plateau_detected"))
        dominant_issue = last_round.get("dominant_issue")
        worse_side = _worse_side(last_round)

        if _significant_regression(last_score, best_score, guardrails):
            decision_mode = "rollback"
            decision_summary = "rollback to the current batch-best PID after a clear score regression"
            supporting_signals = [
                "latest combined score regressed materially against the current batch best",
                "protect the known best region before trying another change",
            ]
            confidence = "high"
        elif headroom_limited:
            decision_mode = "rollback"
            primary_reason = "saturation_limited"
            expected_outcome = "reduce_overshoot"
            risk_level = "high"
            decision_summary = "hold the batch-best PID because saturation dominates the latest result"
            supporting_signals = [
                "pwm saturation or headroom limit is active",
                "avoid pushing Kp or Ki further into a clipped region",
            ]
            confidence = "high"
        elif left_right_gap >= 0.12:
            if overshoot:
                kp_delta = _kp_step(candidate_pid[worse_side].get("kp"), stage_name, True)
                _apply_kp_change(candidate_pid, worse_side, -kp_delta)
                decision_summary = "single-side Kp trim to reduce overshoot on the weaker side"
                supporting_signals = [
                    "left-right gap is large enough to justify a split correction",
                    "overshoot is present, so Kp stays responsible for damping the response",
                ]
                primary_reason = "left_right_mismatch"
                expected_outcome = "fix_left_right_gap"
                decision_mode = "rebalance_left"
                if worse_side == "right":
                    decision_mode = "rebalance_right"
            elif speed_drop or dominant_issue == "steady_error":
                ki_delta = _ki_step(candidate_pid[worse_side].get("ki"), stage_name, True)
                _apply_ki_change(candidate_pid, worse_side, ki_delta)
                decision_summary = "single-side Ki boost to improve wheel sustain without touching the other side"
                supporting_signals = [
                    "left-right gap is large enough to justify a split correction",
                    "speed-drop or steady-state weakness is present, so Ki handles the sustain side",
                ]
                primary_reason = "left_right_mismatch"
                expected_outcome = "fix_left_right_gap"
                decision_mode = "rebalance_left"
                if worse_side == "right":
                    decision_mode = "rebalance_right"
                risk_level = "medium"
            else:
                kp_delta = _kp_step(candidate_pid[worse_side].get("kp"), stage_name, False)
                _apply_kp_change(candidate_pid, worse_side, kp_delta)
                decision_summary = "single-side Kp increase to recover response on the weaker side"
                supporting_signals = [
                    "left-right gap is visible",
                    "response-side mismatch is stronger than steady-state evidence here",
                ]
                primary_reason = "left_right_mismatch"
                expected_outcome = "fix_left_right_gap"
                decision_mode = "rebalance_left"
                if worse_side == "right":
                    decision_mode = "rebalance_right"
                risk_level = "medium"
        elif overshoot and speed_drop:
            for side_name in ("left", "right"):
                kp_delta = _kp_step(candidate_pid[side_name].get("kp"), stage_name, True)
                ki_delta = _ki_step(candidate_pid[side_name].get("ki"), stage_name, True)
                _apply_kp_change(candidate_pid, side_name, -kp_delta)
                _apply_ki_change(candidate_pid, side_name, ki_delta)
            decision_mode = "local_refine"
            primary_reason = "overshoot"
            expected_outcome = "reduce_overshoot"
            decision_summary = "mixed PID move: Kp down for overshoot, Ki up for sustain and steady pull"
            supporting_signals = [
                "overshoot says the response loop is too aggressive",
                "speed-drop says integral support is still insufficient after the transient",
            ]
            risk_level = "medium"
            confidence = "high"
        elif overshoot:
            for side_name in ("left", "right"):
                kp_delta = _kp_step(candidate_pid[side_name].get("kp"), stage_name, False)
                _apply_kp_change(candidate_pid, side_name, -kp_delta)
            decision_mode = "local_refine"
            primary_reason = "overshoot"
            expected_outcome = "reduce_overshoot"
            decision_summary = "reduce Kp first because overshoot remains the dominant problem"
            supporting_signals = [
                "overshoot or persistent overshoot flag is active",
                "Kp stays responsible for transient speed and overshoot control",
            ]
            risk_level = "medium"
        elif speed_drop or dominant_issue == "steady_error" or plateau_detected:
            aggressive_ki = bool(speed_drop) or stage_name == "ground_dual"
            for side_name in ("left", "right"):
                ki_delta = _ki_step(candidate_pid[side_name].get("ki"), stage_name, aggressive_ki)
                _apply_ki_change(candidate_pid, side_name, ki_delta)
            decision_mode = "local_refine"
            primary_reason = "steady_error"
            expected_outcome = "improve_steady_state"
            decision_summary = "increase Ki to improve steady pull, speed-hold, and load sustain"
            supporting_signals = [
                "speed-drop or plateau suggests the loop still lacks integral support",
                "Ki is allowed to move in larger steps once sustain becomes the bottleneck",
            ]
            risk_level = "medium"
            confidence = "high"
        else:
            for side_name in ("left", "right"):
                kp_delta = _kp_step(candidate_pid[side_name].get("kp"), stage_name, False)
                _apply_kp_change(candidate_pid, side_name, kp_delta)
            decision_mode = "jump_explore"
            primary_reason = "slow_response"
            expected_outcome = "improve_response"
            decision_summary = "increase Kp to explore a faster response region"
            supporting_signals = [
                "no strong overshoot or sustain limitation is visible",
                "Kp remains the first search axis for response speed",
            ]
            risk_level = "medium"

    candidate_pid = _apply_precision_and_limits(candidate_pid, limits, precision)
    return {
        "schema_version": 1,
        "candidate_pid": candidate_pid,
        "decision_summary": decision_summary,
        "primary_reason": primary_reason,
        "supporting_signals": supporting_signals,
        "decision_mode": decision_mode,
        "base_reference": "current_batch_best" if anchors.get("current_batch_best_pid") is not None else "batch_start",
        "expected_outcome": expected_outcome,
        "confidence": confidence,
        "risk_level": risk_level,
        "needs_waveform_review": False,
        "batch_end_recommendation_if_no_improve": "continue_ground" if stage_name == "ground_dual" else "continue_air",
    }

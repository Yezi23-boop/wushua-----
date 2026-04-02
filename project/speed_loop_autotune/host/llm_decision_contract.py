import decimal
import math
import re


SCHEMA_VERSION = 1
REQUEST_ID_RE = re.compile(r"^(air|ground)_[0-9]{4}_r[0-9]{2}$")
STAGE_NAME_VALUES = ("air_dual", "ground_dual")
STATUS_VALUES = ("running",)
SCORE_DIRECTION_VALUES = ("lower_is_better",)
WAVEFORM_ROLE_VALUES = ("secondary_evidence",)
DISALLOWED_ACTION_VALUES = (
    "save",
    "enter_ground",
    "continue_air",
    "continue_ground",
    "stop_air",
    "stop_without_save",
)
PRIMARY_REASON_VALUES = (
    "slow_response",
    "steady_error",
    "overshoot",
    "left_right_mismatch",
    "plateau_break",
    "saturation_limited",
    "measurement_conflict",
)
DECISION_MODE_VALUES = (
    "hold",
    "local_refine",
    "rollback",
    "jump_explore",
    "rebalance_left",
    "rebalance_right",
    "kd_probe",
)
BASE_REFERENCE_VALUES = (
    "batch_start",
    "last_round",
    "current_batch_best",
    "historical_stage_best",
    "seed_pi",
)
EXPECTED_OUTCOME_VALUES = (
    "improve_response",
    "reduce_overshoot",
    "improve_steady_state",
    "fix_left_right_gap",
    "verify_plateau",
    "test_new_region",
)
CONFIDENCE_VALUES = ("low", "medium", "high")
RISK_LEVEL_VALUES = ("low", "medium", "high")
RECOMMENDATION_VALUES = (
    "continue_air",
    "enter_ground",
    "stop_air",
    "continue_ground",
    "save",
    "stop_without_save",
)
ROUND_RESULT_MODE_VALUES = ("air-dual-step", "ground-dual-step")

DEFAULT_PID_LIMITS = {
    "kp_min": 0.0,
    "kp_max": 500.0,
    "ki_min": 0.0,
    "ki_max": 200.0,
    "kd_min": 0.0,
    "kd_max": 50.0,
}


def _error(message):
    raise ValueError(message)


def _is_finite_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _require_dict(value, label):
    if not isinstance(value, dict):
        _error("{0} must be an object".format(label))


def _require_no_additional_properties(data, allowed_keys, label):
    for key in data:
        if key not in allowed_keys:
            _error("{0} has unexpected property: {1}".format(label, key))


def _require_keys(data, required_keys, label):
    for key in required_keys:
        if key not in data:
            _error("{0} is missing required property: {1}".format(label, key))


def _require_enum(value, allowed_values, label):
    if value not in allowed_values:
        _error("{0} must be one of: {1}".format(label, ", ".join(allowed_values)))


def _require_string(value, label, min_length=None, max_length=None):
    if not isinstance(value, str):
        _error("{0} must be a string".format(label))
    if min_length is not None and len(value) < min_length:
        _error("{0} is too short".format(label))
    if max_length is not None and len(value) > max_length:
        _error("{0} is too long".format(label))


def _require_bool(value, label):
    if not isinstance(value, bool):
        _error("{0} must be a boolean".format(label))


def _require_int_in_range(value, label, minimum=None, maximum=None):
    if not isinstance(value, int) or isinstance(value, bool):
        _error("{0} must be an integer".format(label))
    if minimum is not None and value < minimum:
        _error("{0} must be >= {1}".format(label, minimum))
    if maximum is not None and value > maximum:
        _error("{0} must be <= {1}".format(label, maximum))


def _require_finite_number(value, label, minimum=None, maximum=None):
    if not _is_finite_number(value):
        _error("{0} must be a finite number".format(label))
    if minimum is not None and value < minimum:
        _error("{0} must be >= {1}".format(label, minimum))
    if maximum is not None and value > maximum:
        _error("{0} must be <= {1}".format(label, maximum))


def _require_true(value, label):
    if value is not True:
        _error("{0} must be true".format(label))


def _decimal_places(value):
    decimal_value = decimal.Decimal(str(value)).normalize()
    exponent = decimal_value.as_tuple().exponent
    if exponent >= 0:
        return 0
    return -exponent


def _resolve_precision(guardrails, label_prefix):
    _require_dict(guardrails, label_prefix)
    if "precision" not in guardrails:
        _error("{0} is missing required property: precision".format(label_prefix))

    precision = guardrails["precision"]
    _require_dict(precision, label_prefix + ".precision")
    _require_no_additional_properties(
        precision,
        ("kp_decimals", "ki_decimals", "kd_decimals"),
        label_prefix + ".precision",
    )
    _require_keys(
        precision,
        ("kp_decimals", "ki_decimals", "kd_decimals"),
        label_prefix + ".precision",
    )
    _require_int_in_range(precision["kp_decimals"], label_prefix + ".precision.kp_decimals", 0, 6)
    _require_int_in_range(precision["ki_decimals"], label_prefix + ".precision.ki_decimals", 0, 6)
    _require_int_in_range(precision["kd_decimals"], label_prefix + ".precision.kd_decimals", 0, 6)
    return precision


def _validate_pid_bundle(pid_bundle, label, limits=None, require_nonnegative=False):
    _require_dict(pid_bundle, label)
    _require_no_additional_properties(pid_bundle, ("left", "right"), label)
    _require_keys(pid_bundle, ("left", "right"), label)

    for side in ("left", "right"):
        side_label = "{0}.{1}".format(label, side)
        gains = pid_bundle[side]
        _require_dict(gains, side_label)
        _require_no_additional_properties(gains, ("kp", "ki", "kd"), side_label)
        _require_keys(gains, ("kp", "ki", "kd"), side_label)
        kp_minimum = None
        ki_minimum = None
        kd_minimum = None
        kp_maximum = None
        ki_maximum = None
        kd_maximum = None
        if limits is not None:
            kp_minimum = limits.get("kp_min")
            ki_minimum = limits.get("ki_min")
            kd_minimum = limits.get("kd_min")
            kp_maximum = limits.get("kp_max")
            ki_maximum = limits.get("ki_max")
            kd_maximum = limits.get("kd_max")
        if require_nonnegative:
            kp_minimum = max(0.0, 0.0 if kp_minimum is None else kp_minimum)
            ki_minimum = max(0.0, 0.0 if ki_minimum is None else ki_minimum)
            kd_minimum = max(0.0, 0.0 if kd_minimum is None else kd_minimum)
        _require_finite_number(gains["kp"], side_label + ".kp", kp_minimum, kp_maximum)
        _require_finite_number(gains["ki"], side_label + ".ki", ki_minimum, ki_maximum)
        _require_finite_number(gains["kd"], side_label + ".kd", kd_minimum, kd_maximum)


def _validate_pid_bundle_precision(pid_bundle, label, precision):
    for side in ("left", "right"):
        gains = pid_bundle[side]
        if _decimal_places(gains["kp"]) > precision["kp_decimals"]:
            _error("{0}.{1}.kp exceeds allowed precision".format(label, side))
        if _decimal_places(gains["ki"]) > precision["ki_decimals"]:
            _error("{0}.{1}.ki exceeds allowed precision".format(label, side))
        if _decimal_places(gains["kd"]) > precision["kd_decimals"]:
            _error("{0}.{1}.kd exceeds allowed precision".format(label, side))


def _resolve_pid_limits(guardrails, label_prefix, require_present):
    limits = dict(DEFAULT_PID_LIMITS)
    if isinstance(guardrails, dict):
        absolute_limits = guardrails.get("absolute_pid_limits")
        if require_present and absolute_limits is None:
            _error("{0} is missing required property: absolute_pid_limits".format(label_prefix))
        if absolute_limits is not None:
            absolute_label = label_prefix + ".absolute_pid_limits"
            _require_dict(absolute_limits, absolute_label)
            _require_no_additional_properties(
                absolute_limits,
                ("kp_min", "kp_max", "ki_min", "ki_max", "kd_min", "kd_max"),
                absolute_label,
            )
            _require_keys(
                absolute_limits,
                ("kp_min", "kp_max", "ki_min", "ki_max", "kd_min", "kd_max"),
                absolute_label,
            )
            for key in limits:
                _require_finite_number(
                    absolute_limits[key],
                    absolute_label + "." + key,
                )
                limits[key] = float(absolute_limits[key])
    absolute_label = label_prefix + ".absolute_pid_limits"
    if limits["kp_min"] > limits["kp_max"]:
        _error(absolute_label + ".kp_min must be <= kp_max")
    if limits["ki_min"] > limits["ki_max"]:
        _error(absolute_label + ".ki_min must be <= ki_max")
    if limits["kd_min"] > limits["kd_max"]:
        _error(absolute_label + ".kd_min must be <= kd_max")
    return limits


def _validate_band_scores_dict(band_scores, label, allow_none_values):
    _require_dict(band_scores, label)
    _require_no_additional_properties(band_scores, ("low", "mid", "high", "top"), label)
    _require_keys(band_scores, ("low", "mid", "high", "top"), label)
    for band_name in ("low", "mid", "high", "top"):
        band_label = label + "." + band_name
        if band_scores[band_name] is None:
            if not allow_none_values:
                _error(band_label + " must be a finite number")
            continue
        _require_finite_number(band_scores[band_name], band_label)


def validate_runtime_guardrails(guardrails, label="runtime_guardrails"):
    _require_dict(guardrails, label)
    _require_no_additional_properties(
        guardrails,
        (
            "absolute_pid_limits",
            "precision",
            "allow_large_jump",
            "single_candidate_only",
            "worker_timeout_seconds",
            "decision_retry_budget",
            "timeout_retry_budget",
            "significant_regression_abs",
            "significant_regression_ratio",
            "plateau_abs_threshold",
            "plateau_rounds",
            "abnormal_persistent_rounds",
            "waveform_severe_flag_threshold",
        ),
        label,
    )
    _require_keys(
        guardrails,
        (
            "absolute_pid_limits",
            "precision",
            "allow_large_jump",
            "single_candidate_only",
            "worker_timeout_seconds",
            "decision_retry_budget",
            "timeout_retry_budget",
            "significant_regression_abs",
            "significant_regression_ratio",
            "plateau_abs_threshold",
            "plateau_rounds",
            "abnormal_persistent_rounds",
            "waveform_severe_flag_threshold",
        ),
        label,
    )
    _resolve_precision(guardrails, label)
    _resolve_pid_limits(guardrails, label, True)
    _require_true(guardrails["allow_large_jump"], label + ".allow_large_jump")
    _require_true(guardrails["single_candidate_only"], label + ".single_candidate_only")
    _require_int_in_range(guardrails["worker_timeout_seconds"], label + ".worker_timeout_seconds", 1, None)
    _require_int_in_range(guardrails["decision_retry_budget"], label + ".decision_retry_budget", 0, None)
    _require_int_in_range(guardrails["timeout_retry_budget"], label + ".timeout_retry_budget", 0, None)
    _require_finite_number(guardrails["significant_regression_abs"], label + ".significant_regression_abs", 0.0, None)
    _require_finite_number(guardrails["significant_regression_ratio"], label + ".significant_regression_ratio", 0.0, None)
    _require_finite_number(guardrails["plateau_abs_threshold"], label + ".plateau_abs_threshold", 0.0, None)
    _require_int_in_range(guardrails["plateau_rounds"], label + ".plateau_rounds", 1, None)
    _require_int_in_range(guardrails["abnormal_persistent_rounds"], label + ".abnormal_persistent_rounds", 1, None)
    _require_int_in_range(guardrails["waveform_severe_flag_threshold"], label + ".waveform_severe_flag_threshold", 1, None)
    return guardrails


def validate_round_result(result):
    _require_dict(result, "round_result")
    _require_no_additional_properties(
        result,
        (
            "mode",
            "batch_id",
            "round_index",
            "candidate_pid",
            "baseline_pid",
            "a_score",
            "b_score",
            "combined_score",
            "left_score",
            "right_score",
            "band_scores",
            "stage_reached",
            "overshoot_flag",
            "persistent_overshoot_flag",
            "speed_drop_flag",
            "stop_clean_flag",
            "pwm_saturation_ratio",
            "waveform_path",
            "waveform_digest",
            "result_path",
            "timestamp",
            "trial_name",
            "segments_ms",
        ),
        "round_result",
    )
    _require_keys(
        result,
        (
            "mode",
            "batch_id",
            "round_index",
            "candidate_pid",
            "baseline_pid",
            "a_score",
            "b_score",
            "combined_score",
            "band_scores",
            "stage_reached",
            "overshoot_flag",
            "persistent_overshoot_flag",
            "speed_drop_flag",
            "stop_clean_flag",
            "pwm_saturation_ratio",
            "waveform_path",
            "waveform_digest",
            "result_path",
            "timestamp",
        ),
        "round_result",
    )

    _require_enum(result["mode"], ROUND_RESULT_MODE_VALUES, "round_result.mode")
    _require_string(result["batch_id"], "round_result.batch_id")
    if not re.match(r"^(air|ground)_[0-9]{4}$", result["batch_id"]):
        _error("round_result.batch_id has invalid format")
    if result["mode"] == "air-dual-step" and not result["batch_id"].startswith("air_"):
        _error("round_result.batch_id prefix must match mode")
    if result["mode"] == "ground-dual-step" and not result["batch_id"].startswith("ground_"):
        _error("round_result.batch_id prefix must match mode")
    _require_int_in_range(result["round_index"], "round_result.round_index", 1, None)
    _validate_pid_bundle(result["candidate_pid"], "round_result.candidate_pid", require_nonnegative=True)
    _validate_pid_bundle(result["baseline_pid"], "round_result.baseline_pid", require_nonnegative=True)
    _require_finite_number(result["a_score"], "round_result.a_score")
    _require_finite_number(result["b_score"], "round_result.b_score")
    _require_finite_number(result["combined_score"], "round_result.combined_score")
    if result["mode"] == "air-dual-step":
        if "left_score" not in result or "right_score" not in result:
            _error("round_result air-dual-step payload must include left_score and right_score")
    if "left_score" in result:
        _require_finite_number(result["left_score"], "round_result.left_score")
    if "right_score" in result:
        _require_finite_number(result["right_score"], "round_result.right_score")
    _validate_band_scores_dict(result["band_scores"], "round_result.band_scores", True)
    _require_string(result["stage_reached"], "round_result.stage_reached", 1, None)
    _require_int_in_range(result["overshoot_flag"], "round_result.overshoot_flag", 0, 1)
    _require_int_in_range(result["persistent_overshoot_flag"], "round_result.persistent_overshoot_flag", 0, 1)
    _require_int_in_range(result["speed_drop_flag"], "round_result.speed_drop_flag", 0, 1)
    _require_int_in_range(result["stop_clean_flag"], "round_result.stop_clean_flag", 0, 1)
    _require_finite_number(result["pwm_saturation_ratio"], "round_result.pwm_saturation_ratio", 0.0, 1.0)
    _require_string(result["waveform_path"], "round_result.waveform_path", 1, None)
    _require_string(result["result_path"], "round_result.result_path", 1, None)
    _require_string(result["timestamp"], "round_result.timestamp", 1, None)

    waveform_digest = result["waveform_digest"]
    _require_dict(waveform_digest, "round_result.waveform_digest")
    _require_no_additional_properties(
        waveform_digest,
        ("tail_jitter", "peak_windows"),
        "round_result.waveform_digest",
    )
    _require_keys(
        waveform_digest,
        ("tail_jitter", "peak_windows"),
        "round_result.waveform_digest",
    )
    _require_finite_number(waveform_digest["tail_jitter"], "round_result.waveform_digest.tail_jitter", 0.0, None)
    if not isinstance(waveform_digest["peak_windows"], list):
        _error("round_result.waveform_digest.peak_windows must be an array")
    for item_index, peak_window in enumerate(waveform_digest["peak_windows"]):
        peak_label = "round_result.waveform_digest.peak_windows[{0}]".format(item_index)
        _require_dict(peak_window, peak_label)
        _require_no_additional_properties(peak_window, ("run_index", "peak_speed"), peak_label)
        _require_keys(peak_window, ("run_index", "peak_speed"), peak_label)
        _require_int_in_range(peak_window["run_index"], peak_label + ".run_index", 1, None)
        _require_finite_number(peak_window["peak_speed"], peak_label + ".peak_speed", 0.0, None)

    if "trial_name" in result:
        _require_string(result["trial_name"], "round_result.trial_name", 1, None)
    if "segments_ms" in result:
        if not isinstance(result["segments_ms"], list):
            _error("round_result.segments_ms must be an array")
        for item_index, segment in enumerate(result["segments_ms"]):
            segment_label = "round_result.segments_ms[{0}]".format(item_index)
            if not isinstance(segment, list) or len(segment) != 2:
                _error(segment_label + " must contain exactly 2 items")
            _require_finite_number(segment[0], segment_label + "[0]")
            _require_int_in_range(segment[1], segment_label + "[1]", 1, None)

    return result


def validate_decision_context(context):
    _require_dict(context, "decision_context")
    allowed_keys = (
        "schema_version",
        "stage_context",
        "pid_anchors",
        "recent_rounds",
        "waveform_summary",
        "runtime_guardrails",
        "recovery_state",
        "advisory_hints",
    )
    _require_no_additional_properties(context, allowed_keys, "decision_context")
    _require_keys(context, allowed_keys, "decision_context")

    _require_int_in_range(context["schema_version"], "decision_context.schema_version", SCHEMA_VERSION, SCHEMA_VERSION)

    stage_context = context["stage_context"]
    _require_dict(stage_context, "decision_context.stage_context")
    _require_no_additional_properties(
        stage_context,
        (
            "stage_name",
            "batch_id",
            "round_index",
            "batch_size",
            "current_status",
            "score_direction",
            "waveform_role",
            "disallowed_actions",
        ),
        "decision_context.stage_context",
    )
    _require_keys(
        stage_context,
        (
            "stage_name",
            "batch_id",
            "round_index",
            "batch_size",
            "current_status",
            "score_direction",
            "waveform_role",
            "disallowed_actions",
        ),
        "decision_context.stage_context",
    )
    _require_enum(stage_context["stage_name"], STAGE_NAME_VALUES, "decision_context.stage_context.stage_name")
    _require_string(stage_context["batch_id"], "decision_context.stage_context.batch_id")
    if not re.match(r"^(air|ground)_[0-9]{4}$", stage_context["batch_id"]):
        _error("decision_context.stage_context.batch_id has invalid format")
    if stage_context["stage_name"] == "air_dual" and not stage_context["batch_id"].startswith("air_"):
        _error("decision_context.stage_context.batch_id prefix must match stage_name")
    if stage_context["stage_name"] == "ground_dual" and not stage_context["batch_id"].startswith("ground_"):
        _error("decision_context.stage_context.batch_id prefix must match stage_name")
    _require_int_in_range(stage_context["round_index"], "decision_context.stage_context.round_index", 1, 10)
    _require_int_in_range(stage_context["batch_size"], "decision_context.stage_context.batch_size", 10, 10)
    _require_enum(stage_context["current_status"], STATUS_VALUES, "decision_context.stage_context.current_status")
    _require_enum(stage_context["score_direction"], SCORE_DIRECTION_VALUES, "decision_context.stage_context.score_direction")
    _require_enum(stage_context["waveform_role"], WAVEFORM_ROLE_VALUES, "decision_context.stage_context.waveform_role")
    if not isinstance(stage_context["disallowed_actions"], list):
        _error("decision_context.stage_context.disallowed_actions must be an array")
    for action in stage_context["disallowed_actions"]:
        _require_string(action, "decision_context.stage_context.disallowed_actions[]")
        _require_enum(action, DISALLOWED_ACTION_VALUES, "decision_context.stage_context.disallowed_actions[]")

    pid_anchors = context["pid_anchors"]
    _require_dict(pid_anchors, "decision_context.pid_anchors")
    _require_no_additional_properties(
        pid_anchors,
        (
            "batch_start_pid",
            "baseline_pid",
            "current_batch_best_pid",
            "current_batch_best_score",
            "historical_stage_best_pid",
            "historical_stage_best_score",
            "last_batch_best",
            "seed_pi",
        ),
        "decision_context.pid_anchors",
    )
    _require_keys(
        pid_anchors,
        (
            "batch_start_pid",
            "baseline_pid",
            "current_batch_best_pid",
            "current_batch_best_score",
            "historical_stage_best_pid",
            "historical_stage_best_score",
            "last_batch_best",
            "seed_pi",
        ),
        "decision_context.pid_anchors",
    )
    _validate_pid_bundle(pid_anchors["batch_start_pid"], "decision_context.pid_anchors.batch_start_pid")
    _validate_pid_bundle(pid_anchors["baseline_pid"], "decision_context.pid_anchors.baseline_pid")
    if pid_anchors["current_batch_best_pid"] is not None:
        _validate_pid_bundle(pid_anchors["current_batch_best_pid"], "decision_context.pid_anchors.current_batch_best_pid")
    if pid_anchors["historical_stage_best_pid"] is not None:
        _validate_pid_bundle(pid_anchors["historical_stage_best_pid"], "decision_context.pid_anchors.historical_stage_best_pid")
    if pid_anchors["last_batch_best"] is not None:
        _validate_pid_bundle(pid_anchors["last_batch_best"], "decision_context.pid_anchors.last_batch_best")
    if pid_anchors["seed_pi"] is not None:
        _validate_pid_bundle(pid_anchors["seed_pi"], "decision_context.pid_anchors.seed_pi")
    if pid_anchors["current_batch_best_score"] is not None:
        _require_finite_number(pid_anchors["current_batch_best_score"], "decision_context.pid_anchors.current_batch_best_score")
    if pid_anchors["historical_stage_best_score"] is not None:
        _require_finite_number(pid_anchors["historical_stage_best_score"], "decision_context.pid_anchors.historical_stage_best_score")

    recent_rounds = context["recent_rounds"]
    if not isinstance(recent_rounds, list):
        _error("decision_context.recent_rounds must be an array")
    if len(recent_rounds) > 5:
        _error("decision_context.recent_rounds must contain at most 5 items")

    previous_round_index = None
    for item_index, round_item in enumerate(recent_rounds):
        label = "decision_context.recent_rounds[{0}]".format(item_index)
        _require_dict(round_item, label)
        _require_no_additional_properties(
            round_item,
            (
                "round_index",
                "candidate_pid",
                "combined_score",
                "a_score",
                "b_score",
                "left_score",
                "right_score",
                "band_scores",
                "overshoot_flag",
                "persistent_overshoot_flag",
                "speed_drop_flag",
                "stop_clean_flag",
                "pwm_saturation_ratio",
                "current_limit_or_headroom_flag",
                "dominant_issue",
                "delta_vs_previous_combined",
                "delta_vs_batch_best_combined",
                "left_right_gap",
                "score_trend",
                "plateau_detected",
                "decision_hints",
                "advisory_only",
                "result_path",
                "waveform_path",
            ),
            label,
        )
        _require_keys(
            round_item,
            (
                "round_index",
                "candidate_pid",
                "combined_score",
                "a_score",
                "b_score",
                "left_score",
                "right_score",
                "band_scores",
                "overshoot_flag",
                "persistent_overshoot_flag",
                "speed_drop_flag",
                "stop_clean_flag",
                "pwm_saturation_ratio",
                "current_limit_or_headroom_flag",
                "dominant_issue",
                "delta_vs_previous_combined",
                "delta_vs_batch_best_combined",
                "left_right_gap",
                "score_trend",
                "plateau_detected",
                "decision_hints",
                "advisory_only",
                "result_path",
                "waveform_path",
            ),
            label,
        )
        _require_int_in_range(round_item["round_index"], label + ".round_index", 1, 10)
        if round_item["round_index"] >= stage_context["round_index"]:
            _error("decision_context.recent_rounds must stay before the current round")
        if previous_round_index is not None and round_item["round_index"] != previous_round_index + 1:
            _error("decision_context.recent_rounds must be consecutive")
        previous_round_index = round_item["round_index"]

        _validate_pid_bundle(round_item["candidate_pid"], label + ".candidate_pid")
        for score_key in ("combined_score", "a_score", "b_score", "left_score", "right_score", "left_right_gap"):
            _require_finite_number(round_item[score_key], label + "." + score_key)
        _require_finite_number(round_item["pwm_saturation_ratio"], label + ".pwm_saturation_ratio", 0.0, 1.0)
        for flag_key in (
            "overshoot_flag",
            "persistent_overshoot_flag",
            "speed_drop_flag",
            "stop_clean_flag",
            "current_limit_or_headroom_flag",
            "plateau_detected",
        ):
            _require_bool(round_item[flag_key], label + "." + flag_key)
        _require_true(round_item["advisory_only"], label + ".advisory_only")
        if round_item["dominant_issue"] is not None:
            _require_string(round_item["dominant_issue"], label + ".dominant_issue")
        if round_item["delta_vs_previous_combined"] is not None:
            _require_finite_number(round_item["delta_vs_previous_combined"], label + ".delta_vs_previous_combined")
        if round_item["delta_vs_batch_best_combined"] is not None:
            _require_finite_number(round_item["delta_vs_batch_best_combined"], label + ".delta_vs_batch_best_combined")
        if round_item["score_trend"] is not None:
            _require_enum(round_item["score_trend"], ("improving", "worsening", "flat"), label + ".score_trend")
        if not isinstance(round_item["decision_hints"], list):
            _error(label + ".decision_hints must be an array")
        for hint in round_item["decision_hints"]:
            _require_string(hint, label + ".decision_hints[]")
        _require_string(round_item["result_path"], label + ".result_path")
        if round_item["waveform_path"] is not None:
            _require_string(round_item["waveform_path"], label + ".waveform_path")

        band_scores = round_item["band_scores"]
        _require_dict(band_scores, label + ".band_scores")
        _require_no_additional_properties(band_scores, ("low", "mid", "high", "top"), label + ".band_scores")
        _require_keys(band_scores, ("low", "mid", "high", "top"), label + ".band_scores")
        for band_name in ("low", "mid", "high", "top"):
            if band_scores[band_name] is not None:
                _require_finite_number(band_scores[band_name], label + ".band_scores." + band_name)

    if recent_rounds:
        expected_last_round = stage_context["round_index"] - 1
        if recent_rounds[-1]["round_index"] != expected_last_round:
            _error("decision_context.recent_rounds tail must match stage_context.round_index - 1")

    waveform_summary = context["waveform_summary"]
    _require_dict(waveform_summary, "decision_context.waveform_summary")
    _require_no_additional_properties(
        waveform_summary,
        ("available", "waveform_digest", "waveform_flags"),
        "decision_context.waveform_summary",
    )
    _require_keys(
        waveform_summary,
        ("available", "waveform_digest", "waveform_flags"),
        "decision_context.waveform_summary",
    )
    _require_bool(waveform_summary["available"], "decision_context.waveform_summary.available")
    waveform_digest = waveform_summary["waveform_digest"]
    _require_dict(waveform_digest, "decision_context.waveform_summary.waveform_digest")
    _require_no_additional_properties(
        waveform_digest,
        ("tail_jitter", "peak_windows", "settling_tail_shape", "stop_tail_residual", "oscillation_hint"),
        "decision_context.waveform_summary.waveform_digest",
    )
    _require_keys(
        waveform_digest,
        ("tail_jitter", "peak_windows", "settling_tail_shape", "stop_tail_residual", "oscillation_hint"),
        "decision_context.waveform_summary.waveform_digest",
    )
    if waveform_digest["tail_jitter"] is not None:
        _require_finite_number(waveform_digest["tail_jitter"], "decision_context.waveform_summary.waveform_digest.tail_jitter")
    if waveform_digest["peak_windows"] is not None:
        _require_finite_number(
            waveform_digest["peak_windows"],
            "decision_context.waveform_summary.waveform_digest.peak_windows",
            0.0,
            None,
        )
    if waveform_digest["settling_tail_shape"] is not None:
        _require_string(waveform_digest["settling_tail_shape"], "decision_context.waveform_summary.waveform_digest.settling_tail_shape")
    if waveform_digest["stop_tail_residual"] is not None:
        _require_finite_number(waveform_digest["stop_tail_residual"], "decision_context.waveform_summary.waveform_digest.stop_tail_residual")
    if waveform_digest["oscillation_hint"] is not None:
        _require_string(waveform_digest["oscillation_hint"], "decision_context.waveform_summary.waveform_digest.oscillation_hint")

    waveform_flags = waveform_summary["waveform_flags"]
    _require_dict(waveform_flags, "decision_context.waveform_summary.waveform_flags")
    _require_no_additional_properties(
        waveform_flags,
        ("looks_noisy", "looks_underdamped", "looks_saturated", "looks_measurement_limited"),
        "decision_context.waveform_summary.waveform_flags",
    )
    _require_keys(
        waveform_flags,
        ("looks_noisy", "looks_underdamped", "looks_saturated", "looks_measurement_limited"),
        "decision_context.waveform_summary.waveform_flags",
    )
    for key in ("looks_noisy", "looks_underdamped", "looks_saturated", "looks_measurement_limited"):
        _require_bool(waveform_flags[key], "decision_context.waveform_summary.waveform_flags.{0}".format(key))

    runtime_guardrails = context["runtime_guardrails"]
    validate_runtime_guardrails(runtime_guardrails, "decision_context.runtime_guardrails")

    recovery_state = context["recovery_state"]
    _require_dict(recovery_state, "decision_context.recovery_state")
    _require_no_additional_properties(
        recovery_state,
        ("must_recover", "high_risk_round_seen", "recovery_reason"),
        "decision_context.recovery_state",
    )
    _require_keys(
        recovery_state,
        ("must_recover", "high_risk_round_seen", "recovery_reason"),
        "decision_context.recovery_state",
    )
    _require_bool(recovery_state["must_recover"], "decision_context.recovery_state.must_recover")
    _require_bool(recovery_state["high_risk_round_seen"], "decision_context.recovery_state.high_risk_round_seen")
    if recovery_state["recovery_reason"] is not None:
        _require_enum(
            recovery_state["recovery_reason"],
            ("score_regression", "high_risk_failure", "manual_resume"),
            "decision_context.recovery_state.recovery_reason",
        )

    advisory_hints = context["advisory_hints"]
    _require_dict(advisory_hints, "decision_context.advisory_hints")
    _require_no_additional_properties(advisory_hints, ("search_phase", "advisory_only"), "decision_context.advisory_hints")
    _require_keys(advisory_hints, ("search_phase", "advisory_only"), "decision_context.advisory_hints")
    if advisory_hints["search_phase"] is not None:
        _require_enum(advisory_hints["search_phase"], ("explore", "shrink", "confirm"), "decision_context.advisory_hints.search_phase")
    _require_true(advisory_hints["advisory_only"], "decision_context.advisory_hints.advisory_only")

    return context


def validate_decision_request_payload(payload, batch_id, round_index):
    _require_dict(payload, "decision_request_payload")
    _require_no_additional_properties(payload, ("request_id", "context", "retry_counters"), "decision_request_payload")
    _require_keys(payload, ("request_id", "context", "retry_counters"), "decision_request_payload")
    _require_string(batch_id, "decision_request_payload.batch_id")
    if not re.match(r"^(air|ground)_[0-9]{4}$", batch_id):
        _error("decision_request_payload.batch_id has invalid format")
    _require_string(payload["request_id"], "decision_request_payload.request_id")
    _require_int_in_range(round_index, "decision_request_payload.round_index", 1, 10)

    expected_request_id = "{0}_r{1:02d}".format(batch_id, int(round_index))
    if not REQUEST_ID_RE.match(payload["request_id"]):
        _error("decision_request_payload.request_id has invalid format")
    if payload["request_id"] != expected_request_id:
        _error("decision_request_payload.request_id does not match batch_id/round_index")

    validate_decision_context(payload["context"])
    stage_context = payload["context"]["stage_context"]
    if stage_context["batch_id"] != batch_id or stage_context["round_index"] != round_index:
        _error("decision_request_payload.context does not match batch_id/round_index")

    retry_counters = payload["retry_counters"]
    _require_dict(retry_counters, "decision_request_payload.retry_counters")
    _require_no_additional_properties(retry_counters, ("decision_errors_used", "timeouts_used"), "decision_request_payload.retry_counters")
    _require_keys(retry_counters, ("decision_errors_used", "timeouts_used"), "decision_request_payload.retry_counters")
    runtime_guardrails = payload["context"]["runtime_guardrails"]
    _require_int_in_range(
        retry_counters["decision_errors_used"],
        "decision_request_payload.retry_counters.decision_errors_used",
        0,
        runtime_guardrails["decision_retry_budget"],
    )
    _require_int_in_range(
        retry_counters["timeouts_used"],
        "decision_request_payload.retry_counters.timeouts_used",
        0,
        runtime_guardrails["timeout_retry_budget"],
    )

    return payload


def validate_llm_decision(decision, guardrails):
    _require_dict(decision, "llm_decision")
    allowed_keys = (
        "schema_version",
        "candidate_pid",
        "decision_summary",
        "primary_reason",
        "supporting_signals",
        "decision_mode",
        "base_reference",
        "expected_outcome",
        "confidence",
        "risk_level",
        "needs_waveform_review",
        "batch_end_recommendation_if_no_improve",
    )
    _require_no_additional_properties(decision, allowed_keys, "llm_decision")
    _require_keys(decision, allowed_keys, "llm_decision")

    _require_int_in_range(decision["schema_version"], "llm_decision.schema_version", SCHEMA_VERSION, SCHEMA_VERSION)

    limits = _resolve_pid_limits(guardrails, "llm_decision.runtime_guardrails", True)
    precision = _resolve_precision(guardrails, "llm_decision.runtime_guardrails")
    _validate_pid_bundle(decision["candidate_pid"], "llm_decision.candidate_pid", limits, require_nonnegative=True)
    _validate_pid_bundle_precision(decision["candidate_pid"], "llm_decision.candidate_pid", precision)
    _require_string(decision["decision_summary"], "llm_decision.decision_summary", 1, 400)
    _require_enum(decision["primary_reason"], PRIMARY_REASON_VALUES, "llm_decision.primary_reason")
    if not isinstance(decision["supporting_signals"], list):
        _error("llm_decision.supporting_signals must be an array")
    if len(decision["supporting_signals"]) < 1 or len(decision["supporting_signals"]) > 12:
        _error("llm_decision.supporting_signals must contain 1..12 items")
    for signal in decision["supporting_signals"]:
        _require_string(signal, "llm_decision.supporting_signals[]")
    _require_enum(decision["decision_mode"], DECISION_MODE_VALUES, "llm_decision.decision_mode")
    _require_enum(decision["base_reference"], BASE_REFERENCE_VALUES, "llm_decision.base_reference")
    _require_enum(decision["expected_outcome"], EXPECTED_OUTCOME_VALUES, "llm_decision.expected_outcome")
    _require_enum(decision["confidence"], CONFIDENCE_VALUES, "llm_decision.confidence")
    _require_enum(decision["risk_level"], RISK_LEVEL_VALUES, "llm_decision.risk_level")
    _require_bool(decision["needs_waveform_review"], "llm_decision.needs_waveform_review")
    _require_enum(
        decision["batch_end_recommendation_if_no_improve"],
        RECOMMENDATION_VALUES,
        "llm_decision.batch_end_recommendation_if_no_improve",
    )

    return decision

import unittest

from project.speed_loop_autotune.host import llm_decision_contract


def _make_pid_bundle(kp=10.0, ki=2.0, kd=0.0):
    return {
        "left": {"kp": float(kp), "ki": float(ki), "kd": float(kd)},
        "right": {"kp": float(kp), "ki": float(ki), "kd": float(kd)},
    }


def _make_context():
    return {
        "schema_version": 1,
        "stage_context": {
            "stage_name": "air_dual",
            "batch_id": "air_0001",
            "round_index": 3,
            "batch_size": 10,
            "current_status": "running",
            "score_direction": "lower_is_better",
            "waveform_role": "secondary_evidence",
            "disallowed_actions": ["save", "enter_ground", "continue_air", "continue_ground", "stop_air", "stop_without_save"],
        },
        "pid_anchors": {
            "batch_start_pid": _make_pid_bundle(100.0, 20.0),
            "baseline_pid": _make_pid_bundle(90.0, 18.0),
            "current_batch_best_pid": None,
            "current_batch_best_score": None,
            "historical_stage_best_pid": None,
            "historical_stage_best_score": None,
            "last_batch_best": None,
            "seed_pi": _make_pid_bundle(88.0, 17.0),
        },
        "recent_rounds": [
            {
                "round_index": 1,
                "candidate_pid": _make_pid_bundle(101.0, 20.5),
                "combined_score": 12.0,
                "a_score": 12.0,
                "b_score": 12.1,
                "left_score": 11.8,
                "right_score": 12.2,
                "band_scores": {"low": 12.0, "mid": 12.0, "high": 12.0, "top": 12.0},
                "overshoot_flag": False,
                "persistent_overshoot_flag": False,
                "speed_drop_flag": False,
                "stop_clean_flag": True,
                "pwm_saturation_ratio": 0.0,
                "current_limit_or_headroom_flag": False,
                "dominant_issue": None,
                "delta_vs_previous_combined": None,
                "delta_vs_batch_best_combined": None,
                "left_right_gap": 0.0,
                "score_trend": None,
                "plateau_detected": False,
                "decision_hints": [],
                "advisory_only": True,
                "result_path": "logs/round_01.json",
                "waveform_path": None,
            },
            {
                "round_index": 2,
                "candidate_pid": _make_pid_bundle(102.0, 20.5),
                "combined_score": 11.0,
                "a_score": 11.0,
                "b_score": 11.1,
                "left_score": 10.8,
                "right_score": 11.2,
                "band_scores": {"low": 11.0, "mid": 11.0, "high": 11.0, "top": 11.0},
                "overshoot_flag": False,
                "persistent_overshoot_flag": False,
                "speed_drop_flag": False,
                "stop_clean_flag": True,
                "pwm_saturation_ratio": 0.0,
                "current_limit_or_headroom_flag": False,
                "dominant_issue": None,
                "delta_vs_previous_combined": -1.0,
                "delta_vs_batch_best_combined": -1.0,
                "left_right_gap": 0.0,
                "score_trend": "improving",
                "plateau_detected": False,
                "decision_hints": ["keep exploring"],
                "advisory_only": True,
                "result_path": "logs/round_02.json",
                "waveform_path": None,
            },
        ],
        "waveform_summary": {
            "available": True,
            "waveform_digest": {
                "tail_jitter": 0.1,
                "peak_windows": 1,
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
        },
        "runtime_guardrails": {
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
        },
        "recovery_state": {
            "must_recover": False,
            "high_risk_round_seen": False,
            "recovery_reason": None,
        },
        "advisory_hints": {
            "search_phase": "explore",
            "advisory_only": True,
        },
    }


def _make_request_payload():
    return {
        "request_id": "air_0001_r03",
        "context": _make_context(),
        "retry_counters": {
            "decision_errors_used": 0,
            "timeouts_used": 0,
        },
    }


def _make_decision():
    return {
        "schema_version": 1,
        "candidate_pid": _make_pid_bundle(105.0, 21.0),
        "decision_summary": "move a little faster",
        "primary_reason": "slow_response",
        "supporting_signals": ["recent rounds are stable"],
        "decision_mode": "local_refine",
        "base_reference": "last_round",
        "expected_outcome": "improve_response",
        "confidence": "medium",
        "risk_level": "low",
        "needs_waveform_review": False,
        "batch_end_recommendation_if_no_improve": "continue_air",
    }


def _make_runtime_guardrails():
    return _make_context()["runtime_guardrails"]


def _make_waveform_digest():
    return {
        "tail_jitter": 0.1,
        "peak_windows": [{"run_index": 1, "peak_speed": 25.0}],
    }


def _make_round_result(mode="air-dual-step", batch_id="air_0001", round_index=2):
    stage_name = "air_dual"
    result = {
        "mode": mode,
        "batch_id": batch_id,
        "round_index": int(round_index),
        "candidate_pid": _make_pid_bundle(101.0, 20.5),
        "baseline_pid": _make_pid_bundle(100.0, 20.0),
        "a_score": 11.0,
        "b_score": 11.2,
        "combined_score": 11.1,
        "band_scores": {"low": 1.0, "mid": 2.0, "high": 3.0, "top": 4.0},
        "stage_reached": "step_completed",
        "overshoot_flag": 0,
        "persistent_overshoot_flag": 0,
        "speed_drop_flag": 0,
        "stop_clean_flag": 1,
        "pwm_saturation_ratio": 0.0,
        "waveform_path": "logs/agent_waveforms/air_dual/air_0001_r02.jsonl",
        "waveform_digest": _make_waveform_digest(),
        "result_path": "logs/agent_rounds/air_dual/air_0001_r02.json",
        "timestamp": "2026-04-02 10:00:00",
    }
    if mode == "ground-dual-step":
        stage_name = "ground_dual"
    else:
        result["left_score"] = 10.8
        result["right_score"] = 11.4
    result["waveform_path"] = "logs/agent_waveforms/{0}/{1}_r{2:02d}.jsonl".format(stage_name, batch_id, int(round_index))
    result["result_path"] = "logs/agent_rounds/{0}/{1}_r{2:02d}.json".format(stage_name, batch_id, int(round_index))
    return result


class DecisionContractTests(unittest.TestCase):
    def test_validate_decision_context_accepts_aligned_recent_rounds(self):
        context = _make_context()

        validated = llm_decision_contract.validate_decision_context(context)

        self.assertEqual(validated["stage_context"]["round_index"], 3)
        self.assertEqual(validated["recent_rounds"][-1]["round_index"], 2)

    def test_validate_decision_context_rejects_misaligned_recent_rounds_tail(self):
        context = _make_context()
        context["recent_rounds"][-1]["round_index"] = 1

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_recent_rounds_longer_than_five(self):
        context = _make_context()
        context["recent_rounds"] = [
            dict(context["recent_rounds"][0], round_index=1),
            dict(context["recent_rounds"][0], round_index=2),
            dict(context["recent_rounds"][0], round_index=3),
            dict(context["recent_rounds"][0], round_index=4),
            dict(context["recent_rounds"][0], round_index=5),
            dict(context["recent_rounds"][0], round_index=6),
        ]
        context["stage_context"]["round_index"] = 7

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_recent_rounds_with_gap(self):
        context = _make_context()
        context["stage_context"]["round_index"] = 4
        context["recent_rounds"] = [
            dict(context["recent_rounds"][0], round_index=1),
            dict(context["recent_rounds"][1], round_index=3),
        ]

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_recent_round_including_current_or_future(self):
        context = _make_context()
        context["recent_rounds"][-1]["round_index"] = 3

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_additional_properties(self):
        context = _make_context()
        context["unexpected"] = True

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_recent_round_non_true_advisory_only(self):
        context = _make_context()
        context["recent_rounds"][0]["advisory_only"] = False

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_advisory_hints_non_true_advisory_only(self):
        context = _make_context()
        context["advisory_hints"]["advisory_only"] = False

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_non_true_runtime_constants(self):
        context = _make_context()
        context["runtime_guardrails"]["allow_large_jump"] = False

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

        context = _make_context()
        context["runtime_guardrails"]["single_candidate_only"] = False

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_invalid_precision_structure(self):
        context = _make_context()
        del context["runtime_guardrails"]["precision"]["kd_decimals"]

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

        context = _make_context()
        context["runtime_guardrails"]["precision"]["kp_decimals"] = 7

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_invalid_absolute_pid_limits_shape(self):
        context = _make_context()
        context["runtime_guardrails"]["absolute_pid_limits"] = {"kp_min": 0.0}

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_reversed_absolute_pid_limits(self):
        context = _make_context()
        context["runtime_guardrails"]["absolute_pid_limits"]["kp_min"] = 10.0
        context["runtime_guardrails"]["absolute_pid_limits"]["kp_max"] = 5.0

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_allows_pid_anchors_outside_default_limits(self):
        context = _make_context()
        context["pid_anchors"]["batch_start_pid"]["left"]["kp"] = 600.0
        context["pid_anchors"]["batch_start_pid"]["right"]["kp"] = 600.0

        validated = llm_decision_contract.validate_decision_context(context)

        self.assertEqual(validated["pid_anchors"]["batch_start_pid"]["left"]["kp"], 600.0)

    def test_validate_decision_context_rejects_boolean_schema_version(self):
        context = _make_context()
        context["schema_version"] = True

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_stage_and_batch_prefix_mismatch(self):
        context = _make_context()
        context["stage_context"]["stage_name"] = "ground_dual"

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_accepts_numeric_peak_windows(self):
        context = _make_context()
        context["waveform_summary"]["waveform_digest"]["peak_windows"] = 1.5

        validated = llm_decision_contract.validate_decision_context(context)

        self.assertEqual(validated["waveform_summary"]["waveform_digest"]["peak_windows"], 1.5)

    def test_validate_decision_context_rejects_invalid_waveform_summary_shape(self):
        context = _make_context()
        del context["waveform_summary"]["waveform_flags"]["looks_noisy"]

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

        context = _make_context()
        context["waveform_summary"]["waveform_digest"]["unexpected"] = True

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_decision_context_rejects_invalid_runtime_guardrail_ranges(self):
        context = _make_context()
        context["runtime_guardrails"]["worker_timeout_seconds"] = 0

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

        context = _make_context()
        context["runtime_guardrails"]["decision_retry_budget"] = -1

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_context(context)

    def test_validate_runtime_guardrails_accepts_valid_payload(self):
        guardrails = _make_runtime_guardrails()

        validated = llm_decision_contract.validate_runtime_guardrails(guardrails)

        self.assertEqual(validated["worker_timeout_seconds"], 20)

    def test_validate_runtime_guardrails_rejects_missing_required_field(self):
        guardrails = _make_runtime_guardrails()
        del guardrails["precision"]

        with self.assertRaisesRegex(ValueError, "runtime_guardrails is missing required property: precision"):
            llm_decision_contract.validate_runtime_guardrails(guardrails)

    def test_validate_runtime_guardrails_rejects_invalid_constant_flags(self):
        guardrails = _make_runtime_guardrails()
        guardrails["single_candidate_only"] = False

        with self.assertRaisesRegex(ValueError, "runtime_guardrails.single_candidate_only"):
            llm_decision_contract.validate_runtime_guardrails(guardrails)

    def test_validate_round_result_accepts_air_worker_minimum_payload(self):
        result = _make_round_result()

        validated = llm_decision_contract.validate_round_result(result)

        self.assertEqual(validated["mode"], "air-dual-step")

    def test_validate_round_result_accepts_ground_optional_fields(self):
        result = _make_round_result(mode="ground-dual-step", batch_id="ground_0001", round_index=4)
        result["trial_name"] = "ground_forward"
        result["segments_ms"] = [[15.0, 200], [25.0, 200]]

        validated = llm_decision_contract.validate_round_result(result)

        self.assertEqual(validated["batch_id"], "ground_0001")

    def test_validate_round_result_rejects_invalid_waveform_digest_shape(self):
        result = _make_round_result()
        result["waveform_digest"] = {"tail_jitter": 0.1}

        with self.assertRaisesRegex(ValueError, "round_result.waveform_digest"):
            llm_decision_contract.validate_round_result(result)

    def test_validate_round_result_rejects_invalid_band_scores_shape(self):
        result = _make_round_result()
        result["band_scores"] = {"low": 1.0}

        with self.assertRaisesRegex(ValueError, "round_result.band_scores"):
            llm_decision_contract.validate_round_result(result)

    def test_validate_round_result_rejects_air_payload_missing_side_scores(self):
        result = _make_round_result()
        del result["left_score"]

        with self.assertRaisesRegex(ValueError, "left_score and right_score"):
            llm_decision_contract.validate_round_result(result)

    def test_validate_decision_request_payload_rejects_request_id_mismatch(self):
        payload = _make_request_payload()
        payload["request_id"] = "air_0001_r04"

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_request_payload(payload, "air_0001", 3)

    def test_validate_decision_request_payload_rejects_missing_required_field(self):
        payload = _make_request_payload()
        del payload["context"]

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_request_payload(payload, "air_0001", 3)

    def test_validate_decision_request_payload_rejects_context_batch_or_round_mismatch(self):
        payload = _make_request_payload()
        payload["context"]["stage_context"]["batch_id"] = "air_0002"

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_request_payload(payload, "air_0001", 3)

        payload = _make_request_payload()
        payload["context"]["stage_context"]["round_index"] = 4

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_request_payload(payload, "air_0001", 3)

    def test_validate_decision_request_payload_rejects_retry_counter_above_budget(self):
        payload = _make_request_payload()
        payload["retry_counters"]["decision_errors_used"] = 3

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_request_payload(payload, "air_0001", 3)

    def test_validate_decision_request_payload_accepts_valid_payload(self):
        payload = _make_request_payload()

        validated = llm_decision_contract.validate_decision_request_payload(payload, "air_0001", 3)

        self.assertEqual(validated["request_id"], "air_0001_r03")

    def test_validate_decision_request_payload_rejects_invalid_batch_id_argument(self):
        payload = _make_request_payload()

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_request_payload(payload, "bad-batch", 3)

    def test_validate_decision_request_payload_rejects_timeout_retry_counter_above_budget(self):
        payload = _make_request_payload()
        payload["retry_counters"]["timeouts_used"] = 2

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_decision_request_payload(payload, "air_0001", 3)

    def test_validate_llm_decision_rejects_non_finite_candidate_pid(self):
        decision = _make_decision()
        decision["candidate_pid"]["left"]["kp"] = float("inf")

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_llm_decision(decision, _make_context()["runtime_guardrails"])

    def test_validate_llm_decision_rejects_unknown_enums(self):
        decision = _make_decision()
        decision["decision_mode"] = "something_else"

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_llm_decision(decision, _make_context()["runtime_guardrails"])

        for key_name, invalid_value in (
            ("primary_reason", "not_real"),
            ("base_reference", "not_real"),
            ("expected_outcome", "not_real"),
            ("confidence", "not_real"),
            ("risk_level", "not_real"),
            ("batch_end_recommendation_if_no_improve", "not_real"),
        ):
            decision = _make_decision()
            decision[key_name] = invalid_value
            with self.assertRaises(ValueError):
                llm_decision_contract.validate_llm_decision(decision, _make_context()["runtime_guardrails"])

    def test_validate_llm_decision_rejects_negative_pid_gain(self):
        decision = _make_decision()
        decision["candidate_pid"]["right"]["ki"] = -1.0

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_llm_decision(decision, _make_context()["runtime_guardrails"])

    def test_validate_llm_decision_rejects_missing_candidate_pid(self):
        decision = _make_decision()
        del decision["candidate_pid"]

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_llm_decision(decision, _make_context()["runtime_guardrails"])

    def test_validate_llm_decision_rejects_candidate_pid_beyond_precision(self):
        decision = _make_decision()
        decision["candidate_pid"]["left"]["kp"] = 105.123

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_llm_decision(decision, _make_context()["runtime_guardrails"])

    def test_validate_llm_decision_accepts_float_with_trailing_zero_at_zero_precision(self):
        decision = _make_decision()
        guardrails = _make_context()["runtime_guardrails"]
        guardrails["precision"] = {"kp_decimals": 0, "ki_decimals": 0, "kd_decimals": 0}
        decision["candidate_pid"] = _make_pid_bundle(105.0, 21.0, 0.0)

        validated = llm_decision_contract.validate_llm_decision(decision, guardrails)

        self.assertEqual(validated["candidate_pid"]["left"]["kp"], 105.0)

    def test_validate_llm_decision_rejects_negative_pid_even_if_guardrails_allow_negative_min(self):
        decision = _make_decision()
        guardrails = _make_context()["runtime_guardrails"]
        guardrails["absolute_pid_limits"]["kp_min"] = -10.0
        decision["candidate_pid"]["left"]["kp"] = -0.5

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_llm_decision(decision, guardrails)

    def test_validate_llm_decision_rejects_boolean_schema_version(self):
        decision = _make_decision()
        decision["schema_version"] = True

        with self.assertRaises(ValueError):
            llm_decision_contract.validate_llm_decision(decision, _make_context()["runtime_guardrails"])

    def test_validate_llm_decision_accepts_valid_payload(self):
        decision = _make_decision()

        validated = llm_decision_contract.validate_llm_decision(decision, _make_context()["runtime_guardrails"])

        self.assertEqual(validated["decision_mode"], "local_refine")

    def test_validate_llm_decision_reports_guardrail_path(self):
        decision = _make_decision()
        guardrails = _make_context()["runtime_guardrails"]
        guardrails["absolute_pid_limits"] = {"kp_min": 0.0}

        with self.assertRaisesRegex(ValueError, "llm_decision.runtime_guardrails.absolute_pid_limits"):
            llm_decision_contract.validate_llm_decision(decision, guardrails)

    def test_validate_llm_decision_rejects_missing_absolute_pid_limits(self):
        decision = _make_decision()
        guardrails = _make_context()["runtime_guardrails"]
        del guardrails["absolute_pid_limits"]

        with self.assertRaisesRegex(ValueError, "llm_decision.runtime_guardrails is missing required property: absolute_pid_limits"):
            llm_decision_contract.validate_llm_decision(decision, guardrails)

    def test_validate_llm_decision_rejects_missing_precision(self):
        decision = _make_decision()
        guardrails = _make_context()["runtime_guardrails"]
        del guardrails["precision"]

        with self.assertRaisesRegex(ValueError, "llm_decision.runtime_guardrails is missing required property: precision"):
            llm_decision_contract.validate_llm_decision(decision, guardrails)


if __name__ == "__main__":
    unittest.main()

import unittest

from project.speed_loop_autotune.host import decision_heuristic
from project.speed_loop_autotune.host import llm_decision_contract


def _make_pid_bundle(left_kp, left_ki, right_kp=None, right_ki=None, left_kd=0.0, right_kd=0.0):
    if right_kp is None:
        right_kp = left_kp
    if right_ki is None:
        right_ki = left_ki
    return {
        "left": {"kp": float(left_kp), "ki": float(left_ki), "kd": float(left_kd)},
        "right": {"kp": float(right_kp), "ki": float(right_ki), "kd": float(right_kd)},
    }


def _make_recent_round(round_index=1):
    return {
        "round_index": int(round_index),
        "candidate_pid": _make_pid_bundle(87.0, 10.88, 99.16, 12.4),
        "combined_score": 1800.0,
        "a_score": 1800.0,
        "b_score": 1800.0,
        "left_score": 2.0,
        "right_score": 2.0,
        "band_scores": {"low": 2.0, "mid": 2.0, "high": 2.0, "top": 2.0},
        "overshoot_flag": False,
        "persistent_overshoot_flag": False,
        "speed_drop_flag": False,
        "stop_clean_flag": True,
        "pwm_saturation_ratio": 0.2,
        "current_limit_or_headroom_flag": False,
        "dominant_issue": None,
        "delta_vs_previous_combined": None,
        "delta_vs_batch_best_combined": 10.0,
        "left_right_gap": 0.0,
        "score_trend": None,
        "plateau_detected": False,
        "decision_hints": [],
        "advisory_only": True,
        "result_path": "logs/round.json",
        "waveform_path": None,
    }


def _make_request():
    return {
        "request_id": "air_0001_r02",
        "context": {
            "schema_version": 1,
            "stage_context": {
                "stage_name": "air_dual",
                "batch_id": "air_0001",
                "round_index": 2,
                "batch_size": 10,
                "current_status": "running",
                "score_direction": "lower_is_better",
                "waveform_role": "secondary_evidence",
                "disallowed_actions": ["save", "enter_ground", "continue_air", "continue_ground", "stop_air", "stop_without_save"],
            },
            "pid_anchors": {
                "batch_start_pid": _make_pid_bundle(87.0, 10.88, 99.16, 12.4),
                "baseline_pid": _make_pid_bundle(87.0, 10.88, 99.16, 12.4),
                "current_batch_best_pid": _make_pid_bundle(87.0, 10.88, 99.16, 12.4),
                "current_batch_best_score": 1771.87,
                "historical_stage_best_pid": _make_pid_bundle(87.0, 10.88, 99.16, 12.4),
                "historical_stage_best_score": 1771.87,
                "last_batch_best": _make_pid_bundle(87.0, 10.88, 99.16, 12.4),
                "seed_pi": _make_pid_bundle(80.0, 9.0, 90.0, 10.0),
            },
            "recent_rounds": [_make_recent_round(1)],
            "waveform_summary": {
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
        },
        "retry_counters": {"decision_errors_used": 0, "timeouts_used": 0},
    }


class DecisionHeuristicTests(unittest.TestCase):
    def test_overshoot_only_moves_kp(self):
        request = _make_request()
        request["context"]["recent_rounds"][-1]["overshoot_flag"] = True
        request["context"]["recent_rounds"][-1]["persistent_overshoot_flag"] = True

        decision = decision_heuristic.build_llm_decision(request)

        self.assertLess(decision["candidate_pid"]["left"]["kp"], 87.0)
        self.assertLess(decision["candidate_pid"]["right"]["kp"], 99.16)
        self.assertEqual(decision["candidate_pid"]["left"]["ki"], 10.88)
        self.assertEqual(decision["candidate_pid"]["right"]["ki"], 12.4)
        self.assertEqual(decision["primary_reason"], "overshoot")
        llm_decision_contract.validate_llm_decision(decision, request["context"]["runtime_guardrails"])

    def test_speed_drop_moves_ki_with_large_step(self):
        request = _make_request()
        request["context"]["recent_rounds"][-1]["speed_drop_flag"] = True
        request["context"]["recent_rounds"][-1]["dominant_issue"] = "slow_response"
        request["context"]["recent_rounds"][-1]["combined_score"] = 1773.0
        request["context"]["recent_rounds"][-1]["delta_vs_batch_best_combined"] = 1.13

        decision = decision_heuristic.build_llm_decision(request)

        self.assertEqual(decision["candidate_pid"]["left"]["kp"], 87.0)
        self.assertEqual(decision["candidate_pid"]["right"]["kp"], 99.16)
        self.assertGreaterEqual(decision["candidate_pid"]["left"]["ki"] - 10.88, 1.0)
        self.assertGreaterEqual(decision["candidate_pid"]["right"]["ki"] - 12.4, 1.0)
        self.assertEqual(decision["primary_reason"], "steady_error")
        llm_decision_contract.validate_llm_decision(decision, request["context"]["runtime_guardrails"])

    def test_mixed_case_moves_kp_and_ki(self):
        request = _make_request()
        request["context"]["recent_rounds"][-1]["overshoot_flag"] = True
        request["context"]["recent_rounds"][-1]["speed_drop_flag"] = True
        request["context"]["recent_rounds"][-1]["combined_score"] = 1774.0
        request["context"]["recent_rounds"][-1]["delta_vs_batch_best_combined"] = 2.13

        decision = decision_heuristic.build_llm_decision(request)

        self.assertLess(decision["candidate_pid"]["left"]["kp"], 87.0)
        self.assertLess(decision["candidate_pid"]["right"]["kp"], 99.16)
        self.assertGreater(decision["candidate_pid"]["left"]["ki"], 10.88)
        self.assertGreater(decision["candidate_pid"]["right"]["ki"], 12.4)
        self.assertEqual(decision["decision_mode"], "local_refine")
        llm_decision_contract.validate_llm_decision(decision, request["context"]["runtime_guardrails"])

    def test_left_right_gap_can_split_ki_change(self):
        request = _make_request()
        request["context"]["recent_rounds"][-1]["speed_drop_flag"] = True
        request["context"]["recent_rounds"][-1]["left_right_gap"] = 0.2
        request["context"]["recent_rounds"][-1]["left_score"] = 2.6
        request["context"]["recent_rounds"][-1]["right_score"] = 2.1

        decision = decision_heuristic.build_llm_decision(request)

        self.assertEqual(decision["decision_mode"], "rebalance_left")
        self.assertGreater(decision["candidate_pid"]["left"]["ki"], 10.88)
        self.assertEqual(decision["candidate_pid"]["right"]["ki"], 12.4)
        llm_decision_contract.validate_llm_decision(decision, request["context"]["runtime_guardrails"])

    def test_regression_rolls_back_to_best(self):
        request = _make_request()
        request["context"]["recent_rounds"][-1]["combined_score"] = 1900.0
        request["context"]["recent_rounds"][-1]["delta_vs_batch_best_combined"] = 128.13

        decision = decision_heuristic.build_llm_decision(request)

        self.assertEqual(decision["decision_mode"], "rollback")
        self.assertEqual(decision["candidate_pid"], request["context"]["pid_anchors"]["current_batch_best_pid"])
        llm_decision_contract.validate_llm_decision(decision, request["context"]["runtime_guardrails"])


if __name__ == "__main__":
    unittest.main()

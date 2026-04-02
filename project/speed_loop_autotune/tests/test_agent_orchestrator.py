import json
import pathlib
import tempfile
import unittest

from project.speed_loop_autotune.host import agent_session
from project.speed_loop_autotune.host import common
from project.speed_loop_autotune.host import llm_decision_contract
from project.speed_loop_autotune.host import vofa_autotune

try:
    from project.speed_loop_autotune.host import agent_orchestrator
except ImportError:
    agent_orchestrator = None


def _pid_pair(kp, ki, kd=0.0):
    return {
        "left": {"kp": float(kp), "ki": float(ki), "kd": float(kd)},
        "right": {"kp": float(kp), "ki": float(ki), "kd": float(kd)},
    }


def _make_round_result_payload(mode, batch_id, round_index, candidate_pid, baseline_pid, combined_score):
    stage_name = "air_dual"
    result = {
        "mode": mode,
        "batch_id": batch_id,
        "round_index": int(round_index),
        "candidate_pid": candidate_pid,
        "baseline_pid": baseline_pid,
        "a_score": float(combined_score),
        "b_score": float(combined_score),
        "combined_score": float(combined_score),
        "band_scores": {"low": 1.0, "mid": 2.0, "high": 3.0, "top": 4.0},
        "stage_reached": "step_completed",
        "overshoot_flag": 0,
        "persistent_overshoot_flag": 0,
        "speed_drop_flag": 0,
        "stop_clean_flag": 1,
        "pwm_saturation_ratio": 0.0,
        "waveform_path": "logs/agent_waveforms/{0}/{1}_r{2:02d}.jsonl".format(stage_name, batch_id, int(round_index)),
        "waveform_digest": {"tail_jitter": 0.1, "peak_windows": [{"run_index": 1, "peak_speed": 25.0}]},
        "result_path": "logs/agent_rounds/{0}/{1}_r{2:02d}.json".format(stage_name, batch_id, int(round_index)),
        "timestamp": "2026-04-02 10:00:00",
    }
    if mode == common.MODE_GROUND_DUAL_STEP:
        stage_name = "ground_dual"
        result["trial_name"] = "ground_forward"
        result["segments_ms"] = [[15.0, 200], [25.0, 200]]
    else:
        result["left_score"] = float(combined_score) - 0.3
        result["right_score"] = float(combined_score) + 0.3
    result["waveform_path"] = "logs/agent_waveforms/{0}/{1}_r{2:02d}.jsonl".format(stage_name, batch_id, int(round_index))
    result["result_path"] = "logs/agent_rounds/{0}/{1}_r{2:02d}.json".format(stage_name, batch_id, int(round_index))
    return result


class AgentOrchestratorTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir.cleanup)
        self.root = pathlib.Path(self.temp_dir.name)
        self.profile_path = self.root / "logs" / "current_tuning_profile.json"
        self.args = vofa_autotune.build_argument_parser().parse_args(
            [
                "--profile-path",
                str(self.profile_path),
            ]
        )

    def _load_profile(self):
        return common.load_tuning_profile(str(self.profile_path), required=False)

    def _save_profile(self, profile):
        common.save_tuning_profile(profile, str(self.profile_path))

    def _make_stage_runner(self, call_log):
        def _runner(_client, run_args, mode_name, _context):
            profile = self._load_profile()

            if mode_name == common.MODE_PWM_MAP:
                call_log.append("pwm_map")
                profile["shared_targets"] = {
                    "bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0},
                    "default_sequences": {
                        "air_primary": [{"target_speed": 15.0, "hold_ms": 300}],
                        "air_verify": [{"target_speed": 15.0, "hold_ms": 200}],
                        "ground_forward": [{"target_speed": 25.0, "hold_ms": 200}],
                    },
                }
                profile["pwm_map"] = {
                    "left": {"deadzone_pwm": 180},
                    "right": {"deadzone_pwm": 185},
                }
                self._save_profile(profile)
                return {"mode": mode_name}

            if mode_name == common.MODE_PWM_IDENTIFY:
                call_log.append("pwm_identify")
                profile["pwm_identify"] = {
                    "seed_pi": _pid_pair(90.0, 18.0),
                }
                self._save_profile(profile)
                return {"mode": mode_name}

            candidate = common.load_json_dict(run_args.candidate_json)
            baseline = common.load_json_dict(run_args.baseline_json)
            round_score = 50.0 - float(run_args.round_index)
            result = {
                "mode": mode_name,
                "batch_id": run_args.batch_id,
                "round_index": int(run_args.round_index),
                "candidate_pid": candidate,
                "baseline_pid": baseline,
                "a_score": round_score,
                "b_score": round_score + 0.25,
                "combined_score": round_score,
                "band_scores": {"low": round_score, "mid": round_score, "high": round_score, "top": round_score},
                "stage_reached": "step_completed",
                "overshoot_flag": 0,
                "persistent_overshoot_flag": 0,
                "speed_drop_flag": 0,
                "stop_clean_flag": 1,
                "pwm_saturation_ratio": 0.0,
                "waveform_path": pathlib.Path(run_args.waveform_path).as_posix(),
                "waveform_digest": {"tail_jitter": 0.1, "peak_windows": [{"run_index": 1, "peak_speed": 25.0}]},
                "result_path": pathlib.Path(run_args.result_json).as_posix(),
                "timestamp": "2026-04-02 10:00:00",
            }
            if mode_name == common.MODE_AIR_DUAL_STEP:
                result["left_score"] = round_score
                result["right_score"] = round_score + 0.5
            else:
                result["trial_name"] = "ground_forward"
                result["segments_ms"] = [[15.0, 200], [25.0, 200]]

            pathlib.Path(run_args.waveform_path).parent.mkdir(parents=True, exist_ok=True)
            pathlib.Path(run_args.waveform_path).write_text('{"sample_index": 0}\n', encoding="utf-8")
            common.write_json_file(run_args.result_json, result)
            call_log.append("{0}_r{1:02d}".format(mode_name, int(run_args.round_index)))
            return result

        return _runner

    def _make_valid_agent_output(self, decision_request, delta_kp=0.0):
        candidate_pid = decision_request["context"]["pid_anchors"]["current_batch_best_pid"]
        if candidate_pid is None:
            candidate_pid = decision_request["context"]["pid_anchors"]["batch_start_pid"]
        candidate_pid = json.loads(json.dumps(candidate_pid))
        candidate_pid["left"]["kp"] += float(delta_kp)
        candidate_pid["right"]["kp"] += float(delta_kp)
        return json.dumps(
            {
                "schema_version": 1,
                "candidate_pid": candidate_pid,
                "decision_summary": "small local refine",
                "primary_reason": "slow_response",
                "supporting_signals": ["latest scores are stable"],
                "decision_mode": "local_refine",
                "base_reference": "current_batch_best",
                "expected_outcome": "improve_response",
                "confidence": "medium",
                "risk_level": "low",
                "needs_waveform_review": False,
                "batch_end_recommendation_if_no_improve": "continue_air",
            }
        )

    def test_start_or_resume_workflow_generates_valid_decision_request(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        result = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(call_log[0:2], ["pwm_map", "pwm_identify"])
        self.assertEqual(result["workflow_status"], "decision_required")
        self.assertEqual(result["workflow_stage"], "air_dual")
        self.assertIn("decision_request", result)
        llm_decision_contract.validate_decision_request_payload(
            result["decision_request"],
            result["decision_request"]["context"]["stage_context"]["batch_id"],
            result["decision_request"]["context"]["stage_context"]["round_index"],
        )

    def test_submit_agent_response_invalid_json_retries_same_request_id(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        started = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )

        request_id = started["decision_request"]["request_id"]
        retried = agent_orchestrator.submit_agent_response(
            object(),
            self.args,
            request_id,
            "ok",
            "{bad json",
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(retried["workflow_status"], "decision_required")
        self.assertEqual(retried["decision_request"]["request_id"], request_id)
        self.assertEqual(retried["decision_request"]["retry_counters"]["decision_errors_used"], 1)
        self.assertEqual(retried["decision_request"]["retry_counters"]["timeouts_used"], 0)

    def test_start_or_resume_workflow_reuses_existing_pending_request(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        started = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )
        request_id = started["decision_request"]["request_id"]
        retried = agent_orchestrator.submit_agent_response(
            object(),
            self.args,
            request_id,
            "ok",
            "{bad json",
            stage_runner=self._make_stage_runner(call_log),
        )

        resumed = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(resumed["workflow_status"], "decision_required")
        self.assertEqual(resumed["decision_request"]["request_id"], request_id)
        self.assertEqual(resumed["decision_request"]["retry_counters"]["decision_errors_used"], 1)
        self.assertEqual(
            resumed["decision_request"]["retry_counters"],
            retried["decision_request"]["retry_counters"],
        )

    def test_start_or_resume_workflow_carries_recovery_state_into_decision_context(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        profile = agent_session.ensure_agent_profile_defaults({})
        profile["shared_targets"] = {
            "bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0},
        }
        profile["pwm_map"] = {"left": {"deadzone_pwm": 180}, "right": {"deadzone_pwm": 185}}
        profile["pwm_identify"] = {"seed_pi": _pid_pair(90.0, 18.0)}
        profile = agent_session.start_batch(profile, "air_dual", "air_0001", _pid_pair(90.0, 18.0))
        profile = agent_session.mark_recovery_required(profile, "high_risk_failure")
        self._save_profile(profile)

        result = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner([]),
        )

        self.assertEqual(
            result["decision_request"]["context"]["recovery_state"],
            {
                "must_recover": True,
                "high_risk_round_seen": True,
                "recovery_reason": "high_risk_failure",
            },
        )

    def test_submit_agent_response_timeout_uses_separate_budget(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        started = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )

        request_id = started["decision_request"]["request_id"]
        retried = agent_orchestrator.submit_agent_response(
            object(),
            self.args,
            request_id,
            "timeout",
            "",
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(retried["workflow_status"], "decision_required")
        self.assertEqual(retried["decision_request"]["request_id"], request_id)
        self.assertEqual(retried["decision_request"]["retry_counters"]["decision_errors_used"], 0)
        self.assertEqual(retried["decision_request"]["retry_counters"]["timeouts_used"], 1)

        exhausted = agent_orchestrator.submit_agent_response(
            object(),
            self.args,
            request_id,
            "timeout",
            "",
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(exhausted["workflow_status"], "waiting_user")
        self.assertEqual(exhausted["pending_user_action"]["stage"], "air_dual")
        self.assertEqual(
            exhausted["pending_user_action"]["allowed_actions"],
            ["continue_air", "stop_air"],
        )
        profile = self._load_profile()
        self.assertEqual(
            profile["agent_tuning"]["failure_trace"]["type"],
            "decision_timeout_exhausted",
        )
        self.assertEqual(
            profile["agent_tuning"]["failure_trace"]["request_consumed"],
            False,
        )

    def test_submit_agent_response_rejects_consumed_request_id(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        started = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )

        request = started["decision_request"]
        request_id = request["request_id"]
        next_state = agent_orchestrator.submit_agent_response(
            object(),
            self.args,
            request_id,
            "ok",
            self._make_valid_agent_output(request),
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(next_state["workflow_status"], "decision_required")
        self.assertEqual(next_state["decision_request"]["request_id"], "air_0001_r02")

        with self.assertRaisesRegex(RuntimeError, "already been consumed"):
            agent_orchestrator.submit_agent_response(
                object(),
                self.args,
                request_id,
                "ok",
                self._make_valid_agent_output(request),
                stage_runner=self._make_stage_runner(call_log),
            )

    def test_submit_agent_response_updates_last_committed_request_id_on_success(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        started = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )

        request = started["decision_request"]
        next_state = agent_orchestrator.submit_agent_response(
            object(),
            self.args,
            request["request_id"],
            "ok",
            self._make_valid_agent_output(request),
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(next_state["workflow_status"], "decision_required")
        profile = self._load_profile()
        self.assertEqual(
            profile["agent_tuning"]["recovery_state"]["last_committed_request_id"],
            request["request_id"],
        )

    def test_continue_air_after_failure_marks_manual_resume_in_next_request(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        started = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )

        request_id = started["decision_request"]["request_id"]
        waiting = agent_orchestrator.submit_agent_response(
            object(),
            self.args,
            request_id,
            "timeout",
            "",
            stage_runner=self._make_stage_runner(call_log),
        )
        waiting = agent_orchestrator.submit_agent_response(
            object(),
            self.args,
            request_id,
            "timeout",
            "",
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(waiting["workflow_status"], "waiting_user")

        resumed = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            explicit_action="continue_air",
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(resumed["workflow_status"], "decision_required")
        self.assertEqual(resumed["decision_request"]["request_id"], "air_0001_r01")
        self.assertEqual(
            resumed["decision_request"]["context"]["recovery_state"],
            {
                "must_recover": True,
                "high_risk_round_seen": False,
                "recovery_reason": "manual_resume",
            },
        )

    def test_continue_air_after_worker_circuit_break_advances_past_consumed_request(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        started = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )
        request = started["decision_request"]
        request_id = request["request_id"]

        original_append = agent_session.append_decision_trace
        agent_session.append_decision_trace = lambda *_args, **_kwargs: (_ for _ in ()).throw(RuntimeError("trace failed"))
        try:
            with self.assertRaisesRegex(RuntimeError, "trace failed"):
                agent_orchestrator.submit_agent_response(
                    object(),
                    self.args,
                    request_id,
                    "ok",
                    self._make_valid_agent_output(request),
                    stage_runner=self._make_stage_runner(call_log),
                )
        finally:
            agent_session.append_decision_trace = original_append

        resumed = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            explicit_action="continue_air",
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(resumed["workflow_status"], "decision_required")
        self.assertEqual(resumed["decision_request"]["request_id"], "air_0001_r02")

    def test_run_agent_workflow_stops_at_decision_boundary(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        result = agent_orchestrator.run_agent_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(result["workflow_status"], "decision_required")
        self.assertEqual(call_log[0:2], ["pwm_map", "pwm_identify"])
        self.assertNotIn("air-dual-step_r01", call_log)

    def test_start_or_resume_workflow_rejects_corrupted_request_state(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        profile = agent_session.ensure_agent_profile_defaults({})
        profile["agent_tuning"]["pending_decision_request"] = "broken"
        self._save_profile(profile)

        with self.assertRaisesRegex(ValueError, "pending_decision_request"):
            agent_orchestrator.start_or_resume_workflow(
                object(),
                self.args,
                stage_runner=self._make_stage_runner([]),
            )

        profile = agent_session.ensure_agent_profile_defaults({})
        profile["agent_tuning"]["consumed_request_ids"] = [123]
        self._save_profile(profile)

        with self.assertRaisesRegex(ValueError, "consumed_request_ids"):
            agent_orchestrator.start_or_resume_workflow(
                object(),
                self.args,
                stage_runner=self._make_stage_runner([]),
            )

    def test_submit_agent_response_marks_request_consumed_before_post_execution_failure(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        started = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )
        request = started["decision_request"]
        request_id = request["request_id"]

        original_append = agent_session.append_decision_trace
        agent_session.append_decision_trace = lambda *_args, **_kwargs: (_ for _ in ()).throw(RuntimeError("trace failed"))
        try:
            with self.assertRaisesRegex(RuntimeError, "trace failed"):
                agent_orchestrator.submit_agent_response(
                    object(),
                    self.args,
                    request_id,
                    "ok",
                    self._make_valid_agent_output(request),
                    stage_runner=self._make_stage_runner(call_log),
                )
        finally:
            agent_session.append_decision_trace = original_append

        profile = self._load_profile()
        self.assertEqual(profile["agent_tuning"]["workflow_status"], "waiting_user")
        self.assertIsNone(profile["agent_tuning"].get("pending_decision_request"))
        self.assertIn(request_id, profile["agent_tuning"].get("consumed_request_ids", []))
        self.assertEqual(profile["agent_tuning"]["failure_trace"]["type"], "worker_circuit_break")
        self.assertTrue(profile["agent_tuning"]["failure_trace"]["request_consumed"])

        with self.assertRaisesRegex(RuntimeError, "already been consumed"):
            agent_orchestrator.submit_agent_response(
                object(),
                self.args,
                request_id,
                "ok",
                self._make_valid_agent_output(request),
                stage_runner=self._make_stage_runner(call_log),
            )

    def test_start_or_resume_workflow_ignores_uncommitted_result_beyond_last_committed_request(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        profile = agent_session.ensure_agent_profile_defaults(
            {
                "shared_targets": {"bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0}},
                "pwm_map": {"left": {"deadzone_pwm": 180}, "right": {"deadzone_pwm": 185}},
                "pwm_identify": {"seed_pi": _pid_pair(90.0, 18.0)},
            }
        )
        profile = agent_session.start_batch(profile, "air_dual", "air_0001", _pid_pair(90.0, 18.0))
        profile["agent_tuning"]["current_batch_id"] = "air_0001"
        profile["agent_tuning"]["workflow_stage"] = "air_dual"
        profile["agent_tuning"]["workflow_status"] = "running"
        profile["agent_tuning"]["current_round_index"] = 2
        profile["agent_tuning"]["last_committed_request_id"] = "air_0001_r01"
        profile["air_dual"]["active_batch"]["rounds_completed"] = 2
        self._save_profile(profile)

        round1 = _make_round_result_payload(common.MODE_AIR_DUAL_STEP, "air_0001", 1, _pid_pair(90.0, 18.0), _pid_pair(90.0, 18.0), 20.0)
        round1["request_id"] = "air_0001_r01"
        round2 = _make_round_result_payload(common.MODE_AIR_DUAL_STEP, "air_0001", 2, _pid_pair(92.0, 18.0), _pid_pair(90.0, 18.0), 18.0)
        round2["request_id"] = "air_0001_r02"
        common.write_json_file(
            self.root / "logs" / "agent_rounds" / "air_dual" / "air_0001_r01.json",
            round1,
        )
        common.write_json_file(
            self.root / "logs" / "agent_rounds" / "air_dual" / "air_0001_r02.json",
            round2,
        )

        resumed = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner([]),
        )

        self.assertEqual(resumed["workflow_status"], "decision_required")
        self.assertEqual(resumed["decision_request"]["request_id"], "air_0001_r02")

    def test_submit_agent_response_advances_batch_round_consistently(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        started = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
        )

        request = started["decision_request"]
        advanced = agent_orchestrator.submit_agent_response(
            object(),
            self.args,
            request["request_id"],
            "ok",
            self._make_valid_agent_output(request, delta_kp=1.0),
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(call_log[-1], "{0}_r01".format(common.MODE_AIR_DUAL_STEP))
        self.assertEqual(advanced["workflow_status"], "decision_required")
        self.assertEqual(advanced["decision_request"]["request_id"], "air_0001_r02")
        self.assertEqual(advanced["decision_request"]["context"]["stage_context"]["batch_id"], "air_0001")
        self.assertEqual(advanced["decision_request"]["context"]["stage_context"]["round_index"], 2)

        profile = self._load_profile()
        self.assertEqual(profile["agent_tuning"]["current_round_index"], 1)
        self.assertEqual(profile["air_dual"]["active_batch"]["rounds_completed"], 1)

    def test_start_or_resume_workflow_rejects_direct_ground_without_enter_ground(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []
        with self.assertRaisesRegex(RuntimeError, "enter_ground"):
            agent_orchestrator.start_or_resume_workflow(
                object(),
                self.args,
                requested_stage="ground_dual",
                stage_runner=self._make_stage_runner(call_log),
            )

    def test_start_or_resume_workflow_rejects_ground_override_when_action_is_not_enter_ground(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        profile = agent_session.ensure_agent_profile_defaults(
            {
                "shared_targets": {"bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0}},
                "pwm_map": {"left": {"deadzone_pwm": 180}, "right": {"deadzone_pwm": 180}},
                "pwm_identify": {"seed_pi": _pid_pair(88.0, 17.0)},
            }
        )
        profile = agent_session.set_pending_user_action(
            profile,
            "air_dual",
            "air_0001",
            "enter_ground",
            ["continue_air", "enter_ground", "stop_air"],
            "ready for load stage",
        )
        self._save_profile(profile)

        call_log = []
        with self.assertRaisesRegex(RuntimeError, "enter_ground"):
            agent_orchestrator.start_or_resume_workflow(
                object(),
                self.args,
                explicit_action="continue_air",
                requested_stage="ground_dual",
                stage_runner=self._make_stage_runner(call_log),
            )

    def test_start_or_resume_workflow_allows_ground_after_enter_ground(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        profile = agent_session.ensure_agent_profile_defaults(
            {
                "shared_targets": {"bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0}},
                "pwm_map": {"left": {"deadzone_pwm": 180}, "right": {"deadzone_pwm": 180}},
                "pwm_identify": {"seed_pi": _pid_pair(88.0, 17.0)},
                "air_dual": {
                    "best_pid": _pid_pair(111.0, 22.0),
                    "last_batch_best": {"best_pid": _pid_pair(111.0, 22.0), "combined_score": 6.0},
                    "last_batch_summary": {"batch_id": "air_0001", "best_pid": _pid_pair(111.0, 22.0), "combined_score": 6.0},
                    "last_summary": {"combined_score": 6.0},
                },
            }
        )
        profile = agent_session.set_pending_user_action(
            profile,
            "air_dual",
            "air_0001",
            "enter_ground",
            ["continue_air", "enter_ground", "stop_air"],
            "ready for load stage",
        )
        self._save_profile(profile)

        call_log = []
        started = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            explicit_action="enter_ground",
            requested_stage="ground_dual",
            stage_runner=self._make_stage_runner(call_log),
        )

        self.assertEqual(started["workflow_status"], "decision_required")
        self.assertEqual(started["workflow_stage"], "ground_dual")
        self.assertEqual(started["decision_request"]["request_id"], "ground_0001_r01")

    def test_start_or_resume_workflow_returns_completed_after_stop_air(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        profile = agent_session.ensure_agent_profile_defaults(
            {
                "shared_targets": {"bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0}},
                "pwm_map": {"left": {"deadzone_pwm": 180}, "right": {"deadzone_pwm": 180}},
                "pwm_identify": {"seed_pi": _pid_pair(88.0, 17.0)},
                "air_dual": {
                    "best_pid": _pid_pair(111.0, 22.0),
                    "last_batch_best": {"best_pid": _pid_pair(111.0, 22.0), "combined_score": 6.0},
                    "last_batch_summary": {"batch_id": "air_0001", "best_pid": _pid_pair(111.0, 22.0), "combined_score": 6.0},
                    "last_summary": {"combined_score": 6.0},
                },
            }
        )
        profile = agent_session.set_pending_user_action(
            profile,
            "air_dual",
            "air_0001",
            "stop_air",
            ["continue_air", "enter_ground", "stop_air"],
            "stop now",
        )
        self._save_profile(profile)

        result = agent_orchestrator.start_or_resume_workflow(
            object(),
            self.args,
            explicit_action="stop_air",
            stage_runner=self._make_stage_runner([]),
        )

        self.assertEqual(result["workflow_status"], "completed")
        self.assertIsNone(self._load_profile()["agent_tuning"]["pending_user_action"])


if __name__ == "__main__":
    unittest.main()

import io
import json
import pathlib
import tempfile
import unittest
from contextlib import redirect_stdout

from project.speed_loop_autotune.host import agent_autotune
from project.speed_loop_autotune.host import common


def _pid_pair(kp, ki, kd=0.0):
    return {
        "left": {"kp": float(kp), "ki": float(ki), "kd": float(kd)},
        "right": {"kp": float(kp), "ki": float(ki), "kd": float(kd)},
    }


class AgentWorkflowCliTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir.cleanup)
        self.root = pathlib.Path(self.temp_dir.name)
        self.profile_path = self.root / "logs" / "current_tuning_profile.json"
        self.args = agent_autotune.build_argument_parser().parse_args(
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
            round_score = 40.0 - float(run_args.round_index)
            result = {
                "mode": mode_name,
                "batch_id": run_args.batch_id,
                "round_index": int(run_args.round_index),
                "candidate_pid": candidate,
                "baseline_pid": baseline,
                "a_score": round_score,
                "b_score": round_score + 0.2,
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
                "timestamp": "2026-04-07 21:30:00",
                "left_score": round_score,
                "right_score": round_score + 0.4,
            }
            pathlib.Path(run_args.waveform_path).parent.mkdir(parents=True, exist_ok=True)
            pathlib.Path(run_args.waveform_path).write_text('{"sample_index": 0}\n', encoding="utf-8")
            common.write_json_file(run_args.result_json, result)
            call_log.append("{0}_r{1:02d}".format(mode_name, int(run_args.round_index)))
            return result

        return _runner

    def _make_valid_raw_decision(self, request_payload):
        return json.dumps(
            {
                "schema_version": 1,
                "candidate_pid": request_payload["context"]["pid_anchors"]["batch_start_pid"],
                "decision_summary": "hold seed for verification",
                "primary_reason": "measurement_conflict",
                "supporting_signals": ["structured metrics first"],
                "decision_mode": "hold",
                "base_reference": "batch_start",
                "expected_outcome": "verify_plateau",
                "confidence": "medium",
                "risk_level": "low",
                "needs_waveform_review": False,
                "batch_end_recommendation_if_no_improve": "continue_air",
            }
        )

    def test_run_agent_autotune_default_stops_at_first_decision_boundary(self):
        call_log = []

        summary = agent_autotune.run_agent_autotune(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
            batch_size=10,
        )

        self.assertEqual(call_log[0:2], ["pwm_map", "pwm_identify"])
        self.assertNotIn("{0}_r01".format(common.MODE_AIR_DUAL_STEP), call_log)
        self.assertEqual(summary["operation"], "start_or_resume")
        self.assertEqual(summary["final_workflow_status"], "decision_required")
        self.assertEqual(summary["decision_request"]["request_id"], "air_0001_r01")

    def test_run_agent_autotune_can_submit_raw_agent_output_and_advance(self):
        call_log = []
        started = agent_autotune.run_agent_autotune(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
            batch_size=10,
        )

        summary = agent_autotune.run_agent_autotune(
            object(),
            self.args,
            raw_agent_output=self._make_valid_raw_decision(started["decision_request"]),
            request_id=started["decision_request"]["request_id"],
            stage_runner=self._make_stage_runner(call_log),
            batch_size=10,
        )

        self.assertEqual(summary["operation"], "submit_decision")
        self.assertIn("{0}_r01".format(common.MODE_AIR_DUAL_STEP), call_log)
        self.assertEqual(summary["final_workflow_status"], "decision_required")
        self.assertEqual(summary["decision_request"]["request_id"], "air_0001_r02")

    def test_run_agent_autotune_loops_until_waiting_user_in_fallback_mode(self):
        call_log = []
        seen_requests = []

        def decision_builder(request_payload):
            seen_requests.append(request_payload.get("request_id"))
            return {
                "schema_version": 1,
                "candidate_pid": request_payload["context"]["pid_anchors"]["batch_start_pid"],
                "decision_summary": "hold seed for verification",
                "primary_reason": "measurement_conflict",
                "supporting_signals": ["structured metrics first"],
                "decision_mode": "hold",
                "base_reference": "batch_start",
                "expected_outcome": "verify_plateau",
                "confidence": "medium",
                "risk_level": "low",
                "needs_waveform_review": False,
                "batch_end_recommendation_if_no_improve": "continue_air",
            }

        summary = agent_autotune.run_agent_autotune(
            object(),
            self.args,
            decision_builder=decision_builder,
            stage_runner=self._make_stage_runner(call_log),
            batch_size=10,
        )

        self.assertEqual(call_log[0:2], ["pwm_map", "pwm_identify"])
        self.assertEqual(len(seen_requests), 10)
        self.assertEqual(seen_requests[0], "air_0001_r01")
        self.assertEqual(seen_requests[-1], "air_0001_r10")
        self.assertEqual(summary["final_workflow_status"], "waiting_user")
        self.assertEqual(summary["final_workflow_stage"], "air_dual")
        self.assertEqual(len(summary["rounds_run"]), 10)
        self.assertEqual(summary["operation"], "fallback_heuristic")
        self.assertEqual(
            summary["pending_user_action"]["allowed_actions"],
            ["continue_air", "enter_ground", "stop_air"],
        )

    def test_format_waiting_user_actions_keeps_only_action_words(self):
        text = agent_autotune.format_waiting_user_actions(
            {
                "pending_user_action": {
                    "allowed_actions": ["continue_air", "enter_ground", "stop_air"],
                }
            }
        )

        self.assertEqual(text, "continue_air\nenter_ground\nstop_air")

    def test_main_prints_waiting_actions_by_default(self):
        original_detect_port = agent_autotune.common.detect_port
        original_client = agent_autotune.common.VofaSerialClient
        original_run = agent_autotune.run_agent_autotune

        class FakeClient(object):
            def __init__(self, port, baudrate, timeout):
                self.port = port

            def close(self):
                return None

        agent_autotune.common.detect_port = lambda port: port
        agent_autotune.common.VofaSerialClient = FakeClient
        agent_autotune.run_agent_autotune = lambda *args, **kwargs: {
            "final_workflow_status": "waiting_user",
            "pending_user_action": {
                "allowed_actions": ["continue_air", "enter_ground", "stop_air"],
            },
        }

        stdout = io.StringIO()
        try:
            with redirect_stdout(stdout):
                exit_code = agent_autotune.main(["--profile-path", str(self.profile_path)])
        finally:
            agent_autotune.common.detect_port = original_detect_port
            agent_autotune.common.VofaSerialClient = original_client
            agent_autotune.run_agent_autotune = original_run

        self.assertEqual(exit_code, 0)
        self.assertEqual(stdout.getvalue().strip(), "continue_air\nenter_ground\nstop_air")

    def test_main_prints_json_when_decision_is_required(self):
        original_detect_port = agent_autotune.common.detect_port
        original_client = agent_autotune.common.VofaSerialClient
        original_run = agent_autotune.run_agent_autotune

        class FakeClient(object):
            def __init__(self, port, baudrate, timeout):
                self.port = port

            def close(self):
                return None

        expected = {
            "final_workflow_status": "decision_required",
            "decision_request": {
                "request_id": "air_0001_r01",
            },
        }

        agent_autotune.common.detect_port = lambda port: port
        agent_autotune.common.VofaSerialClient = FakeClient
        agent_autotune.run_agent_autotune = lambda *args, **kwargs: expected

        stdout = io.StringIO()
        try:
            with redirect_stdout(stdout):
                exit_code = agent_autotune.main(["--profile-path", str(self.profile_path)])
        finally:
            agent_autotune.common.detect_port = original_detect_port
            agent_autotune.common.VofaSerialClient = original_client
            agent_autotune.run_agent_autotune = original_run

        self.assertEqual(exit_code, 0)
        self.assertEqual(json.loads(stdout.getvalue()), expected)

    def test_main_json_flag_prints_summary_json(self):
        original_detect_port = agent_autotune.common.detect_port
        original_client = agent_autotune.common.VofaSerialClient
        original_run = agent_autotune.run_agent_autotune

        class FakeClient(object):
            def __init__(self, port, baudrate, timeout):
                self.port = port

            def close(self):
                return None

        expected = {
            "final_workflow_status": "completed",
            "final_workflow_stage": "air_dual",
            "rounds_run": [],
        }

        agent_autotune.common.detect_port = lambda port: port
        agent_autotune.common.VofaSerialClient = FakeClient
        agent_autotune.run_agent_autotune = lambda *args, **kwargs: expected

        stdout = io.StringIO()
        try:
            with redirect_stdout(stdout):
                exit_code = agent_autotune.main(["--profile-path", str(self.profile_path), "--json"])
        finally:
            agent_autotune.common.detect_port = original_detect_port
            agent_autotune.common.VofaSerialClient = original_client
            agent_autotune.run_agent_autotune = original_run

        self.assertEqual(exit_code, 0)
        self.assertEqual(json.loads(stdout.getvalue()), expected)


if __name__ == "__main__":
    unittest.main()

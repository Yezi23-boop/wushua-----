import json
import pathlib
import tempfile
import unittest

from project.speed_loop_autotune.host import agent_session
from project.speed_loop_autotune.host import common
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

    def _make_decision_policy(self):
        def _policy(stage_name, round_index, _profile, _stage_block, _last_result, _round_history):
            base_kp = 100.0
            base_ki = 20.0
            if stage_name == "ground_dual":
                base_kp = 120.0
                base_ki = 30.0
            return {
                "candidate_pid": _pid_pair(base_kp + float(round_index), base_ki + float(round_index) * 0.5),
                "decision_type": "mutate_from_seed",
                "base_pid_source": "seed_pi",
                "target_param": "kp",
                "delta": float(round_index),
                "phase": "explore",
                "reason": "{0} round {1}".format(stage_name, round_index),
            }

        return _policy

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
            round_score = 50.0 - float(run_args.round_index)
            result = {
                "mode": mode_name,
                "batch_id": run_args.batch_id,
                "round_index": int(run_args.round_index),
                "candidate_pid": candidate,
                "baseline_pid": common.load_json_dict(run_args.baseline_json),
                "a_score": round_score,
                "b_score": round_score + 0.25,
                "combined_score": round_score,
                "left_score": round_score,
                "right_score": round_score + 0.5,
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
                "timestamp": "2026-03-26 20:00:00",
            }
            pathlib.Path(run_args.waveform_path).parent.mkdir(parents=True, exist_ok=True)
            pathlib.Path(run_args.waveform_path).write_text('{"sample_index": 0}\n', encoding="utf-8")
            common.write_json_file(run_args.result_json, result)
            call_log.append("{0}_r{1:02d}".format(mode_name, int(run_args.round_index)))
            return result

        return _runner

    def test_run_agent_workflow_auto_runs_prereqs_and_finishes_air_batch(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        call_log = []

        result = agent_orchestrator.run_agent_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
            decision_policy=self._make_decision_policy(),
        )

        profile = self._load_profile()
        trace_path = pathlib.Path(profile["agent_tuning"]["last_decision_trace_path"])

        self.assertEqual(call_log[0:2], ["pwm_map", "pwm_identify"])
        self.assertEqual(len([entry for entry in call_log if entry.startswith(common.MODE_AIR_DUAL_STEP)]), 10)
        self.assertEqual(result["workflow_status"], "waiting_user")
        self.assertEqual(result["workflow_stage"], "air_dual")
        self.assertEqual(result["pending_user_action"]["stage"], "air_dual")
        self.assertEqual(profile["air_dual"]["last_batch_summary"]["evaluated_groups"], 10)
        self.assertEqual(profile["air_dual"]["last_batch_summary"]["best_pid"], _pid_pair(110.0, 25.0))
        self.assertTrue(trace_path.exists())
        self.assertEqual(len(trace_path.read_text(encoding="utf-8").strip().splitlines()), 10)

    def test_run_agent_workflow_resumes_existing_batch_from_next_round(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        profile = agent_session.ensure_agent_profile_defaults(
            {
                "shared_targets": {"bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0}},
                "pwm_map": {"left": {"deadzone_pwm": 180}, "right": {"deadzone_pwm": 180}},
                "pwm_identify": {"seed_pi": _pid_pair(88.0, 17.0)},
            }
        )
        profile = agent_session.start_batch(profile, "air_dual", "air_0001", _pid_pair(101.0, 20.5))
        result_root = self.profile_path.parent
        round_index = 1
        while round_index <= 3:
            result_path = agent_session.build_round_result_path(result_root, "air_dual", "air_0001", round_index)
            payload = {
                "candidate_pid": _pid_pair(100.0 + float(round_index), 20.0 + float(round_index) * 0.5),
                "combined_score": 60.0 - float(round_index),
            }
            common.write_json_file(result_path, payload)
            profile = agent_session.record_round_result(
                profile,
                "air_dual",
                "air_0001",
                round_index,
                result_path,
                payload,
            )
            round_index += 1
        self._save_profile(profile)

        call_log = []
        result = agent_orchestrator.run_agent_workflow(
            object(),
            self.args,
            stage_runner=self._make_stage_runner(call_log),
            decision_policy=self._make_decision_policy(),
        )

        self.assertEqual(
            call_log,
            [
                "{0}_r04".format(common.MODE_AIR_DUAL_STEP),
                "{0}_r05".format(common.MODE_AIR_DUAL_STEP),
                "{0}_r06".format(common.MODE_AIR_DUAL_STEP),
                "{0}_r07".format(common.MODE_AIR_DUAL_STEP),
                "{0}_r08".format(common.MODE_AIR_DUAL_STEP),
                "{0}_r09".format(common.MODE_AIR_DUAL_STEP),
                "{0}_r10".format(common.MODE_AIR_DUAL_STEP),
            ],
        )
        self.assertEqual(result["pending_user_action"]["stage"], "air_dual")
        self.assertEqual(self._load_profile()["agent_tuning"]["current_round_index"], 10)

    def test_run_agent_workflow_enters_ground_then_requires_explicit_save(self):
        if agent_orchestrator is None:
            self.fail("agent_orchestrator module missing")

        air_best = _pid_pair(111.0, 22.0)
        profile = agent_session.ensure_agent_profile_defaults(
            {
                "shared_targets": {"bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0}},
                "pwm_map": {"left": {"deadzone_pwm": 180}, "right": {"deadzone_pwm": 180}},
                "pwm_identify": {"seed_pi": _pid_pair(88.0, 17.0)},
                "air_dual": {
                    "best_pid": air_best,
                    "last_batch_best": {"best_pid": air_best, "combined_score": 6.0},
                    "last_batch_summary": {"batch_id": "air_0001", "best_pid": air_best, "combined_score": 6.0},
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
        entered = agent_orchestrator.run_agent_workflow(
            object(),
            self.args,
            explicit_action="enter_ground",
            stage_runner=self._make_stage_runner(call_log),
            decision_policy=self._make_decision_policy(),
        )

        profile = self._load_profile()

        self.assertEqual(len([entry for entry in call_log if entry.startswith(common.MODE_GROUND_DUAL_STEP)]), 10)
        self.assertEqual(entered["workflow_stage"], "ground_dual")
        self.assertEqual(entered["workflow_status"], "waiting_user")
        self.assertEqual(profile["ground_dual"]["best_pid"], None)
        self.assertEqual(profile["agent_tuning"]["pending_user_action"]["recommended_action"], "save")

        saved = agent_orchestrator.run_agent_workflow(
            object(),
            self.args,
            explicit_action="save",
            stage_runner=self._make_stage_runner(call_log),
            decision_policy=self._make_decision_policy(),
        )

        profile = self._load_profile()

        self.assertEqual(saved["workflow_status"], "completed")
        self.assertEqual(profile["ground_dual"]["best_pid"], _pid_pair(130.0, 35.0))
        self.assertEqual(profile["agent_tuning"]["pending_user_action"], None)


if __name__ == "__main__":
    unittest.main()

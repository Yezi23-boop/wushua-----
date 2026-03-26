import json
import pathlib
import tempfile
import types
import unittest

from project.speed_loop_autotune.host import air_dual
from project.speed_loop_autotune.host import agent_session
from project.speed_loop_autotune.host import common
from project.speed_loop_autotune.host import ground_dual
from project.speed_loop_autotune.host import vofa_autotune


class AgentSessionDefaultsTests(unittest.TestCase):
    def test_ensure_agent_profile_defaults_backfills_agent_tuning_and_stage_blocks(self):
        profile = {
            "meta": {"profile_version": 1},
            "pwm_identify": {},
        }

        updated = agent_session.ensure_agent_profile_defaults(profile)

        self.assertIn("agent_tuning", updated)
        self.assertIn("air_dual", updated)
        self.assertIn("ground_dual", updated)
        self.assertEqual(updated["agent_tuning"]["workflow_stage"], "air_dual")
        self.assertEqual(updated["agent_tuning"]["workflow_status"], "running")
        self.assertIsNone(updated["agent_tuning"]["pending_user_action"])
        self.assertEqual(updated["agent_tuning"]["current_round_index"], 0)
        self.assertEqual(updated["agent_tuning"]["last_worker_mode"], common.MODE_AIR_DUAL_STEP)
        self.assertEqual(updated["air_dual"]["active_batch"], None)
        self.assertEqual(updated["ground_dual"]["active_batch"], None)

    def test_start_batch_sets_active_batch_and_agent_cursor(self):
        profile = agent_session.ensure_agent_profile_defaults({"meta": {"profile_version": 1}})
        start_pid = {
            "left": {"kp": 1.0, "ki": 2.0, "kd": 0.0},
            "right": {"kp": 1.0, "ki": 2.0, "kd": 0.0},
        }
        profile["air_dual"]["last_batch_best"] = {"best_pid": start_pid}
        profile["air_dual"]["last_batch_summary"] = {"combined_score": 12.0}

        updated = agent_session.start_batch(profile, "air_dual", "air_0001", start_pid)

        self.assertEqual(updated["agent_tuning"]["workflow_stage"], "air_dual")
        self.assertEqual(updated["agent_tuning"]["current_batch_id"], "air_0001")
        self.assertEqual(updated["agent_tuning"]["current_round_index"], 0)
        self.assertEqual(updated["agent_tuning"]["last_worker_mode"], common.MODE_AIR_DUAL_STEP)
        self.assertEqual(updated["air_dual"]["active_batch"]["batch_id"], "air_0001")
        self.assertEqual(updated["air_dual"]["active_batch"]["start_pid"], start_pid)
        self.assertEqual(updated["air_dual"]["active_batch"]["rounds_completed"], 0)
        self.assertEqual(updated["air_dual"]["active_batch"]["search_phase"], "explore")
        self.assertEqual(updated["air_dual"]["last_batch_best"]["best_pid"], start_pid)
        self.assertEqual(updated["air_dual"]["last_batch_summary"]["combined_score"], 12.0)
        self.assertIsNot(updated["air_dual"]["active_batch"]["start_pid"], updated["air_dual"]["active_batch"]["current_best_pid"])
        self.assertIsNot(
            updated["air_dual"]["active_batch"]["start_pid"]["left"],
            updated["air_dual"]["active_batch"]["current_best_pid"]["left"],
        )

    def test_start_batch_rejects_unknown_stage_name(self):
        profile = agent_session.ensure_agent_profile_defaults({"meta": {"profile_version": 1}})

        with self.assertRaises(ValueError):
            agent_session.start_batch(profile, "grounddual", "air_0001", {})

    def test_set_pending_user_action_records_recommendation(self):
        profile = agent_session.ensure_agent_profile_defaults({"meta": {"profile_version": 1}})

        updated = agent_session.set_pending_user_action(
            profile,
            "air_dual",
            "air_0001",
            "continue_air",
            ["continue_air", "enter_ground", "stop_air"],
            "keep going",
        )

        self.assertEqual(updated["agent_tuning"]["pending_user_action"]["stage"], "air_dual")
        self.assertEqual(updated["agent_tuning"]["pending_user_action"]["batch_id"], "air_0001")
        self.assertEqual(updated["agent_tuning"]["pending_user_action"]["recommended_action"], "continue_air")
        self.assertEqual(updated["agent_tuning"]["pending_user_action"]["allowed_actions"], ["continue_air", "enter_ground", "stop_air"])
        self.assertEqual(updated["agent_tuning"]["pending_user_action"]["reason"], "keep going")


class CommonSchemaTests(unittest.TestCase):
    def test_normalize_band_scores_backfills_missing_keys(self):
        scores = common.normalize_band_scores({"low": 1.0, "top": 4.0})

        self.assertEqual(scores["low"], 1.0)
        self.assertEqual(scores["mid"], 0.0)
        self.assertEqual(scores["high"], 0.0)
        self.assertEqual(scores["top"], 4.0)

    def test_normalize_mode_name_accepts_agent_step_modes(self):
        self.assertEqual(common.normalize_mode_name(common.MODE_AIR_DUAL_STEP), common.MODE_AIR_DUAL_STEP)
        self.assertEqual(common.normalize_mode_name(common.MODE_GROUND_DUAL_STEP), common.MODE_GROUND_DUAL_STEP)


class AgentSessionRoundTests(unittest.TestCase):
    def test_record_round_updates_current_best_and_resume_cursor(self):
        profile = agent_session.ensure_agent_profile_defaults({"meta": {"profile_version": 1}})
        start_pid = {
            "left": {"kp": 1.0, "ki": 2.0, "kd": 0.0},
            "right": {"kp": 1.0, "ki": 2.0, "kd": 0.0},
        }
        better_pid = {
            "left": {"kp": 2.0, "ki": 2.5, "kd": 0.0},
            "right": {"kp": 2.0, "ki": 2.5, "kd": 0.0},
        }

        updated = agent_session.start_batch(profile, "air_dual", "air_0001", start_pid)
        updated = agent_session.record_round_result(
            updated,
            "air_dual",
            "air_0001",
            1,
            "logs/agent_rounds/air_dual/air_0001_r01.json",
            {
                "candidate_pid": start_pid,
                "combined_score": 18.0,
            },
        )
        updated = agent_session.record_round_result(
            updated,
            "air_dual",
            "air_0001",
            3,
            "logs/agent_rounds/air_dual/air_0001_r03.json",
            {
                "candidate_pid": better_pid,
                "combined_score": 12.5,
            },
        )

        self.assertEqual(updated["agent_tuning"]["current_round_index"], 3)
        self.assertEqual(updated["agent_tuning"]["last_result_path"], "logs/agent_rounds/air_dual/air_0001_r03.json")
        self.assertEqual(updated["air_dual"]["active_batch"]["rounds_completed"], 3)
        self.assertEqual(updated["air_dual"]["active_batch"]["current_best_pid"], better_pid)
        self.assertEqual(updated["air_dual"]["active_batch"]["current_best_score"], 12.5)
        self.assertEqual(updated["air_dual"]["active_batch"]["search_phase"], "explore")

        resumed = agent_session.resume_session_state(updated)

        self.assertEqual(resumed["workflow_stage"], "air_dual")
        self.assertEqual(resumed["current_batch_id"], "air_0001")
        self.assertEqual(resumed["current_round_index"], 3)
        self.assertIsNone(resumed["pending_user_action"])

    def test_append_decision_trace_writes_jsonl_row(self):
        temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(temp_dir.cleanup)
        trace_path = pathlib.Path(temp_dir.name) / "agent_decision_trace.jsonl"

        agent_session.append_decision_trace(
            trace_path,
            {
                "decision_type": "mutate_kp",
                "target_param": "kp",
                "delta": 5.0,
            },
        )

        content = trace_path.read_text(encoding="utf-8")
        self.assertIn('"decision_type": "mutate_kp"', content)
        self.assertIn('"delta": 5.0', content)


class AgentBatchFinalizeTests(unittest.TestCase):
    def test_finish_batch_summary_writes_last_batch_summary_and_pending_action(self):
        profile = agent_session.ensure_agent_profile_defaults({"meta": {"profile_version": 1}})
        start_pid = {
            "left": {"kp": 1.0, "ki": 2.0, "kd": 0.0},
            "right": {"kp": 1.0, "ki": 2.0, "kd": 0.0},
        }
        batch_best = {
            "left": {"kp": 1.5, "ki": 2.2, "kd": 0.0},
            "right": {"kp": 1.5, "ki": 2.2, "kd": 0.0},
        }

        updated = agent_session.start_batch(profile, "air_dual", "air_0002", start_pid)
        updated = agent_session.finish_batch_summary(
            updated,
            "air_dual",
            {
                "batch_id": "air_0002",
                "combined_score": 9.5,
                "a_score": 9.5,
                "b_score": 9.5,
                "best_pid": batch_best,
                "band_scores": {"low": 1.0, "mid": 2.0, "high": 3.0, "top": 4.0},
            },
            "continue_air",
            ["continue_air", "enter_ground", "stop_air"],
            "recent rounds still improved",
        )

        self.assertEqual(updated["air_dual"]["batch_round"], 1)
        self.assertEqual(updated["air_dual"]["last_batch_summary"]["combined_score"], 9.5)
        self.assertEqual(updated["air_dual"]["last_batch_best"]["best_pid"], batch_best)
        self.assertEqual(updated["air_dual"]["best_pid"], batch_best)
        self.assertEqual(updated["agent_tuning"]["workflow_status"], "waiting_user")
        self.assertEqual(updated["agent_tuning"]["pending_user_action"]["recommended_action"], "continue_air")

    def test_finish_batch_summary_rejects_illegal_action_words(self):
        profile = agent_session.ensure_agent_profile_defaults({"meta": {"profile_version": 1}})

        with self.assertRaises(ValueError):
            agent_session.finish_batch_summary(
                profile,
                "air_dual",
                {"batch_id": "air_0002", "best_pid": None, "combined_score": 9.5},
                "save",
                ["save"],
                "illegal action for air",
            )

    def test_finish_batch_summary_keeps_existing_air_best_when_new_batch_is_worse(self):
        profile = agent_session.ensure_agent_profile_defaults({"meta": {"profile_version": 1}})
        existing_best = {
            "left": {"kp": 1.1, "ki": 2.1, "kd": 0.0},
            "right": {"kp": 1.1, "ki": 2.1, "kd": 0.0},
        }
        worse_best = {
            "left": {"kp": 1.8, "ki": 2.8, "kd": 0.0},
            "right": {"kp": 1.8, "ki": 2.8, "kd": 0.0},
        }
        profile["air_dual"]["best_pid"] = existing_best
        profile["air_dual"]["last_summary"] = {"combined_score": 5.0}

        updated = agent_session.finish_batch_summary(
            profile,
            "air_dual",
            {
                "batch_id": "air_0003",
                "combined_score": 9.5,
                "best_pid": worse_best,
            },
            "stop_air",
            ["continue_air", "enter_ground", "stop_air"],
            "worse batch",
        )

        self.assertEqual(updated["air_dual"]["best_pid"], existing_best)

    def test_finalize_stage_best_requires_explicit_save_action(self):
        profile = agent_session.ensure_agent_profile_defaults({"meta": {"profile_version": 1}})
        ground_best = {
            "left": {"kp": 11.0, "ki": 3.0, "kd": 0.0},
            "right": {"kp": 12.0, "ki": 3.5, "kd": 0.0},
        }
        summary = {
            "batch_id": "ground_0001",
            "combined_score": 5.5,
            "band_scores": {"low": 1.0, "mid": 2.0, "high": 3.0, "top": 4.0},
            "trial_name": "ground_forward",
            "segments_ms": [[15.0, 200], [25.0, 200]],
        }

        unchanged = agent_session.finalize_stage_best(profile, "ground_dual", ground_best, summary, "continue_ground")
        self.assertIsNone(unchanged["ground_dual"]["best_pid"])

        saved = agent_session.finalize_stage_best(profile, "ground_dual", ground_best, summary, "save")
        self.assertEqual(saved["ground_dual"]["best_pid"], ground_best)
        self.assertEqual(saved["ground_dual"]["last_summary"]["combined_score"], 5.5)
        self.assertEqual(saved["agent_tuning"]["workflow_status"], "completed")


class StepWorkerTests(unittest.TestCase):
    def _make_step_samples(self):
        return [
            common.TelemetrySample(25.0, 18.0, 17.5, 3100.0, 3090.0, 1.0, 0.0, 0.0, 3100.0, 3090.0),
            common.TelemetrySample(25.0, 24.5, 24.0, 2600.0, 2580.0, 1.0, 0.0, 0.0, 2600.0, 2580.0),
            common.TelemetrySample(0.0, 1.0, 0.8, 100.0, 80.0, 0.0, 1.0, 0.0, 100.0, 80.0),
        ]

    def _make_air_display(self):
        return {
            "wheels": {
                "left": {
                    "total_score": 6.0,
                    "segments": [
                        {"target_speed": 15.0, "score": 1.0, "mean_speed": 15.0, "overshoot": 0.01},
                        {"target_speed": 25.0, "score": 2.0, "mean_speed": 25.0, "overshoot": 0.02},
                    ],
                },
                "right": {
                    "total_score": 6.5,
                    "segments": [
                        {"target_speed": 15.0, "score": 1.5, "mean_speed": 15.0, "overshoot": 0.02},
                        {"target_speed": 25.0, "score": 2.5, "mean_speed": 24.8, "overshoot": 0.03},
                    ],
                },
            }
        }

    def _make_ground_display(self):
        return {
            "segments": [
                {"target_speed": 15.0, "score": 1.0, "mean_speed": 15.0, "overshoot": 0.01},
                {"target_speed": 25.0, "score": 2.0, "mean_speed": 24.9, "overshoot": 0.02},
            ]
        }

    def test_run_air_dual_step_writes_required_json_fields(self):
        temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(temp_dir.cleanup)
        candidate_path = pathlib.Path(temp_dir.name) / "candidate.json"
        baseline_path = pathlib.Path(temp_dir.name) / "baseline.json"
        result_path = pathlib.Path(temp_dir.name) / "result.json"
        pid_pair = {
            "left": {"kp": 101.0, "ki": 22.0, "kd": 0.0},
            "right": {"kp": 102.0, "ki": 23.0, "kd": 0.0},
        }
        candidate_path.write_text(json.dumps(pid_pair), encoding="utf-8")
        baseline_path.write_text(json.dumps(pid_pair), encoding="utf-8")

        args = vofa_autotune.build_argument_parser().parse_args(
            [
                "--mode",
                common.MODE_AIR_DUAL_STEP,
                "--candidate-json",
                str(candidate_path),
                "--baseline-json",
                str(baseline_path),
                "--result-json",
                str(result_path),
                "--batch-id",
                "air_0001",
                "--round-index",
                "2",
            ]
        )
        args.autotune_sequence_explicit = False
        args.autotune_verify_sequence_explicit = False

        original_resolve = air_dual.resolve_air_dual_profile_defaults
        original_build_trial = air_dual.build_autotune_trial
        original_build_verify = air_dual.build_autotune_verify_trial
        original_score_config = air_dual.build_score_config
        original_eval = getattr(air_dual, "_evaluate_air_dual_step_candidate", None)

        def fake_resolve(_args):
            return {
                "profile": {"shared_targets": {"bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0}}},
                "profile_path": pathlib.Path(temp_dir.name) / "current_tuning_profile.json",
                "autotune_sequence": "15:500,25:500",
                "verify_sequence": "15:300,25:300",
                "initial_pair": common.wheel_pid_gains_from_dict(pid_pair),
                "shared_targets": {"bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0}},
            }

        def fake_build_trial(*_args, **_kwargs):
            return common.GroundLoadTrial("air_primary", ((15.0, 500), (25.0, 500)), 1000)

        def fake_build_verify(*_args, **_kwargs):
            return common.GroundLoadTrial("air_verify", ((15.0, 300), (25.0, 300)), 600)

        def fake_eval(*_args, **_kwargs):
            return (
                {
                    "score": 7.5,
                    "combined_score": 7.5,
                    "a_score": 7.5,
                    "b_score": 7.5,
                    "left_score": 6.0,
                    "right_score": 6.5,
                    "display": self._make_air_display(),
                    "persistent_overshoot": 0,
                },
                [self._make_step_samples()],
            )

        air_dual.resolve_air_dual_profile_defaults = fake_resolve
        air_dual.build_autotune_trial = fake_build_trial
        air_dual.build_autotune_verify_trial = fake_build_verify
        air_dual.build_score_config = lambda _args: common.DEFAULT_SCORE_CONFIG
        air_dual._evaluate_air_dual_step_candidate = fake_eval

        try:
            result = air_dual.run_air_dual_step(object(), args)
        finally:
            air_dual.resolve_air_dual_profile_defaults = original_resolve
            air_dual.build_autotune_trial = original_build_trial
            air_dual.build_autotune_verify_trial = original_build_verify
            air_dual.build_score_config = original_score_config
            if original_eval is None:
                delattr(air_dual, "_evaluate_air_dual_step_candidate")
            else:
                air_dual._evaluate_air_dual_step_candidate = original_eval

        self.assertEqual(result["mode"], common.MODE_AIR_DUAL_STEP)
        self.assertIn("combined_score", result)
        self.assertIn("band_scores", result)
        self.assertIn("waveform_digest", result)
        self.assertTrue(result_path.exists())

    def test_air_dual_step_start_priority_prefers_active_batch_then_last_batch_then_best(self):
        fallback = common.WheelPidGains(
            common.PidGains(10.0, 1.0, 0.0),
            common.PidGains(10.0, 1.0, 0.0),
        )
        active_pair = {
            "left": {"kp": 1.0, "ki": 1.0, "kd": 0.0},
            "right": {"kp": 1.0, "ki": 1.0, "kd": 0.0},
        }
        last_batch_pair = {
            "left": {"kp": 2.0, "ki": 2.0, "kd": 0.0},
            "right": {"kp": 2.0, "ki": 2.0, "kd": 0.0},
        }
        best_pair = {
            "left": {"kp": 3.0, "ki": 3.0, "kd": 0.0},
            "right": {"kp": 3.0, "ki": 3.0, "kd": 0.0},
        }

        profile = {
            "air_dual": {
                "active_batch": {"current_best_pid": active_pair},
                "last_batch_best": {"best_pid": last_batch_pair},
                "best_pid": best_pair,
            }
        }

        selected = air_dual.resolve_air_dual_step_start_pair(profile, fallback)
        self.assertEqual(common.wheel_pid_gains_to_dict(selected), active_pair)

        profile["air_dual"]["active_batch"] = None
        selected = air_dual.resolve_air_dual_step_start_pair(profile, fallback)
        self.assertEqual(common.wheel_pid_gains_to_dict(selected), last_batch_pair)

        profile["air_dual"]["last_batch_best"] = None
        selected = air_dual.resolve_air_dual_step_start_pair(profile, fallback)
        self.assertEqual(common.wheel_pid_gains_to_dict(selected), best_pair)

    def test_run_ground_dual_step_writes_required_json_fields(self):
        temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(temp_dir.cleanup)
        candidate_path = pathlib.Path(temp_dir.name) / "candidate.json"
        baseline_path = pathlib.Path(temp_dir.name) / "baseline.json"
        result_path = pathlib.Path(temp_dir.name) / "ground_result.json"
        pid_pair = {
            "left": {"kp": 120.0, "ki": 24.0, "kd": 0.0},
            "right": {"kp": 121.0, "ki": 25.0, "kd": 0.0},
        }
        candidate_path.write_text(json.dumps(pid_pair), encoding="utf-8")
        baseline_path.write_text(json.dumps(pid_pair), encoding="utf-8")

        args = vofa_autotune.build_argument_parser().parse_args(
            [
                "--mode",
                common.MODE_GROUND_DUAL_STEP,
                "--candidate-json",
                str(candidate_path),
                "--baseline-json",
                str(baseline_path),
                "--result-json",
                str(result_path),
                "--batch-id",
                "ground_0001",
                "--round-index",
                "4",
            ]
        )

        original_load = ground_dual.load_tuning_profile
        original_build_trials = ground_dual.build_ground_load_trials
        original_return_trial = ground_dual.build_ground_load_return_trial
        original_score_config = ground_dual.build_score_config
        original_eval = getattr(ground_dual, "_evaluate_ground_dual_step_candidate", None)

        def fake_load(*_args, **_kwargs):
            return {"shared_targets": {"bands": {"low": 15.0, "mid": 25.0, "high": 35.0, "top": 45.0}}}

        def fake_trials(*_args, **_kwargs):
            return [common.GroundLoadTrial("ground_forward", ((15.0, 200), (25.0, 200)), 400)]

        def fake_eval(*_args, **_kwargs):
            return (
                {
                    "combined_score": 5.5,
                    "display": self._make_ground_display(),
                },
                [self._make_step_samples()],
            )

        ground_dual.load_tuning_profile = fake_load
        ground_dual.build_ground_load_trials = fake_trials
        ground_dual.build_ground_load_return_trial = lambda *_args, **_kwargs: None
        ground_dual.build_score_config = lambda _args: common.DEFAULT_SCORE_CONFIG
        ground_dual._evaluate_ground_dual_step_candidate = fake_eval

        try:
            result = ground_dual.run_ground_dual_step(object(), args)
        finally:
            ground_dual.load_tuning_profile = original_load
            ground_dual.build_ground_load_trials = original_build_trials
            ground_dual.build_ground_load_return_trial = original_return_trial
            ground_dual.build_score_config = original_score_config
            if original_eval is None:
                delattr(ground_dual, "_evaluate_ground_dual_step_candidate")
            else:
                ground_dual._evaluate_ground_dual_step_candidate = original_eval

        self.assertEqual(result["mode"], common.MODE_GROUND_DUAL_STEP)
        self.assertIn("combined_score", result)
        self.assertIn("band_scores", result)
        self.assertIn("waveform_digest", result)
        self.assertTrue(result_path.exists())

    def test_ground_dual_step_start_priority_prefers_ground_then_air_then_seed(self):
        fallback = common.WheelPidGains(
            common.PidGains(10.0, 1.0, 0.0),
            common.PidGains(10.0, 1.0, 0.0),
        )
        ground_best = {
            "left": {"kp": 4.0, "ki": 4.0, "kd": 0.0},
            "right": {"kp": 4.0, "ki": 4.0, "kd": 0.0},
        }
        air_best = {
            "left": {"kp": 5.0, "ki": 5.0, "kd": 0.0},
            "right": {"kp": 5.0, "ki": 5.0, "kd": 0.0},
        }
        seed_pair = {
            "left": {"kp": 6.0, "ki": 6.0, "kd": 0.0},
            "right": {"kp": 6.0, "ki": 6.0, "kd": 0.0},
        }

        profile = {
            "ground_dual": {"best_pid": ground_best},
            "air_dual": {"best_pid": air_best},
            "pwm_identify": {"seed_pi": seed_pair},
        }

        selected = ground_dual.resolve_ground_dual_step_start_pair(profile, fallback)
        self.assertEqual(common.wheel_pid_gains_to_dict(selected), ground_best)

        profile["ground_dual"] = {}
        selected = ground_dual.resolve_ground_dual_step_start_pair(profile, fallback)
        self.assertEqual(common.wheel_pid_gains_to_dict(selected), air_best)

        profile["air_dual"] = {}
        selected = ground_dual.resolve_ground_dual_step_start_pair(profile, fallback)
        self.assertEqual(common.wheel_pid_gains_to_dict(selected), seed_pair)


class StepCliTests(unittest.TestCase):
    def test_build_argument_parser_accepts_step_modes_and_json_flags(self):
        parser = vofa_autotune.build_argument_parser()

        args = parser.parse_args(
            [
                "--mode",
                common.MODE_AIR_DUAL_STEP,
                "--candidate-json",
                "candidate.json",
                "--baseline-json",
                "baseline.json",
                "--result-json",
                "result.json",
            ]
        )

        self.assertEqual(args.mode, common.MODE_AIR_DUAL_STEP)
        self.assertEqual(args.candidate_json, "candidate.json")
        self.assertEqual(args.baseline_json, "baseline.json")
        self.assertEqual(args.result_json, "result.json")

    def test_main_dispatches_step_modes(self):
        original_detect_port = vofa_autotune.detect_port
        original_client = vofa_autotune.VofaSerialClient
        original_air_step = getattr(vofa_autotune, "run_air_dual_step", None)
        original_ground_step = getattr(vofa_autotune, "run_ground_dual_step", None)

        class FakeClient(object):
            def __init__(self, port, baudrate, timeout):
                self.port = port
                self.baudrate = baudrate
                self.timeout = timeout

            def close(self):
                return None

        vofa_autotune.detect_port = lambda port: port
        vofa_autotune.VofaSerialClient = FakeClient
        vofa_autotune.run_air_dual_step = lambda _client, _args: 11
        vofa_autotune.run_ground_dual_step = lambda _client, _args: 12

        try:
            air_result = vofa_autotune.main(["--mode", common.MODE_AIR_DUAL_STEP])
            ground_result = vofa_autotune.main(["--mode", common.MODE_GROUND_DUAL_STEP])
        finally:
            vofa_autotune.detect_port = original_detect_port
            vofa_autotune.VofaSerialClient = original_client
            if original_air_step is None:
                delattr(vofa_autotune, "run_air_dual_step")
            else:
                vofa_autotune.run_air_dual_step = original_air_step
            if original_ground_step is None:
                delattr(vofa_autotune, "run_ground_dual_step")
            else:
                vofa_autotune.run_ground_dual_step = original_ground_step

        self.assertEqual(air_result, 11)
        self.assertEqual(ground_result, 12)


if __name__ == "__main__":
    unittest.main()

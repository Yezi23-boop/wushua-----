import unittest

from project.speed_loop_autotune.host import agent_session
from project.speed_loop_autotune.host import common


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

        updated = agent_session.start_batch(profile, "air_dual", "air_0001", start_pid)

        self.assertEqual(updated["agent_tuning"]["workflow_stage"], "air_dual")
        self.assertEqual(updated["agent_tuning"]["current_batch_id"], "air_0001")
        self.assertEqual(updated["agent_tuning"]["current_round_index"], 1)
        self.assertEqual(updated["agent_tuning"]["last_worker_mode"], common.MODE_AIR_DUAL_STEP)
        self.assertEqual(updated["air_dual"]["active_batch"]["batch_id"], "air_0001")
        self.assertEqual(updated["air_dual"]["active_batch"]["start_pid"], start_pid)
        self.assertEqual(updated["air_dual"]["active_batch"]["rounds_completed"], 0)
        self.assertEqual(updated["air_dual"]["active_batch"]["search_phase"], "explore")

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


if __name__ == "__main__":
    unittest.main()

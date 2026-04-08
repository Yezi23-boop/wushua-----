import pathlib
import tempfile
import unittest
from contextlib import redirect_stdout
import io

from project.speed_loop_autotune.host import agent_autotune_sim


class AgentAutotuneSimTests(unittest.TestCase):
    def setUp(self):
        self.module = agent_autotune_sim
        self.temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir.cleanup)
        self.root = pathlib.Path(self.temp_dir.name)

    def test_run_simulation_stops_at_decision_boundary_by_default(self):
        args = self.module.build_argument_parser().parse_args(
            [
                "--sim-root",
                str(self.root),
                "--json",
            ]
        )
        args.mode = self.module.common.normalize_mode_name(args.mode)

        summary = self.module.run_simulation(args)

        self.assertEqual(summary["final_workflow_status"], "decision_required")
        self.assertEqual(summary["decision_request"]["request_id"], "air_0001_r01")
        self.assertEqual(summary["simulation_root"], str(self.root))

    def test_run_simulation_can_auto_run_mock_agent_batch(self):
        args = self.module.build_argument_parser().parse_args(
            [
                "--sim-root",
                str(self.root),
                "--auto-sim-agent",
                "--json",
            ]
        )
        args.mode = self.module.common.normalize_mode_name(args.mode)

        summary = self.module.run_simulation(args)

        self.assertEqual(summary["final_workflow_status"], "waiting_user")
        self.assertEqual(summary["final_workflow_stage"], "air_dual")
        self.assertEqual(len(summary["rounds_run"]), 10)
        self.assertEqual(
            summary["pending_user_action"]["allowed_actions"],
            ["continue_air", "enter_ground", "stop_air"],
        )

    def test_main_prints_json_summary(self):
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            exit_code = self.module.main(
                [
                    "--sim-root",
                    str(self.root),
                    "--auto-sim-agent",
                    "--json",
                ]
            )

        self.assertEqual(exit_code, 0)


if __name__ == "__main__":
    unittest.main()

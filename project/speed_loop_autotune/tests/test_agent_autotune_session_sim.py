import io
import json
import pathlib
import tempfile
import unittest
from contextlib import redirect_stdout

from project.speed_loop_autotune.host import agent_autotune_session_sim


class AgentAutotuneSessionSimTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir.cleanup)
        self.root = pathlib.Path(self.temp_dir.name)

    def _make_raw_decision(self):
        request = json.loads((self.root / "last_decision_request.json").read_text(encoding="utf-8"))
        return json.dumps(
            {
                "schema_version": 1,
                "candidate_pid": request["context"]["pid_anchors"]["batch_start_pid"],
                "decision_summary": "session sim hold",
                "primary_reason": "measurement_conflict",
                "supporting_signals": ["session simulation"],
                "decision_mode": "hold",
                "base_reference": "batch_start",
                "expected_outcome": "verify_plateau",
                "confidence": "medium",
                "risk_level": "low",
                "needs_waveform_review": False,
                "batch_end_recommendation_if_no_improve": "continue_air",
            }
        )

    def test_start_creates_last_decision_request_file(self):
        summary = agent_autotune_session_sim.run_start(self.root)

        self.assertEqual(summary["final_workflow_status"], "decision_required")
        self.assertTrue((self.root / "last_decision_request.json").exists())
        self.assertTrue((self.root / "last_bridge_summary.json").exists())

    def test_submit_advances_one_round(self):
        started = agent_autotune_session_sim.run_start(self.root)
        summary = agent_autotune_session_sim.run_submit(
            self.root,
            self._make_raw_decision(),
            request_id=started["decision_request"]["request_id"],
        )

        self.assertEqual(summary["final_workflow_status"], "decision_required")
        self.assertEqual(summary["decision_request"]["request_id"], "air_0001_r02")

    def test_main_prints_json(self):
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            exit_code = agent_autotune_session_sim.main(
                [
                    "--sim-root",
                    str(self.root),
                    "start",
                    "--json",
                ]
            )

        self.assertEqual(exit_code, 0)
        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["final_workflow_status"], "decision_required")


if __name__ == "__main__":
    unittest.main()

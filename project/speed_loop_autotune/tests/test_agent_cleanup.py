import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).resolve().parents[1] / "host" / "vofa_autotune.py"
HOST_DIR = MODULE_PATH.parent


def load_module():
    spec = importlib.util.spec_from_file_location("vofa_autotune", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError("Unable to load tools/vofa_autotune.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AgentCleanupTests(unittest.TestCase):
    def test_parser_uses_stage_specific_sequence_flags(self):
        module = load_module()
        parser = module.build_argument_parser()

        args = parser.parse_args([])

        self.assertTrue(hasattr(args, "air_primary_sequence"))
        self.assertTrue(hasattr(args, "air_verify_sequence"))
        self.assertTrue(hasattr(args, "air_tail_zero_ms"))
        self.assertFalse(hasattr(args, "autotune_sequence"))
        self.assertFalse(hasattr(args, "autotune_verify_sequence"))
        self.assertFalse(hasattr(args, "autotune_tail_zero_ms"))

    def test_host_sources_no_longer_keep_legacy_helper_names(self):
        common_source = (HOST_DIR / "common.py").read_text(encoding="utf-8")
        air_source = (HOST_DIR / "air_dual.py").read_text(encoding="utf-8")
        ground_source = (HOST_DIR / "ground_dual.py").read_text(encoding="utf-8")

        self.assertNotIn("GroundLoadTrial", common_source)
        self.assertNotIn("_split_ground_load_segments", common_source)
        self.assertNotIn("build_autotune_trial", air_source)
        self.assertNotIn("build_autotune_verify_trial", air_source)
        self.assertNotIn("run_autotune_trial", air_source)
        self.assertNotIn("build_ground_load_trials", ground_source)
        self.assertNotIn("build_ground_load_return_trial", ground_source)
        self.assertNotIn("run_ground_load_group", ground_source)
        self.assertNotIn("wait_for_operator", ground_source)
        self.assertNotIn("input(message)", ground_source)


if __name__ == "__main__":
    unittest.main()

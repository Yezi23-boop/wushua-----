import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).resolve().parents[1] / "host" / "vofa_autotune.py"


def load_module():
    spec = importlib.util.spec_from_file_location("vofa_autotune", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError("Unable to load tools/vofa_autotune.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AgentOnlyCliTests(unittest.TestCase):
    def test_normalize_mode_name_rejects_legacy_batch_modes(self):
        module = load_module()

        with self.assertRaises(ValueError):
            module.normalize_mode_name("air-dual")
        with self.assertRaises(ValueError):
            module.normalize_mode_name("autotune")
        with self.assertRaises(ValueError):
            module.normalize_mode_name("ground-dual")
        with self.assertRaises(ValueError):
            module.normalize_mode_name("ground-load")

    def test_argument_parser_drops_legacy_batch_flags(self):
        module = load_module()
        parser = module.build_argument_parser()

        args = parser.parse_args([])

        self.assertFalse(hasattr(args, "candidate_limit"))
        self.assertFalse(hasattr(args, "interactive_batches"))
        self.assertFalse(hasattr(args, "iterations"))
        self.assertFalse(hasattr(args, "delta_kp"))
        self.assertFalse(hasattr(args, "delta_ki"))
        self.assertFalse(hasattr(args, "delta_kd"))
        self.assertFalse(hasattr(args, "search_tolerance"))
        self.assertEqual(args.mode, module.MODE_PWM_MAP)

        with self.assertRaises(SystemExit):
            parser.parse_args(["--mode", "air-dual"])

    def test_top_level_module_no_longer_exports_legacy_batch_entrypoints(self):
        module = load_module()

        self.assertFalse(hasattr(module, "run_air_dual_autotune"))
        self.assertFalse(hasattr(module, "run_ground_dual_autotune"))

    def test_capture_only_remains_available(self):
        module = load_module()
        original_detect_port = module.detect_port
        original_client = module.VofaSerialClient
        original_run_capture = module.run_capture

        class FakeClient(object):
            def __init__(self, port, baudrate, timeout):
                self.port = port
                self.baudrate = baudrate
                self.timeout = timeout

            def close(self):
                return None

        module.detect_port = lambda port: port
        module.VofaSerialClient = FakeClient
        module.run_capture = lambda _client, _seconds: 17

        try:
            result = module.main(["--capture-only", "--mode", module.MODE_PWM_MAP])
        finally:
            module.detect_port = original_detect_port
            module.VofaSerialClient = original_client
            module.run_capture = original_run_capture

        self.assertEqual(result, 17)


if __name__ == "__main__":
    unittest.main()

import importlib.util
import pathlib
import unittest


MODULE_PATH = (
    pathlib.Path(__file__).resolve().parents[1]
    / "project"
    / "speed_loop_autotune"
    / "tests"
    / "test_vofa_autotune.py"
)


def _load_module():
    spec = importlib.util.spec_from_file_location("project_speed_loop_autotune_tests", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError("Unable to load project/speed_loop_autotune/tests/test_vofa_autotune.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_MODULE = _load_module()
globals().update(
    {name: getattr(_MODULE, name) for name in dir(_MODULE) if not name.startswith("_")}
)


if __name__ == "__main__":
    unittest.main()

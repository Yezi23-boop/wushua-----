import importlib.util
import pathlib
import sys


MODULE_PATH = (
    pathlib.Path(__file__).resolve().parents[1]
    / "project"
    / "speed_loop_autotune"
    / "host"
    / "vofa_autotune.py"
)


def _load_module():
    spec = importlib.util.spec_from_file_location("project_speed_loop_autotune", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("Unable to load project/speed_loop_autotune/host/vofa_autotune.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_MODULE = _load_module()
globals().update(
    {name: getattr(_MODULE, name) for name in dir(_MODULE) if not name.startswith("_")}
)


if __name__ == "__main__":
    sys.exit(_MODULE.main())

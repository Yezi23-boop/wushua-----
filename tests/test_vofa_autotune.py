import importlib.util
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VOFA_C = ROOT / "project" / "service" / "vofa.c"
DEBUG_VIEW_C = ROOT / "project" / "service" / "debug_view.c"
AUTOTUNE_ADAPTER_C = ROOT / "project" / "service" / "speed_loop_autotune_adapter.c"
TOOL_NAMES = (
    "vofa_autotune.py",
    "agent_autotune.py",
    "agent_autotune_sim.py",
    "agent_autotune_session_sim.py",
)


def test_vofa_service_uses_current_legacy_command_path():
    vofa_source = VOFA_C.read_text(encoding="utf-8")
    debug_source = DEBUG_VIEW_C.read_text(encoding="utf-8")

    assert "void vofa_service_legacy(void)" in vofa_source
    assert "static void vofa_handle_legacy_command(char *cmd)" in vofa_source
    assert "handle_vofa_command(vofa_cmd);" in vofa_source
    assert "vofa_handle_legacy_command(cmd);" in vofa_source

    assert 'strcmp(param_name, "L_KP")' in vofa_source
    assert 'strcmp(param_name, "R_KP")' in vofa_source
    assert 'strcmp(cmd, "SAVE")' in vofa_source
    assert 'strcmp(cmd, "LOAD")' in vofa_source
    assert 'strcmp(cmd, "STOP")' in vofa_source
    assert 'strcmp(cmd, "START")' in vofa_source

    assert "#define DEBUG_VIEW_ENABLE_SPEED_LOOP_AUTOTUNE 0" in debug_source
    assert "vofa_service_legacy();" in debug_source


def test_old_autotune_tool_modules_import_without_missing_project_package():
    for tool_name in TOOL_NAMES:
        tool_path = ROOT / "tools" / tool_name
        spec = importlib.util.spec_from_file_location(tool_name.replace(".py", ""), tool_path)
        assert spec is not None
        assert spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        assert hasattr(module, "main")
        assert module.main(["--help"]) == 0


def test_old_autotune_tool_scripts_report_removed_topic():
    for tool_name in TOOL_NAMES:
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools" / tool_name), "--help"],
            cwd=str(ROOT),
            text=True,
            capture_output=True,
            check=False,
        )

        assert result.returncode == 0
        assert "project/speed_loop_autotune" in result.stdout
        assert "当前仓库未包含" in result.stdout

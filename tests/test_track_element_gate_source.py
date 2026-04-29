from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
A_RUN_MODE_C = ROOT / "project" / "user" / "a_run_mode.c"
A_RUN_MODE_H = ROOT / "project" / "user" / "a_run_mode.h"
A_RUN_C = ROOT / "project" / "user" / "a_run.c"
FUYA_C = ROOT / "project" / "user" / "FUYA.c"
FUYA_H = ROOT / "project" / "user" / "FUYA.h"
EEPROM_H = ROOT / "project" / "service" / "eeprom.h"
EEPROM_C = ROOT / "project" / "service" / "eeprom.c"
MENU_C = ROOT / "project" / "service" / "menu.c"


def _read(path):
    return path.read_text(encoding="utf-8")


def test_track_element_gate_contract_is_wired():
    header = _read(A_RUN_MODE_H)
    source = _read(A_RUN_MODE_C)
    runner = _read(A_RUN_C)

    assert "void circle_check_l(uint8 allow_entry);" in header
    assert "void a_run_mode_update_track_element_gate(void);" in header
    assert "uint8 a_run_mode_take_ring_finish_event(void);" in header
    assert "int8 a_run_mode_get_expected_element(void);" in header
    assert "int8 a_run_mode_get_track_mode(void);" in header
    assert "int8 a_run_mode_get_cylinder_state(void);" in header

    assert "enum TrackElement" in source
    assert "enum CylinderStep" in source
    assert "ring_finish_event = 1;" in source
    assert "uint8 a_run_mode_take_ring_finish_event(void)" in source
    assert "void a_run_mode_update_track_element_gate(void)" in source
    assert "circle_check_l(1);" in source
    assert "circle_check_l(0);" in source

    assert "a_run_mode_update_track_element_gate();" in runner
    assert "gyro_integrals();" in runner
    assert runner.index("a_run_mode_update_track_element_gate();") < runner.index("gyro_integrals();")


def test_cylinder_peak_angle_api_is_split_from_element_state_machine():
    fuya_header = _read(FUYA_H)
    fuya_source = _read(FUYA_C)
    run_mode_source = _read(A_RUN_MODE_C)

    assert "void fuya_enter_cylinder_peak_mode(void);" in fuya_header
    assert "void fuya_exit_cylinder_peak_mode(void);" in fuya_header
    assert "void fuya_enter_cylinder_peak_mode(void)" in fuya_source
    assert "void fuya_exit_cylinder_peak_mode(void)" in fuya_source

    assert "fuya_enter_cylinder_peak_mode();" in run_mode_source
    assert "fuya_exit_cylinder_peak_mode();" in run_mode_source
    assert "#define CYLINDER_TOP_VZ -0.85f" in run_mode_source
    assert "#define CYLINDER_GROUND_VZ 0.98f" in run_mode_source
    assert "CYLINDER_STABLE_DELAY_COUNT 10" in run_mode_source


def test_track_mode_config_and_menu_are_present():
    eeprom_header = _read(EEPROM_H)
    eeprom_source = _read(EEPROM_C)
    menu_source = _read(MENU_C)

    assert "int16 track_mode;" in eeprom_header
    assert "config->start.track_mode = 0;" in eeprom_source
    assert "config->start.track_mode = (int16)read_int(" in eeprom_source
    assert "save_int(config->start.track_mode" in eeprom_source

    assert '"trk_mode"' in menu_source
    assert "app.start.track_mode" in menu_source
    assert "a_run_mode_get_expected_element()" in menu_source
    assert "a_run_mode_get_cylinder_state()" in menu_source

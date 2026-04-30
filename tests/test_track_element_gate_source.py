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
    assert "int8 a_run_mode_get_expected_element(void);" in header
    assert "int8 a_run_mode_get_cylinder_state(void);" in header

    assert "enum TrackElement" in source
    assert "enum CylinderStep" in source
    assert "ring_finish_event = 1;" in source
    assert "static uint8 ring_take_finish_event(void)" in source
    assert "void a_run_mode_update_track_element_gate(void)" in source
    assert "circle_check_l(1);" in source
    assert "circle_check_l(0);" in source
    assert "if (app.start.circle_flags != 1)" in source
    assert "start_state != START_STATE_2 || app.start.circle_flags != 1" not in source

    run_time_1_body = runner[runner.index("void run_time_1(void)"):runner.index("void run_time_2(void)")]
    run_time_2_body = runner[runner.index("void run_time_2(void)"):runner.index("void run_time_3(void)")]

    assert "a_run_mode_update_track_element_gate();" in run_time_1_body
    assert "gyro_integrals();" in run_time_1_body
    assert "imu_update_gravity_vector_from_quaternion(0, 0, &gravity_vzc);" in run_time_1_body
    assert run_time_1_body.index("imu_update_gravity_vector_from_quaternion") < run_time_1_body.index("a_run_mode_update_track_element_gate();")
    assert run_time_1_body.index("a_run_mode_update_track_element_gate();") < run_time_1_body.index("gyro_integrals();")
    assert "track_gate_div_10" not in runner
    assert "a_run_mode_update_track_element_gate();" not in run_time_2_body
    assert "gyro_integrals();" not in run_time_2_body


def test_cylinder_peak_angle_api_is_split_from_element_state_machine():
    fuya_header = _read(FUYA_H)
    fuya_source = _read(FUYA_C)
    run_mode_source = _read(A_RUN_MODE_C)

    assert "void fuya_apply_cylinder_peak_angle(void);" in fuya_header
    assert "void fuya_restore_cylinder_peak_angle(void);" in fuya_header
    assert "void fuya_apply_cylinder_peak_angle(void)" in fuya_source
    assert "void fuya_restore_cylinder_peak_angle(void)" in fuya_source

    assert "fuya_apply_cylinder_peak_angle();" in run_mode_source
    assert "fuya_restore_cylinder_peak_angle();" in run_mode_source
    assert "#define CYLINDER_TOP_VZ -0.85f" in run_mode_source
    assert "#define CYLINDER_GROUND_VZ 0.90f" in run_mode_source
    assert "CYLINDER_STABLE_DELAY_COUNT 5u" in run_mode_source


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

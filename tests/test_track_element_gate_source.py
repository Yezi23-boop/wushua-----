from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
A_RUN_MODE_C = ROOT / "project" / "user" / "a_run_mode.c"
A_RUN_MODE_H = ROOT / "project" / "user" / "a_run_mode.h"
A_RUN_FLY_C = ROOT / "project" / "user" / "a_run_fly.c"
A_RUN_FLY_H = ROOT / "project" / "user" / "a_run_fly.h"
A_RUN_TRACK_ELEMENT_C = ROOT / "project" / "user" / "a_run_track_element.c"
A_RUN_TRACK_ELEMENT_H = ROOT / "project" / "user" / "a_run_track_element.h"
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
    mode_source = _read(A_RUN_MODE_C)
    source = _read(A_RUN_TRACK_ELEMENT_C)
    track_header = _read(A_RUN_TRACK_ELEMENT_H)
    runner = _read(A_RUN_C)

    assert "void circle_check_l(uint8 allow_entry);" not in header
    assert "RingStruct" not in header
    assert "ring_data" not in header
    assert "FlyState" not in header
    assert "FLY_STATE_" not in header
    assert "void a_run_mode_update_track_element_gate(void);" in header
    assert "int8 a_run_mode_get_expected_element(void);" in header
    assert "int8 a_run_mode_get_cylinder_state(void);" in header
    assert "a_run_mode_get_ring_yaw_delta_sum" not in header
    assert "a_run_mode_get_ring_encoder" not in header
    assert "a_run_mode_get_ring_diff_set" not in header

    assert "typedef struct" in track_header
    assert "} RingStruct;" in track_header
    assert "extern RingStruct ring_data;" in track_header
    assert "RingStruct ring_data = {0};" in source
    assert "static RingStruct ring_data" not in source
    assert "enum TrackElement" in source
    assert "ELEMENT_LEFT_RING = 1" in source
    assert "ELEMENT_RIGHT_RING = 2" in source
    assert "ELEMENT_CYLINDER = 3" in source
    assert "static enum TrackElement expected_element = ELEMENT_LEFT_RING;" in source
    assert "enum CylinderStep" in source
    assert "ring_finish_event = 1;" in source
    assert "static uint8 ring_take_finish_event(void)" in source
    assert "void a_run_track_element_update_gate(void)" in source
    assert "static void circle_check_l(uint8 allow_entry)" in source
    assert "circle_check_l(1);" in source
    assert "circle_check_l(0);" in source
    assert "if (app.start.circle_flags != 1)" in source
    assert "start_state != START_STATE_2 || app.start.circle_flags != 1" not in source
    assert "void a_run_track_element_update_gate(void);" in track_header
    assert "void a_run_track_element_update_integrals(void);" in track_header
    assert "a_run_track_element_get_ring_yaw_delta_sum" not in track_header
    assert "a_run_track_element_get_ring_encoder" not in track_header
    assert "a_run_track_element_get_ring_diff_set" not in track_header
    assert "a_run_track_element_update_gate();" in mode_source
    assert "a_run_track_element_update_integrals();" in mode_source
    assert "a_run_track_element_get_ring_yaw_delta_sum" not in mode_source
    assert "a_run_track_element_get_ring_encoder" not in mode_source
    assert "a_run_track_element_get_ring_diff_set" not in mode_source

    run_time_1_body = runner[runner.index("void run_time_1(void)"):runner.index("void run_time_2(void)")]
    run_time_2_body = runner[runner.index("void run_time_2(void)"):runner.index("void run_time_3(void)")]

    assert "a_run_mode_update_track_element_gate();" in run_time_1_body
    assert "gyro_integrals();" in run_time_1_body
    assert run_time_1_body.index("a_run_mode_update_track_element_gate();") < run_time_1_body.index("gyro_integrals();")
    assert "track_gate_div_10" not in runner
    assert "a_run_mode_update_track_element_gate();" not in run_time_2_body
    assert "gyro_integrals();" not in run_time_2_body


def test_cylinder_peak_angle_api_is_split_from_element_state_machine():
    fuya_header = _read(FUYA_H)
    fuya_source = _read(FUYA_C)
    track_source = _read(A_RUN_TRACK_ELEMENT_C)

    assert "void fuya_apply_cylinder_peak_angle(void);" in fuya_header
    assert "void fuya_restore_cylinder_peak_angle(void);" in fuya_header
    assert "void fuya_apply_cylinder_peak_angle(void)" in fuya_source
    assert "void fuya_restore_cylinder_peak_angle(void)" in fuya_source

    assert "fuya_apply_cylinder_peak_angle();" in track_source
    assert "fuya_restore_cylinder_peak_angle();" in track_source
    assert "#define CYLINDER_TOP_GRAVITY_Z -0.3f" in track_source
    assert "#define CYLINDER_GROUND_GRAVITY_Z 0.3f" in track_source
    assert "CYLINDER_STABLE_DELAY_COUNT 50u" in track_source
    assert "LowPassFilter_t cylinder_vz_low_pass" in track_source
    assert "low_pass_filter_mt(&cylinder_vz_low_pass" in track_source
    assert "CYLINDER_VZ_FILTER_OLD" not in track_source
    assert "CYLINDER_VZ_FILTER_NEW" not in track_source


def test_fly_ramp_logic_is_split_behind_run_mode_wrapper():
    mode_source = _read(A_RUN_MODE_C)
    fly_source = _read(A_RUN_FLY_C)
    fly_header = _read(A_RUN_FLY_H)

    assert "void a_run_fly_update_speed(int *speed);" in fly_header
    assert "static int8 fly_is_acc_z_ramp_pose(void)" in fly_source
    assert "static int8 fly_is_ramp_lost_signal(void)" in fly_source
    assert "static int8 fly_is_center_line(void)" in fly_source
    assert "static void fly_reset_state(void)" in fly_source
    assert "void a_run_fly_update_speed(int *speed)" in fly_source
    assert "} FlyState;" in fly_header
    assert "FLY_STATE_IDLE = 0" in fly_header
    assert "enum FlyState" not in fly_source

    assert "void a_run_mode_update_fly_speed(int *speed)" in mode_source
    assert "a_run_fly_update_speed(speed);" in mode_source
    assert "fly_is_ramp_lost_signal" not in mode_source


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
    assert "ring_data.yaw_delta_sum" in menu_source
    assert "ring_data.encoder" in menu_source
    assert "ring_data.diff_set" in menu_source
    assert "a_run_mode_get_ring_yaw_delta_sum" not in menu_source
    assert "a_run_mode_get_ring_encoder" not in menu_source
    assert "a_run_mode_get_ring_diff_set" not in menu_source

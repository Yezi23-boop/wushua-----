from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
A_RUN_MODE_C = ROOT / "project" / "user" / "a_run_mode.c"
A_RUN_MODE_H = ROOT / "project" / "user" / "a_run_mode.h"
A_RUN_FLY_C = ROOT / "project" / "user" / "a_run_fly.c"
A_RUN_FLY_H = ROOT / "project" / "user" / "a_run_fly.h"
A_RUN_TRACK_ELEMENT_C = ROOT / "project" / "user" / "a_run_track_element.c"
A_RUN_TRACK_ELEMENT_H = ROOT / "project" / "user" / "a_run_track_element.h"
A_RUN_RING_C = ROOT / "project" / "user" / "a_run_ring.c"
A_RUN_RING_H = ROOT / "project" / "user" / "a_run_ring.h"
A_RUN_CYLINDER_C = ROOT / "project" / "user" / "a_run_cylinder.c"
A_RUN_WALL_C = ROOT / "project" / "user" / "a_run_wall.c"
IMU_C = ROOT / "project" / "user" / "imu.c"
ADC_C = ROOT / "project" / "user" / "ADC.c"
A_RUN_C = ROOT / "project" / "user" / "a_run.c"
FUYA_C = ROOT / "project" / "user" / "FUYA.c"
FUYA_H = ROOT / "project" / "user" / "FUYA.h"
EEPROM_H = ROOT / "project" / "service" / "eeprom.h"
EEPROM_C = ROOT / "project" / "service" / "eeprom.c"
MENU_C = ROOT / "project" / "service" / "menu.c"


def _read(path):
    return path.read_text(encoding="utf-8")


def _function_body(source, start_sig, next_sig):
    start = source.index(start_sig)
    end = source.index(next_sig, start)
    return source[start:end]


def test_track_element_gate_is_wired_directly_in_5ms_control_chain():
    mode_header = _read(A_RUN_MODE_H)
    mode_source = _read(A_RUN_MODE_C)
    track_source = _read(A_RUN_TRACK_ELEMENT_C)
    track_header = _read(A_RUN_TRACK_ELEMENT_H)
    runner = _read(A_RUN_C)

    assert "void a_run_mode_update_track_element_gate" not in mode_header
    assert "void a_run_track_element_update_gate(int *speed, float *angle_target);" in track_header
    assert "int8 a_run_track_element_get_expected_element(void);" in track_header
    assert "static enum TrackElement expected_element = ELEMENT_NONE;" in track_source

    run_time_1_body = _function_body(runner, "void run_time_1(void)", "void run_time_2(void)")
    run_time_2_body = _function_body(runner, "void run_time_2(void)", "void run_time_3(void)")

    assert "read_AD();" in run_time_1_body
    assert "Encoder_get(&PID.left_speed, &PID.right_speed);" in run_time_1_body
    assert "imu_update_gyro_z_from_imu660rc();" in run_time_1_body
    assert "if (steer_div_10 >= 2)" in run_time_1_body
    assert "if (steer_div_10 > 2)" not in run_time_1_body
    assert "pid_steer_update(&PID.steer, Err, 0.0f);" in run_time_1_body
    assert "a_run_track_element_update_gate(&speed_active, &PID.steer.output);" in run_time_1_body
    assert "pid_angle_update(&PID.angle, PID.steer.output, gyro_z * app.angle.gyro_feedback_scale);" in run_time_1_body
    assert run_time_1_body.index("pid_steer_update(&PID.steer, Err, 0.0f);") < run_time_1_body.index(
        "a_run_track_element_update_gate(&speed_active, &PID.steer.output);"
    )
    assert "a_run_track_element_update_gate" not in mode_source
    assert "a_run_track_element_update_gate" not in run_time_2_body


def test_track_element_sequence_supports_current_executable_elements():
    source = _read(A_RUN_TRACK_ELEMENT_C)
    header = _read(A_RUN_TRACK_ELEMENT_H)

    assert "#define TRACK_ELEMENT_LEFT_RING 1" in header
    assert "#define TRACK_ELEMENT_RIGHT_RING 2" in header
    assert "#define TRACK_ELEMENT_CYLINDER 3" in header
    assert "#define TRACK_ELEMENT_WALL 4" in header
    assert "#define TRACK_ELEMENT_SEESAW 5" in header
    assert "#define TRACK_ELEMENT_SEQUENCE_MAX 6" in header
    assert "#define TRACK_ELEMENT_DEFAULT_LEN 4" in header

    assert "app.start.element_enable != 1" in source
    assert "track_element_enter_from_index(0);" in source
    assert "element = app.start.element_seq[index];" in source
    assert "track_element_is_executable(element)" in source

    assert "element == ELEMENT_LEFT_RING" in source
    assert "element == ELEMENT_RIGHT_RING" in source
    assert "element == ELEMENT_CYLINDER" in source
    assert "element == ELEMENT_WALL" in source
    assert "element == ELEMENT_SEESAW" in source

    assert "a_run_ring_update_5ms(1)" in source
    assert "a_run_ring_update_5ms(-1)" in source
    assert "a_run_cylinder_update_5ms()" in source
    assert "a_run_fly_update_speed(speed, 1)" in source
    assert "a_run_wall_update_5ms()" in source
    assert "a_run_fly_update_release_speed(speed);" in source
    assert "a_run_ring_update_angle_target(angle_target);" in source


def test_ring_state_is_split_and_directional():
    ring_source = _read(A_RUN_RING_C)
    ring_header = _read(A_RUN_RING_H)
    track_source = _read(A_RUN_TRACK_ELEMENT_C)

    assert "} RingStruct;" in ring_header
    assert "extern RingStruct ring_data;" in ring_header
    assert "RingStruct ring_data = {0};" in ring_source
    assert "uint8 a_run_ring_update_5ms(int8 ring_dir)" in ring_source
    assert "ring_data.diff_set = app.ring.pre_ring_Gyro_target * ring_dir;" in ring_source
    assert "ring_data.diff_set = app.ring.pre_out_ring_Gyro_target * ring_dir;" in ring_source
    assert "ring_data.yaw_delta_sum += delta_angle;" in ring_source
    assert "ring_data.encoder += (speed_l + speed_r) * 0.005;" in ring_source
    assert "a_run_ring_update_5ms(1)" in track_source
    assert "a_run_ring_update_5ms(-1)" in track_source


def test_cylinder_wall_and_fly_are_separate_simple_state_machines():
    cylinder_source = _read(A_RUN_CYLINDER_C)
    wall_source = _read(A_RUN_WALL_C)
    fly_source = _read(A_RUN_FLY_C)
    fly_header = _read(A_RUN_FLY_H)

    assert "enum CylinderStep" in cylinder_source
    assert "CYLINDER_TOP_WINDOW_COUNT 100u" in cylinder_source
    assert "CYLINDER_TOP_HIT_COUNT 3" in cylinder_source
    assert "CYLINDER_STABLE_DELAY_COUNT 100u" in cylinder_source
    assert "uint8 a_run_cylinder_update_5ms(void)" in cylinder_source

    assert "enum WallStep" in wall_source
    assert "WALL_TIMING_COUNT 200u" in wall_source
    assert "uint8 a_run_wall_update_5ms(void)" in wall_source

    assert "} FlyState;" in fly_header
    assert "void a_run_fly_update_speed(int *speed, uint8 allow_entry);" in fly_header
    assert "void a_run_fly_update_release_speed(int *speed);" in fly_header
    assert "volatile uint8 fly_lost_line_blocked = 0;" in fly_source
    assert "volatile int32 fly_pwm_output_limit = 0;" in fly_source
    assert "FLY_STATE_HOLD" in fly_source
    assert "FLY_STATE_RECOVER" in fly_source
    assert "FLY_STATE_COOLDOWN" in fly_source
    assert "fly_finish_event = 1;" in fly_source


def test_adc_uses_cylinder_abc_while_expected_element_is_cylinder():
    source = _read(ADC_C)
    dispose_body = _function_body(source, "static void dispose", "/**\n * @brief 动态扫描")

    assert "#define ADC_CYLINDER_A_1 1.00f" in source
    assert "#define ADC_CYLINDER_B_1 1.00f" in source
    assert "#define ADC_CYLINDER_C_L 0.80f" in source
    assert "float a_value;" in dispose_body
    assert "float b_value;" in dispose_body
    assert "float c_value;" in dispose_body
    assert "a_run_track_element_get_expected_element() == TRACK_ELEMENT_CYLINDER" in dispose_body
    assert "a_value = ADC_CYLINDER_A_1;" in dispose_body
    assert "b_value = ADC_CYLINDER_B_1;" in dispose_body
    assert "c_value = ADC_CYLINDER_C_L;" in dispose_body
    assert "numer = a_value * (float)ad11 - (float)ad44 +" in dispose_body
    assert "b_value * (float)ad22 - (float)ad33;" in dispose_body
    assert "denom = a_value * (float)ad11 + (float)ad44 +" in dispose_body
    assert "c_value * (float)func_abs(diff23);" in dispose_body
    assert "app.angle.A_1 * (float)ad11" not in dispose_body
    assert "app.angle.B_1 * (float)ad22" not in dispose_body
    assert "app.angle.C_l * (float)func_abs(diff23)" not in dispose_body


def test_imu_and_fuya_match_current_fixed_output_strategy():
    imu_source = _read(IMU_C)
    imu_header = _read(ROOT / "project" / "user" / "imu.h")
    fuya_source = _read(FUYA_C)
    fuya_header = _read(FUYA_H)
    runner = _read(A_RUN_C)

    assert "#define IMU_GYRO_Z_SIGN (1.0f)" in imu_source
    assert "void imu_update_gravity_vz_from_roll(void)" in imu_source
    assert "float imu_get_gravity_vz(void)" in imu_source
    assert "imu_update_gravity_vz_from_quaternion" not in imu_source
    assert "imu_update_gravity_vz_from_quaternion" not in imu_header

    assert "void fuya_set_percent(float percent);" in fuya_header
    assert "void fuya_stop(void);" in fuya_header
    assert "fuya_percent_to_pwm" in fuya_source
    assert "pwm_init(PWMA_CH2N_P03, 50, FUYA_PWM_MIN);" in fuya_source
    assert "void fuya_apply_cylinder_peak_angle" not in fuya_header
    assert "void fuya_apply_cylinder_peak_angle" not in fuya_source
    assert "fuya_set_percent(app.start.fuya_xili);" in runner


def test_track_mode_config_and_menu_reflect_current_debug_state():
    eeprom_header = _read(EEPROM_H)
    eeprom_source = _read(EEPROM_C)
    menu_source = _read(MENU_C)

    assert "int16 track_mode;" in eeprom_header
    assert "config->start.track_mode = 0;" in eeprom_source
    assert "config->start.track_mode = (int16)read_int(" in eeprom_source
    assert "save_int(config->start.track_mode" in eeprom_source

    assert "a_run_track_element_get_expected_element()" in menu_source
    assert "a_run_ring_get_state()" in menu_source
    assert "a_run_cylinder_get_state()" in menu_source
    assert "a_run_wall_get_state()" in menu_source
    assert "ring_data.yaw_delta_sum" in menu_source
    assert "ring_data.encoder" in menu_source
    assert "ring_data.diff_set" in menu_source
    assert '"trk_mode"' not in menu_source

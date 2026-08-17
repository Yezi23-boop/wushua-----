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
A_RUN_CYLINDER_H = ROOT / "project" / "user" / "a_run_cylinder.h"
A_RUN_WALL_C = ROOT / "project" / "user" / "a_run_wall.c"
A_RUN_WALL_H = ROOT / "project" / "user" / "a_run_wall.h"
A_RUN_CROSS_C = ROOT / "project" / "user" / "a_run_cross.c"
A_RUN_CROSS_H = ROOT / "project" / "user" / "a_run_cross.h"
IMU_C = ROOT / "project" / "user" / "imu.c"
ADC_C = ROOT / "project" / "user" / "ADC.c"
A_RUN_C = ROOT / "project" / "user" / "a_run.c"
FUYA_C = ROOT / "project" / "user" / "FUYA.c"
FUYA_H = ROOT / "project" / "user" / "FUYA.h"
EEPROM_H = ROOT / "project" / "service" / "eeprom.h"
EEPROM_C = ROOT / "project" / "service" / "eeprom.c"
MENU_C = ROOT / "project" / "service" / "menu.c"
MOTOR_C = ROOT / "project" / "service" / "motor.c"
PID_C = ROOT / "project" / "service" / "pid.c"


def _read(path):
    return path.read_text(encoding="utf-8")


def _function_body(source, start_sig, next_sig):
    start = source.index(start_sig)
    end = source.index(next_sig, start)
    return source[start:end]


def test_track_element_gate_is_wired_directly_in_2ms_control_chain():
    mode_header = _read(A_RUN_MODE_H)
    mode_source = _read(A_RUN_MODE_C)
    track_source = _read(A_RUN_TRACK_ELEMENT_C)
    track_header = _read(A_RUN_TRACK_ELEMENT_H)
    runner = _read(A_RUN_C)

    assert "void a_run_mode_update_track_element_gate" not in mode_header
    assert "void a_run_track_element_update_gate(float *speed, float *angle_target);" in track_header
    assert "int8 a_run_track_element_get_expected_element(void);" in track_header
    assert "static enum TrackElement expected_element = ELEMENT_NONE;" in track_source

    run_time_1_body = _function_body(
        runner, "void run_time_1(void)", "void run_time_2(void)")
    run_time_2_body = _function_body(
        runner, "void run_time_2(void)", "void run_time_3(void)")

    assert "read_AD();" in run_time_1_body
    assert "Encoder_get(&PID.left_speed, &PID.right_speed);" in run_time_1_body
    assert "imu_update_gyro_z_from_imu660rc();" in run_time_1_body
    assert "if (steer_div_10 >= 3)" in run_time_1_body
    assert "if (steer_div_10 >= 2)" not in run_time_1_body
    assert "pid_steer_update(&PID.steer, Err, 0.0f);" in run_time_1_body
    assert "static float speed_active = 0.0f;" in runner
    assert "static int speed_active" not in runner
    assert "speed_active = app.speed.speed_run;" in run_time_1_body
    assert "a_run_track_element_update_gate(&speed_active, &PID.steer.output);" in run_time_1_body
    assert "pid_angle_update(&PID.angle, PID.steer.output, gyro_z * app.angle.gyro_feedback_scale);" in run_time_1_body
    assert run_time_1_body.index("pid_steer_update(&PID.steer, Err, 0.0f);") < run_time_1_body.index(
        "a_run_track_element_update_gate(&speed_active, &PID.steer.output);"
    )
    assert "a_run_track_element_update_gate" not in mode_source
    assert "a_run_track_element_update_gate" not in run_time_2_body


def test_main_control_uses_stable_nonlinear_differential_distribution():
    pid_source = _read(PID_C)
    pid_header = _read(ROOT / "project" / "service" / "pid.h")
    runner = _read(A_RUN_C)
    differential_body = pid_source[pid_source.index("void Pid_Differential("):]
    run_time_1_body = _function_body(
        runner, "void run_time_1(void)", "void run_time_2(void)")

    assert "void Pid_Differential(float speed_run, float diff_output," in pid_source
    assert "float scope, float inner_gain, float outer_gain)" in pid_source
    assert "void Pid_Differential(float speed_run, float diff_output," in pid_header
    assert "float scope, float inner_gain, float outer_gain);" in pid_header
    assert "PID.steer.output" not in differential_body
    assert "ratio = func_abs(diff_output) / scope;" in differential_body
    assert "ratio = ratio * (0.6f + 0.4f * ratio);" in differential_body
    assert "inner_scale = 1.0f - inner_gain * ratio;" in differential_body
    assert "outer_scale = 1.0f + outer_gain * ratio;" in differential_body
    assert "app.speed.diff_inner_gain" not in differential_body
    assert "app.speed.diff_outer_gain" not in differential_body
    assert "if (app.speed.diff_enable != 0)" in run_time_1_body
    assert "Pid_Differential(speed_active, PID.angle.output," in run_time_1_body
    assert "app.angle.limiting_Angle," in run_time_1_body
    assert "diff_inner_gain, diff_outer_gain);" in run_time_1_body
    assert "left_target = speed_active - PID.angle.output;" in run_time_1_body
    assert "right_target = speed_active + PID.angle.output;" in run_time_1_body


def test_imu_drops_wall_pitch_history_after_wall_uses_adc_only():
    imu_source = _read(IMU_C)
    imu_header = _read(ROOT / "project" / "user" / "imu.h")

    assert "IMU_PITCH_WALL_WINDOW_MS" not in imu_header
    assert "imu_get_pitch_current_x10" not in imu_header
    assert "imu_get_pitch_wall_window_ago_x10" not in imu_header
    assert "imu_pitch_history" not in imu_source
    assert "imu_get_pitch_current_x10" not in imu_source
    assert "imu_get_pitch_wall_window_ago_x10" not in imu_source


def test_imu_and_fuya_match_current_fixed_output_strategy():
    imu_source = _read(IMU_C)
    imu_header = _read(ROOT / "project" / "user" / "imu.h")
    fuya_source = _read(FUYA_C)
    fuya_header = _read(FUYA_H)
    runner = _read(A_RUN_C)

    assert "#define IMU_GYRO_Z_SIGN (1.0f)" in imu_source
    assert "imu_update_gravity_vz_from_roll" not in imu_source
    assert "imu_get_gravity_vz" not in imu_source
    assert "imu_roll_delta_deg" not in imu_source
    assert "extern float acc_1;" not in imu_header
    assert "imu_update_gravity_vz_from_roll" not in imu_header
    assert "imu_get_gravity_vz" not in imu_header
    assert "imu_update_gravity_vz_from_roll" not in runner
    assert "imu_update_gravity_vz_from_quaternion" not in imu_source
    assert "imu_update_gravity_vz_from_quaternion" not in imu_header

    assert "void fuya_set_percent(float percent);" in fuya_header
    assert "void fuya_stop(void);" in fuya_header
    assert "fuya_percent_to_pwm" in fuya_source
    assert "pwm_init(PWMA_CH2N_P03, 50, FUYA_PWM_MIN);" in fuya_source
    assert "void fuya_apply_cylinder_peak_angle" not in fuya_header
    assert "void fuya_apply_cylinder_peak_angle" not in fuya_source
    assert "fuya_set_percent(app.start.fuya_xili);" in runner


def test_fly_menu_draw_puts_seesaw_mode_on_first_editable_row():
    menu_source = _read(MENU_C)

    assert '{"<<SEESAW", menu_fly_items' in menu_source
    assert '"mode:"' not in menu_source
    assert menu_source.count('"seesaw_mode", &app.fly.seesaw_mode') == 2
    assert 'MENU_META(MENU_ITEM_BOOL, 1, 0)' in menu_source
    assert '"fly_speed", &app.fly.fly_speed' in menu_source
    assert '"seesaw_spd", &app.fly.seesaw_speed' in menu_source


def test_fly_menu_exposes_land_confirm_count_in_fly_mode():
    menu_source = _read(MENU_C)

    assert '"land_cnt", &app.fly.fly_land_confirm_count' in menu_source
    assert 'MENU_META(MENU_ITEM_INT16, 4, 0), MENU_INT_STEP_1}' in menu_source


def test_cross_menu_entry_is_reachable_and_has_subpage():
    menu_source = _read(MENU_C)

    assert '{"CROSS", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_CROSS}' in menu_source
    assert '{"<<CROSS", menu_cross_items, MENU_ITEM_COUNT(menu_cross_items), MENU_PAGE_YUANSHU}' in menu_source
    assert '"enc_target", &app.cross.encoder_target' in menu_source
    assert '"adc_a_1", &app.cross.adc_a_1' in menu_source
    assert '"adc_b_1", &app.cross.adc_b_1' in menu_source
    assert '"adc_c_l", &app.cross.adc_c_l' in menu_source


def test_eeprom_fly_config_no_longer_uses_readback_guards():
    eeprom_source = _read(EEPROM_C)

    assert "if (config->fly.seesaw_mode != 0)" not in eeprom_source
    assert "if (config->fly.seesaw_speed < 0 || config->fly.seesaw_speed > 200)" not in eeprom_source
    assert "if (config->fly.seesaw_release_step < 0.0f || config->fly.seesaw_release_step > 10.0f)" not in eeprom_source

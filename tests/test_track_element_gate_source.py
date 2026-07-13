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

    run_time_1_body = _function_body(runner, "void run_time_1(void)", "void run_time_2(void)")
    run_time_2_body = _function_body(runner, "void run_time_2(void)", "void run_time_3(void)")

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


def test_encoder_total_distance_stops_at_32_meters():
    pid_source = _read(PID_C)
    encoder_body = _function_body(
        pid_source,
        "void Encoder_get(PID_Speed *left, PID_Speed *right)",
        "/**\n * @brief 速度环 PID 更新",
    )

    assert "#define ENCODER_STOP_DISTANCE_CM 3200.0f" in pid_source
    assert "static float encoder_sum = 0.0f;" in encoder_body
    assert "encoder_sum += (speed_l + speed_r) * 0.5f * 0.012f;" in encoder_body
    assert "if (encoder_sum >= ENCODER_STOP_DISTANCE_CM)" in encoder_body
    assert "stop = 1;" in encoder_body


def test_track_element_sequence_supports_current_executable_elements():
    source = _read(A_RUN_TRACK_ELEMENT_C)
    header = _read(A_RUN_TRACK_ELEMENT_H)
    eeprom_source = _read(EEPROM_C)

    assert "#define TRACK_ELEMENT_LEFT_RING 1" in header
    assert "#define TRACK_ELEMENT_RIGHT_RING 2" in header
    assert "#define TRACK_ELEMENT_CYLINDER 3" in header
    assert "#define TRACK_ELEMENT_WALL 4" in header
    assert "#define TRACK_ELEMENT_SEESAW 5" in header
    assert "#define TRACK_ELEMENT_CROSS 6" in header
    assert "#define TRACK_ELEMENT_SEQUENCE_MAX 6" in header
    assert "#define TRACK_ELEMENT_DEFAULT_LEN 5" in header

    assert "app.start.element_enable != 1" in source
    assert "track_element_enter_from_index(0);" in source
    assert "element = app.start.element_seq[index];" in source
    assert "track_element_is_executable(element)" in source

    assert "element == ELEMENT_LEFT_RING" in source
    assert "element == ELEMENT_RIGHT_RING" in source
    assert "element == ELEMENT_CYLINDER" in source
    assert "element == ELEMENT_WALL" in source
    assert "element == ELEMENT_SEESAW" in source
    assert "element == ELEMENT_CROSS" in source

    assert "a_run_ring_update_5ms(1)" in source
    assert "a_run_ring_update_5ms(-1)" in source
    assert "a_run_cylinder_update_5ms()" in source
    assert "a_run_fly_update_speed(speed, 1)" in source
    assert "a_run_wall_update_5ms(speed)" in source
    assert "a_run_cross_update_5ms()" in source
    assert "a_run_fly_update_release_speed(speed);" in source
    assert "a_run_ring_update_angle_target(angle_target);" in source

    assert "config->start.element_seq[2] = TRACK_ELEMENT_SEESAW;" in eeprom_source
    assert "config->start.element_seq[3] = TRACK_ELEMENT_CROSS;" in eeprom_source
    assert "config->start.element_seq[4] = TRACK_ELEMENT_LEFT_RING;" in eeprom_source


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
    assert "#define RING_ENTRY_CONFIRM_COUNT 8u" in ring_source
    assert "#define RING_YAW_DT_SCALE 0.40f" in ring_source
    assert "ring_data.yaw_delta_sum += delta_angle * RING_YAW_DT_SCALE;" in ring_source
    assert "ring_data.encoder += (speed_l + speed_r) * 0.002;" in ring_source
    assert "a_run_ring_update_5ms(1)" in track_source
    assert "a_run_ring_update_5ms(-1)" in track_source


def test_ring_entry_hits_accumulate_inside_timeout_window():
    ring_source = _read(A_RUN_RING_C)
    update_body = _function_body(
        ring_source,
        "uint8 a_run_ring_update_5ms(int8 ring_dir)",
        "/**\n * @brief 更新环岛判定所需的里程与转角量。",
    )
    no_ring_body = update_body[
        update_body.index("case no_ring:"):update_body.index("case ring:")
    ]

    assert (
        "if (entry_signal != 0)\n"
        "        {\n"
        "            ring_entry_count++;\n"
        "        }\n"
        "\n"
        "        if (ring_entry_count > 0)" in no_ring_body
    )
    assert (
        "else\n"
        "        {\n"
        "            ring_entry_count = 0;\n"
        "            timedestroy(&ring_data.time_l);\n"
        "        }" not in no_ring_body
    )
    assert "else if (timeadd(&ring_data.time_l, 1000))" in no_ring_body
    assert "ring_entry_count = 0;" in no_ring_body
    assert "timedestroy(&ring_data.time_l);" in no_ring_body


def test_cylinder_wall_and_fly_are_separate_simple_state_machines():
    cylinder_source = _read(A_RUN_CYLINDER_C)
    wall_source = _read(A_RUN_WALL_C)
    fly_source = _read(A_RUN_FLY_C)
    fly_header = _read(A_RUN_FLY_H)

    assert "enum CylinderStep" in cylinder_source
    assert "CYLINDER_TOP_WINDOW_COUNT 250u" in cylinder_source
    assert "CYLINDER_TOP_HIT_COUNT 8" in cylinder_source
    assert "CYLINDER_STABLE_DELAY_COUNT 250u" in cylinder_source
    assert "uint8 a_run_cylinder_update_5ms(void)" in cylinder_source

    assert "enum WallStep" in wall_source
    assert "WALL_TIMING_COUNT 500u" in wall_source
    assert "uint8 a_run_wall_update_5ms(void)" in wall_source

    assert "} FlyState;" in fly_header
    assert "void a_run_fly_update_speed(float *speed, uint8 allow_entry);" in fly_header
    assert "void a_run_fly_update_release_speed(float *speed);" in fly_header
    assert "static float fly_release_speed = 0.0f;" in fly_source
    assert "float target_speed;" in fly_source
    assert "target_speed = app.speed.speed_run;" in fly_source
    assert "(int)app.speed.speed_run" not in fly_source
    assert "volatile uint8 fly_lost_line_blocked = 0;" in fly_source
    assert "volatile int32 fly_pwm_output_limit = 0;" in fly_source
    assert "FLY_STATE_HOLD" in fly_source
    assert "FLY_STATE_RECOVER" in fly_source
    assert "FLY_STATE_COOLDOWN" in fly_source
    assert "#define FLY_RECOVER_LINE_STABLE_COUNT 25u" in fly_source
    assert "#define FLY_RECOVER_PWM_LIMIT_EARLY_COUNT 150u" in fly_source
    assert "#define FLY_RECOVER_LOST_LINE_ENABLE_COUNT 500u" in fly_source
    assert "fly_finish_event = 1;" in fly_source


def test_element_state_interfaces_use_public_typed_enums():
    ring_header = _read(A_RUN_RING_H)
    ring_source = _read(A_RUN_RING_C)
    cylinder_header = _read(A_RUN_CYLINDER_H)
    cylinder_source = _read(A_RUN_CYLINDER_C)
    wall_header = _read(A_RUN_WALL_H)
    wall_source = _read(A_RUN_WALL_C)
    cross_header = _read(A_RUN_CROSS_H)
    cross_source = _read(A_RUN_CROSS_C)
    fly_header = _read(A_RUN_FLY_H)
    fly_source = _read(A_RUN_FLY_C)
    runner = _read(A_RUN_C)
    track_source = _read(A_RUN_TRACK_ELEMENT_C)

    expected_interfaces = (
        (ring_header, ring_source, "RingState", "ring_state", "RING_STATE_IDLE"),
        (cylinder_header, cylinder_source, "CylinderState", "cylinder_state", "CYLINDER_STATE_IDLE"),
        (wall_header, wall_source, "WallState", "wall_state", "WALL_STATE_IDLE"),
        (cross_header, cross_source, "CrossState", "cross_state", "CROSS_STATE_IDLE"),
        (fly_header, fly_source, "FlyState", "fly_state", "FLY_STATE_IDLE"),
        (fly_header, fly_source, "SeesawState", "seesaw_state", "SEESAW_STATE_IDLE"),
    )

    for header, source, state_type, state_var, idle_state in expected_interfaces:
        assert f"}} {state_type};" in header
        assert f"static {state_type} {state_var} = {idle_state};" in source

    assert "RingState a_run_ring_get_state(void);" in ring_header
    assert "CylinderState a_run_cylinder_get_state(void);" in cylinder_header
    assert "WallState a_run_wall_get_state(void);" in wall_header
    assert "CrossState a_run_cross_get_state(void);" in cross_header
    assert "FlyState a_run_fly_get_state(void);" in fly_header
    assert "SeesawState a_run_seesaw_get_state(void);" in fly_header
    assert "volatile int flat_fly" not in runner
    assert "extern volatile int flat_fly" not in _read(ROOT / "project" / "user" / "a_run.h")
    assert "a_run_fly_get_state() != FLY_STATE_COOLDOWN" in track_source


def test_cross_timing_uses_dedicated_adc_weights():
    adc_source = _read(ADC_C)
    eeprom_header = _read(EEPROM_H)
    eeprom_source = _read(EEPROM_C)
    menu_source = _read(MENU_C)

    assert "float adc_a_1;              /**< 双十字专用横向主差分权重。 */" in eeprom_header
    assert "float adc_b_1;              /**< 双十字专用竖向差分权重。 */" in eeprom_header
    assert "float adc_c_l;              /**< 双十字专用分母补偿权重。 */" in eeprom_header

    assert "config->cross.adc_a_1 = 1.00f;" in eeprom_source
    assert "config->cross.adc_b_1 = 1.00f;" in eeprom_source
    assert "config->cross.adc_c_l = 1.00f;" in eeprom_source
    assert "config->cross.adc_a_1 = read_float(57);" in eeprom_source
    assert "config->cross.adc_b_1 = read_float(58);" in eeprom_source
    assert "config->cross.adc_c_l = read_float(59);" in eeprom_source
    assert "save_float(config->cross.adc_a_1, 57);" in eeprom_source
    assert "save_float(config->cross.adc_b_1, 58);" in eeprom_source
    assert "save_float(config->cross.adc_c_l, 59);" in eeprom_source

    assert "if (a_run_cross_get_state() == CROSS_STATE_TIMING)" in adc_source
    assert "a_value = app.cross.adc_a_1;" in adc_source
    assert "b_value = app.cross.adc_b_1;" in adc_source
    assert "c_value = app.cross.adc_c_l;" in adc_source
    assert adc_source.index("if (seesaw_centering_active != 0)") < adc_source.index(
        "if (a_run_cross_get_state() == CROSS_STATE_TIMING)"
    )

    assert '"adc_a_1", &app.cross.adc_a_1' in menu_source
    assert '"adc_b_1", &app.cross.adc_b_1' in menu_source
    assert '"adc_c_l", &app.cross.adc_c_l' in menu_source
    assert menu_source.count("MENU_FLOAT_STEP_01") >= 3


def test_cylinder_confirmation_starts_immediate_ramp_deceleration():
    cylinder_header = _read(A_RUN_CYLINDER_H)
    cylinder_source = _read(A_RUN_CYLINDER_C)
    track_source = _read(A_RUN_TRACK_ELEMENT_C)
    runner = _read(A_RUN_C)
    adc_source = _read(ADC_C)
    eeprom_header = _read(EEPROM_H)
    eeprom_source = _read(EEPROM_C)
    menu_source = _read(MENU_C)

    assert "CYLINDER_STATE_DECEL = 2" in cylinder_header
    assert "CYLINDER_STATE_RELEASE = 3" in cylinder_header
    assert "CYLINDER_STATE_WAIT_GROUND" not in cylinder_header
    assert "CYLINDER_STATE_EXIT_SLOW" not in cylinder_header
    assert "uint8 a_run_cylinder_update_5ms(float *speed);" in cylinder_header
    assert "void a_run_cylinder_update_release_speed(float *speed);" in cylinder_header
    assert "uint8 a_run_cylinder_update_5ms(float *speed)" in cylinder_source
    assert "a_run_cylinder_update_5ms(speed)" in track_source

    assert "#define CYLINDER_SPEED_RAMP_STEP 0.2f" in cylinder_source
    assert "static float cylinder_ramp_speed = 0.0f;" in cylinder_source
    assert "cylinder_ramp_speed = *speed;" in cylinder_source
    assert "cylinder_state = CYLINDER_STATE_DECEL;" in cylinder_source
    assert "case CYLINDER_STATE_DECEL:" in cylinder_source
    assert "CYLINDER_STATE_WAIT_GROUND" not in cylinder_source
    assert "CYLINDER_STATE_EXIT_SLOW" not in cylinder_source
    assert "exit_slow_distance" not in cylinder_source
    assert "cylinder_ramp_speed -= CYLINDER_SPEED_RAMP_STEP;" in cylinder_source
    assert "cylinder_ramp_speed < (float)exit_slow_speed" in cylinder_source
    assert "*speed = cylinder_ramp_speed;" in cylinder_source
    assert "*speed = (float)exit_slow_speed;" not in cylinder_source
    assert "cylinder_state = CYLINDER_STATE_RELEASE;" in cylinder_source
    assert "void a_run_cylinder_update_release_speed(float *speed)" in cylinder_source
    assert "cylinder_ramp_speed += CYLINDER_SPEED_RAMP_STEP;" in cylinder_source

    release_call = "a_run_cylinder_update_release_speed(speed);"
    assert release_call in track_source
    assert track_source.index(release_call) < track_source.index("switch (expected_element)")
    assert "element == ELEMENT_CYLINDER ||" in track_source
    assert "a_run_cylinder_get_state() != CYLINDER_STATE_RELEASE" in track_source

    assert "int exit_slow_speed;" in eeprom_header
    assert "exit_slow_distance" not in eeprom_header
    assert "config->cylinder.exit_slow_speed = (int)read_int(26);" in eeprom_source
    assert "save_int(config->cylinder.exit_slow_speed, 26);" in eeprom_source
    assert "read_float(27)" not in eeprom_source
    assert "save_float(config->cylinder.exit_slow_distance, 27)" not in eeprom_source

    assert "cylinder_state == CYLINDER_STATE_DECEL" in adc_source
    assert "CYLINDER_STATE_WAIT_GROUND" not in adc_source
    assert "CYLINDER_STATE_EXIT_SLOW" not in adc_source
    assert "cylinder_state == CYLINDER_STATE_DECEL" in runner
    assert "CYLINDER_STATE_WAIT_GROUND" not in runner
    assert "CYLINDER_STATE_EXIT_SLOW" not in runner

    assert '"exit_spd", &app.cylinder.exit_slow_speed' in menu_source
    assert '"exit_dist"' not in menu_source
    cylinder_menu = menu_source.split("static const MenuItemDef menu_cylinder_items[] = {", 1)[1].split("};", 1)[0]
    assert cylinder_menu.count("MENU_META(") == 6
    assert '"cyl_kp"' not in menu_source
    assert '"cyl_kd"' not in menu_source
    assert 'MENU_META(MENU_ITEM_INT16, 4, 0), MENU_INT_STEP_5}' in menu_source


def test_wall_entry_uses_adc_sum_threshold():
    wall_source = _read(A_RUN_WALL_C)
    wall_header = _read(A_RUN_WALL_H)
    eeprom_source = _read(EEPROM_C)
    eeprom_header = _read(EEPROM_H)
    menu_source = _read(MENU_C)
    update_body = _function_body(
        wall_source,
        "uint8 a_run_wall_update_5ms(float *speed)",
        "    return 0;\n}",
    )

    assert "#define WALL_AD_SUM_THRESHOLD 100u" in wall_source
    assert "uint16 ad_sum;" in update_body
    assert "ad_sum = ad1 + ad2 + ad3 + ad4;" in update_body
    assert "if (ad_sum > WALL_AD_SUM_THRESHOLD)" in update_body
    assert "wall_state = WALL_TIMING;" in update_body
    assert "side_valid" not in update_body
    assert "high_valid" not in update_body
    assert "WALL_PITCH" not in wall_source
    assert "wall_pitch" not in wall_source
    assert "imu_get_pitch" not in wall_source
    assert "pitch" not in wall_header
    assert "static float wall_encoder_sum = 0.0f;" in wall_source
    assert "wall_encoder_sum = 0.0f;" in wall_source
    assert "wall_encoder_sum += (speed_l + speed_r) * 0.5f * 0.012f;" in update_body
    assert "if (wall_timer_count >= (uint16)timing_count ||" in update_body
    assert "wall_encoder_sum >= app.wall.encoder_target)" in update_body
    assert "float encoder_target; /**< 墙面退出编码器积分阈值。 */" in eeprom_header
    assert "config->wall.encoder_target =" in eeprom_source
    assert '"wall_enc", &app.wall.encoder_target' in menu_source
    assert 'MENU_META(MENU_ITEM_FLOAT, 4, 1), MENU_FLOAT_STEP_1}' in menu_source


def test_imu_drops_wall_pitch_history_after_wall_uses_adc_only():
    imu_source = _read(IMU_C)
    imu_header = _read(ROOT / "project" / "user" / "imu.h")

    assert "IMU_PITCH_WALL_WINDOW_MS" not in imu_header
    assert "imu_get_pitch_current_x10" not in imu_header
    assert "imu_get_pitch_wall_window_ago_x10" not in imu_header
    assert "imu_pitch_history" not in imu_source
    assert "imu_get_pitch_current_x10" not in imu_source
    assert "imu_get_pitch_wall_window_ago_x10" not in imu_source


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


def test_seesaw_recover_speed_reuses_creep_speed():
    fly_source = _read(A_RUN_FLY_C)
    eeprom_header = _read(EEPROM_H)
    eeprom_source = _read(EEPROM_C)
    menu_source = _read(MENU_C)

    assert "SEESAW_RECOVER_SPEED" not in fly_source
    assert "fly_release_speed = (float)app.fly.seesaw_speed;" in fly_source
    assert "app.fly.seesaw_recover_speed" not in fly_source

    assert "int seesaw_recover_speed;" not in eeprom_header
    assert "config->fly.seesaw_recover_speed" not in eeprom_source
    assert "app.fly.seesaw_recover_speed" not in menu_source
    assert 'ips114_show_string(16, 6 * MENU_ROW_HEIGHT, "recover_spd");' not in menu_source


def test_seesaw_wait_state_machine_uses_global_stop_only_at_hold_boundaries():
    fly_header = _read(A_RUN_FLY_H)
    fly_source = _read(A_RUN_FLY_C)
    motor_source = _read(MOTOR_C)
    eeprom_source = _read(EEPROM_C)
    seesaw_body = _function_body(
        fly_source,
        "void a_run_seesaw_update_speed(float *speed, uint8 allow_entry)",
        "void a_run_fly_update_release_speed(float *speed)",
    )
    reset_body = _function_body(
        fly_source,
        "void a_run_seesaw_reset(void)",
        "/**\n * @brief 跷跷板停止等待模式速度状态机。",
    )

    for state in (
        "SEESAW_STATE_IDLE = 0",
        "SEESAW_STATE_BRAKE = 1",
        "SEESAW_STATE_CREEP = 2",
        "SEESAW_STATE_HOLD_DELAY = 3",
        "SEESAW_STATE_WAIT_SIGNAL = 4",
        "SEESAW_STATE_RELEASE = 5",
    ):
        assert state in fly_header

    for old_state in (
        "SEESAW_STATE_STOP =",
        "SEESAW_STATE_WAIT =",
        "SEESAW_STATE_CHECK =",
        "SEESAW_STATE_RECOVER =",
        "SEESAW_STATE_COOLDOWN =",
    ):
        assert old_state not in fly_header
        assert old_state not in fly_source

    assert seesaw_body.count("stop = 1;") == 1
    assert seesaw_body.count("stop = 0;") == 1
    assert "if (seesaw_wait_count == 0)" in seesaw_body
    assert "seesaw_state = SEESAW_STATE_HOLD_DELAY;" in seesaw_body
    assert "seesaw_state = SEESAW_STATE_WAIT_SIGNAL;" in seesaw_body
    assert "seesaw_state = SEESAW_STATE_RELEASE;" in seesaw_body
    assert "seesaw_hold_active" not in fly_header
    assert "seesaw_hold_active" not in fly_source
    assert "seesaw_hold_active" not in motor_source

    assert "seesaw_state == SEESAW_STATE_HOLD_DELAY ||" in reset_body
    assert "seesaw_state == SEESAW_STATE_WAIT_SIGNAL" in reset_body
    assert reset_body.count("stop = 0;") == 1

    assert "if (stop == 0)" in motor_source
    assert "motor_update_start_pwm_ramp(0, 0);" in motor_source
    assert "pwm_set_duty(PWMB_CH2_P13, 100);" in motor_source
    assert "pwm_set_duty(PWMB_CH3_P52, 100);" in motor_source
    assert "config->fly.seesaw_wait_count = 10;" in eeprom_source
    assert "10 * 2ms = 20ms" in eeprom_source


def test_cross_menu_entry_is_reachable_and_has_subpage():
    menu_source = _read(MENU_C)

    assert '{"CROSS", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_CROSS}' in menu_source
    assert '{"<<CROSS", menu_cross_items, MENU_ITEM_COUNT(menu_cross_items), MENU_PAGE_YUANSHU}' in menu_source
    assert '"enc_target", &app.cross.encoder_target' in menu_source
    assert '"adc_a_1", &app.cross.adc_a_1' in menu_source
    assert '"adc_b_1", &app.cross.adc_b_1' in menu_source
    assert '"adc_c_l", &app.cross.adc_c_l' in menu_source


def test_cross_eeprom_layout_bumps_version_and_uses_slot_56():
    eeprom_header = _read(EEPROM_H)
    eeprom_source = _read(EEPROM_C)
    track_header = _read(A_RUN_TRACK_ELEMENT_H)
    track_source = _read(A_RUN_TRACK_ELEMENT_C)
    cross_source = _read(ROOT / "project" / "user" / "a_run_cross.c")

    assert "#define EEPROM_CONFIG_VERSION 3L" in eeprom_source
    assert "float encoder_target;" in eeprom_header
    assert "AppCrossConfig cross;" in eeprom_header
    assert "6双十字" in eeprom_header
    assert "config->cross.encoder_target = 20.0f;" in eeprom_source
    assert "config->cross.encoder_target = read_float(56);" in eeprom_source
    assert "if (config->cross.encoder_target <= 0.0f)" not in eeprom_source
    assert "save_float(config->cross.encoder_target, 56);" in eeprom_source

    assert "#define TRACK_ELEMENT_CROSS 6" in track_header
    assert "6-双十字" in track_header
    assert "element == ELEMENT_CROSS" in track_source
    assert "case ELEMENT_CROSS:" in track_source
    assert "a_run_cross_update_5ms()" in track_source
    assert "ad_sum > CROSS_AD_SUM_THRESHOLD" in cross_source
    assert "cross_encoder_sum >= app.cross.encoder_target" in cross_source
    assert "encoder_target = app.cross.encoder_target;" not in cross_source


def test_fly_mode_cooldown_starts_from_forward_recover_speed():
    fly_source = _read(A_RUN_FLY_C)
    fly_low_body = fly_source[
        fly_source.index("case FLY_STATE_LOW:"):fly_source.index("case FLY_STATE_COOLDOWN:")
    ]

    assert "config->fly.fly_recover_speed = 10;" in _read(EEPROM_C)
    assert "fly_release_speed = 0.0f;" not in fly_low_body
    assert "fly_release_speed = (float)app.fly.fly_recover_speed;" in fly_low_body


def test_fly_mode_removes_airborne_state_and_checks_landing_inside_low():
    fly_source = _read(A_RUN_FLY_C)
    fly_header = _read(A_RUN_FLY_H)
    eeprom_header = _read(EEPROM_H)
    eeprom_source = _read(EEPROM_C)
    menu_source = _read(MENU_C)
    fly_low_body = fly_source[
        fly_source.index("case FLY_STATE_LOW:"):fly_source.index("case FLY_STATE_COOLDOWN:")
    ]

    assert "FLY_STATE_FLY" not in fly_header
    assert "fly_airborne_th" not in eeprom_header
    assert "config->fly.fly_airborne_th" not in eeprom_source
    assert "app.fly.fly_airborne_th" not in fly_source
    assert '"airborne"' not in menu_source
    assert "fly_land_confirm_count++;" in fly_low_body
    assert "if (fly_land_confirm_count >= app.fly.fly_land_confirm_count)" in fly_low_body
    assert "stop = 1;" not in fly_low_body


def test_fly_cooldown_runs_in_background_during_wall_timing():
    fly_source = _read(A_RUN_FLY_C)
    track_source = _read(A_RUN_TRACK_ELEMENT_C)

    assert "void a_run_fly_update_release_speed(float *speed);" in _read(A_RUN_FLY_H)
    assert "void a_run_fly_update_release_speed(float *speed)" in fly_source
    assert "apply_speed" not in fly_source
    assert "expected_element == ELEMENT_WALL && a_run_wall_get_state() == 2" not in track_source
    assert "a_run_fly_update_release_speed(speed);" in track_source
    assert "a_run_fly_update_release_speed(speed, 0);" not in track_source
    assert "a_run_fly_update_release_speed(speed, 1);" not in track_source


def test_fly_mode_entry_requires_decreasing_weak_signal_window():
    fly_source = _read(A_RUN_FLY_C)
    fly_idle_body = fly_source[
        fly_source.index("case FLY_STATE_IDLE:"):fly_source.index("case FLY_STATE_LOW:")
    ]
    helper_body = _function_body(
        fly_source,
        "static uint8 a_run_fly_update_entry_gate(",
        "uint8 a_run_fly_take_finish_event(void)",
    )

    assert "#define FLY_DETECT_SIDE_TH 25u" in fly_source
    assert "#define FLY_DETECT_CENTER_TH 10u" in fly_source
    assert "#define FLY_ENTRY_WINDOW_COUNT 10" in fly_source
    assert "a_run_fly_update_entry_gate(" in fly_idle_body
    assert "ad1 < *last_ad1" in helper_body
    assert "ad4 < *last_ad4" in helper_body
    assert "*window_count >= window_limit" in helper_body
    assert "*last_ad_valid = 0;" in helper_body


def test_fly_and_seesaw_share_the_same_entry_gate_helper():
    fly_source = _read(A_RUN_FLY_C)
    seesaw_body = _function_body(
        fly_source,
        "void a_run_seesaw_update_speed(float *speed, uint8 allow_entry)",
        "void a_run_fly_update_release_speed(float *speed)",
    )
    fly_body = fly_source[fly_source.index("void a_run_fly_update_speed(float *speed, uint8 allow_entry)"):]

    assert "static uint8 a_run_fly_update_entry_gate(" in fly_source
    assert "a_run_fly_update_entry_gate(" in seesaw_body
    assert "a_run_fly_update_entry_gate(" in fly_body
    assert "app.fly.fly_detect_count" in fly_body
    assert "app.fly.seesaw_detect_count" in seesaw_body
    assert "SEESAW_ENTRY_WINDOW_COUNT,\n                                        0," in seesaw_body
    assert "FLY_ENTRY_WINDOW_COUNT,\n                                        1," in fly_body
    assert "if (require_decrease == 0)" in fly_source


def test_fly_mode_landing_requires_multi_frame_recovery_confirm():
    fly_source = _read(A_RUN_FLY_C)
    fly_low_body = fly_source[
        fly_source.index("case FLY_STATE_LOW:"):fly_source.index("case FLY_STATE_COOLDOWN:")
    ]
    eeprom_header = _read(EEPROM_H)
    eeprom_source = _read(EEPROM_C)

    assert "int fly_land_confirm_count;" in eeprom_header
    assert "config->fly.fly_land_confirm_count = 2;" in eeprom_source
    assert "config->fly.fly_land_confirm_count = (int)read_int(54);" in eeprom_source
    assert "config->fly.fly_land_confirm_count < 1" not in eeprom_source
    assert "save_int(config->fly.fly_land_confirm_count, 54);" in eeprom_source
    assert "static uint8 fly_land_confirm_count = 0;" in fly_source
    assert "if (ad1 > FLY_LAND_SIDE_TH &&" in fly_low_body
    assert "ad4 > FLY_LAND_SIDE_TH)" in fly_low_body
    assert "ad2 > FLY_LAND_CENTER_TH" not in fly_low_body
    assert "ad3 > FLY_LAND_CENTER_TH" not in fly_low_body
    assert "fly_land_confirm_count++;" in fly_low_body
    assert "if (fly_land_confirm_count >= app.fly.fly_land_confirm_count)" in fly_low_body
    assert "fly_land_confirm_count = 0;" in fly_low_body


def test_eeprom_fly_config_no_longer_uses_readback_guards():
    eeprom_source = _read(EEPROM_C)

    assert "if (config->fly.seesaw_mode != 0)" not in eeprom_source
    assert "if (config->fly.seesaw_speed < 0 || config->fly.seesaw_speed > 200)" not in eeprom_source
    assert "if (config->fly.seesaw_release_step < 0.0f || config->fly.seesaw_release_step > 10.0f)" not in eeprom_source

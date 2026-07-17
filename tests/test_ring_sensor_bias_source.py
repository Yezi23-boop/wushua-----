import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RING_H = ROOT / "project" / "user" / "a_run_ring.h"
RING_LEGACY_C = ROOT / "project" / "user" / "a_run_ring.c"
ADC_C = ROOT / "project" / "user" / "ADC.c"
A_RUN_C = ROOT / "project" / "user" / "a_run.c"
A_RUN_TRACK_ELEMENT_C = ROOT / "project" / "user" / "a_run_track_element.c"
EEPROM_H = ROOT / "project" / "service" / "eeprom.h"
EEPROM_C = ROOT / "project" / "service" / "eeprom.c"
MENU_C = ROOT / "project" / "service" / "menu.c"
UVPROJ = ROOT / "project" / "mdk" / "seekfree.uvproj"


def _read(path):
    return path.read_text(encoding="utf-8")


def test_ring_implementations_are_selected_at_runtime():
    header = _read(RING_H)
    source = _read(RING_LEGACY_C)
    project = _read(UVPROJ)

    assert "RING_CONTROL_LEGACY = 0" in header
    assert "RING_CONTROL_SENSOR_BIAS = 1" in header
    assert "RING_CONTROL_MODE" not in header
    assert "RING_CONTROL_MODE" not in source
    assert source.count("RingStruct ring_data = {0};") == 1
    assert "static RingControlMode ring_mode_active" in source
    assert "ring_mode_active = (RingControlMode)app.ring.control_mode;" in source
    assert "if (ring_mode_active == RING_CONTROL_LEGACY)" in source
    assert "static uint8 ring_update_legacy_2ms(int8 ring_dir)" in source
    assert "static uint8 ring_update_sensor_bias_2ms(void)" in source
    assert "return ring_update_legacy_2ms(ring_dir);" in source
    assert "return ring_update_sensor_bias_2ms();" in source
    assert "a_run_ring_sensor_bias.c" not in project


def test_sensor_bias_reverses_virtual_signal_side_during_out_ring():
    adc = _read(ADC_C)
    ring = _read(RING_LEGACY_C)
    header = _read(RING_H)
    bias_function = ring[
        ring.index("void a_run_ring_apply_adc_bias(float *left_signal,"):
        ring.index("void a_run_ring_reset(void)")
    ]
    pre_ring = bias_function[
        bias_function.index("if (ring_state == RING_STATE_PRE_RING)"):
        bias_function.index("else if (ring_state == RING_STATE_OUT_RING)")
    ]
    out_ring = bias_function[bias_function.index("else if (ring_state == RING_STATE_OUT_RING)"):]
    pre_left = pre_ring[
        pre_ring.index("if (ring_data.flast_l != 0)"):
        pre_ring.index("else if (ring_data.flast_r != 0)")
    ]
    pre_right = pre_ring[pre_ring.index("else if (ring_data.flast_r != 0)"):]
    out_left = out_ring[
        out_ring.index("if (ring_data.flast_l != 0)"):
        out_ring.index("else if (ring_data.flast_r != 0)")
    ]
    out_right = out_ring[out_ring.index("else if (ring_data.flast_r != 0)"):]

    assert "left_signal = (float)ad11;" in adc
    assert "left_middle_signal = (float)ad22;" in adc
    assert "right_middle_signal = (float)ad33;" in adc
    assert "right_signal = (float)ad44;" in adc
    assert "a_run_ring_apply_adc_bias(&left_signal," in adc
    assert "void a_run_ring_apply_adc_bias(float *left_signal," in header
    assert "*left_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;" in pre_left
    assert "*left_middle_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;" in pre_left
    assert "*right_middle_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;" in pre_right
    assert "*right_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;" in pre_right
    assert "*right_middle_signal *= RING_ACTIVE_PROFILE.bias_exit_gain;" in out_left
    assert "*right_signal *= RING_ACTIVE_PROFILE.bias_exit_gain;" in out_left
    assert "*left_signal *= RING_ACTIVE_PROFILE.bias_exit_gain;" in out_right
    assert "*left_middle_signal *= RING_ACTIVE_PROFILE.bias_exit_gain;" in out_right
    assert "bias_entry_gain" not in out_ring
    assert "bias_exit_gain" not in pre_ring
    assert "RING_STATE_IN_RING" not in bias_function
    assert "ad1 *= app.ring.bias_entry_gain" not in adc
    assert "ad4 *= app.ring.bias_entry_gain" not in adc
    assert "a_value * (left_signal - right_signal)" in adc
    assert "a_value * (left_signal + right_signal)" in adc
    assert "b_value * middle_diff" in adc
    assert "c_value * middle_diff_abs" in adc


def test_ring_uses_independent_adc_weights_through_out_ring():
    adc = _read(ADC_C)
    header = _read(RING_H)
    source = _read(RING_LEGACY_C)

    signature = "void a_run_ring_apply_adc_params(float *a_value, float *b_value, float *c_value)"
    assert signature + ";" in header
    assert signature in source
    assert "a_run_ring_apply_adc_params(&a_value, &b_value, &c_value);" in adc
    assert "ring_state != RING_STATE_IDLE && ring_state != RING_STATE_OUT_RING" in source
    assert "ring_state == RING_STATE_PRE_RING ||" in source
    assert "ring_state == RING_STATE_IN_RING ||" in source
    assert "ring_state == RING_STATE_OUT_RING" in source
    assert "*a_value = app.ring.profiles[0].adc_a_1;" in source
    assert "*a_value = RING_ACTIVE_PROFILE.adc_a_1;" in source


def test_ring_uses_independent_steer_parameters_through_out_ring():
    runner = _read(A_RUN_C)
    header = _read(RING_H)
    source = _read(RING_LEGACY_C)

    signature = "void a_run_ring_apply_steer_params(float *kp, float *kd, float *kp2)"
    assert signature + ";" in header
    assert signature in source
    assert "PID.steer.Kp = app.speed.kp_Err;" in runner
    assert "PID.steer.Kd = app.speed.kd_Err;" in runner
    assert "PID.steer.Kp2 = app.speed.kp2_Err;" in runner
    assert "a_run_ring_apply_steer_params(&PID.steer.Kp," in runner
    assert "*kp = app.ring.profiles[0].kp_Err;" in source
    assert "*kp = RING_ACTIVE_PROFILE.kp_Err;" in source
    steer_function = source[
        source.index("void a_run_ring_apply_steer_params(float *kp, float *kd, float *kp2)"):
        source.index("void a_run_ring_apply_angle_diff_params(float *kp,")
    ]
    assert "ring_state == RING_STATE_OUT_RING" in steer_function


def test_sensor_bias_ring_uses_yaw_for_entry_and_encoder_only_for_finish():
    source = _read(RING_LEGACY_C)

    assert "#define RING_ENTRY_CONFIRM_COUNT 5u" in source
    assert "else if (timeadd(&ring_data.time_l, 300))" in source
    assert "ring_state = RING_STATE_ENTRY;" in source
    assert "case RING_STATE_ENTRY:" in source
    assert "ring_data.encoder >= RING_ACTIVE_PROFILE.entry_straight_encoder" in source
    assert "ring_state == RING_STATE_ENTRY" in source
    assert "*angle_target = 0.0f;" in source
    assert "ring_state = RING_STATE_PRE_RING;" in source
    assert (
        "ring_data.yaw_delta_sum >= RING_ACTIVE_PROFILE.bias_entry_yaw &&\n"
        "            ring_data.encoder >= RING_ACTIVE_PROFILE.bias_entry_encoder"
    ) in source
    assert "ring_data.gyro_flat = 0;" in source
    assert "if (ring_data.encoder >= RING_ACTIVE_PROFILE.bias_finish_encoder)" in source
    assert "bias_finish_yaw" not in source
    assert "timeadd(&ring_data.out_ring_time, 200)" in source
    assert "ring_data.diff_set = app.ring.pre_ring_Gyro_target" in source
    assert "ring_data.encoder += (speed_l + speed_r) * 0.5f * 0.012f;" in source


def test_profile_is_latched_at_entry_and_speed_covers_full_ring():
    source = _read(RING_LEGACY_C)
    track = _read(A_RUN_TRACK_ELEMENT_C)
    sensor_update = source[
        source.rindex("static uint8 ring_update_sensor_bias_2ms(void)"):
        source.index("void a_run_ring_update_integrals(void)")
    ]

    assert "static uint8 ring_profile_active = 0;" in source
    assert "ring_profile_active = (uint8)app.ring.profile_select;" in source
    assert "#define RING_ACTIVE_PROFILE (app.ring.profiles[ring_profile_active])" in source
    assert "void a_run_ring_apply_speed(float *speed)" in source
    assert "ring_state == RING_STATE_PRE_RING" in source
    assert "ring_state == RING_STATE_IN_RING" in source
    assert "ring_state == RING_STATE_OUT_RING" in source
    assert "*speed = RING_ACTIVE_PROFILE.target_speed;" in source
    assert "stop=1" not in sensor_update
    assert sensor_update.count("stop = 1;") == 1
    assert "if (ring_third_stop_active != 0)" in sensor_update
    assert "a_run_ring_apply_speed(speed);" in track
    assert track.index("a_run_fly_update_release_speed(speed);") < track.index(
        "a_run_ring_apply_speed(speed);"
    )


def test_ring_profile_overrides_angle_loop_and_differential_gains():
    header = _read(RING_H)
    runner = _read(A_RUN_C)
    source = _read(RING_LEGACY_C)
    signature = "void a_run_ring_apply_angle_diff_params(float *kp,"
    control = source[source.index(signature):source.index("/**", source.index(signature))]

    assert signature in header
    assert "ring_state != RING_STATE_IDLE && ring_state != RING_STATE_OUT_RING" in control
    assert "*kp = app.ring.profiles[0].kp_Angle;" in control
    assert "ring_state == RING_STATE_PRE_RING ||" in control
    assert "ring_state == RING_STATE_IN_RING ||" in control
    assert "ring_state == RING_STATE_OUT_RING" in control
    assert "*kp = RING_ACTIVE_PROFILE.kp_Angle;" in control

    global_kp = runner.index("PID.angle.Kp = app.angle.kp_Angle;")
    global_kd = runner.index("PID.angle.Kd = app.angle.kd_Angle;")
    apply_ring = runner.index("a_run_ring_apply_angle_diff_params(&PID.angle.Kp,")
    angle_update = runner.index("pid_angle_update(&PID.angle,")
    assert global_kp < apply_ring < angle_update
    assert global_kd < apply_ring


def test_sensor_bias_profiles_are_independent_and_persisted():
    header = _read(EEPROM_H)
    source = _read(EEPROM_C)

    assert "} AppRingProfileConfig;" in header
    assert "int16 profile_select;" in header
    assert "AppRingProfileConfig profiles[2];" in header

    defaults = {
        (0, "bias_entry_gain"): ("4.00f", 65),
        (0, "bias_exit_gain"): ("4.00f", 88),
        (0, "entry_straight_encoder"): ("5.0f", 98),
        (0, "bias_entry_yaw"): ("30.00f", 66),
        (0, "bias_entry_encoder"): ("200.00f", 67),
        (0, "kp_Err"): ("8.00f", 68),
        (0, "bias_finish_encoder"): ("1.00f", 69),
        (0, "adc_a_1"): ("1.00f", 70),
        (0, "adc_b_1"): ("1.20f", 71),
        (0, "adc_c_l"): ("0.60f", 72),
        (0, "kd_Err"): ("12.00f", 73),
        (0, "kp2_Err"): ("0.06f", 74),
        (0, "target_speed"): ("50.00f", 75),
        (0, "kp_Angle"): ("0.92f", 90),
        (0, "kd_Angle"): ("0.78f", 91),
        (0, "diff_inner_gain"): ("0.60f", 92),
        (0, "diff_outer_gain"): ("0.50f", 93),
        (1, "bias_entry_gain"): ("2.00f", 76),
        (1, "bias_exit_gain"): ("2.00f", 89),
        (1, "entry_straight_encoder"): ("5.0f", 99),
        (1, "bias_entry_yaw"): ("30.00f", 77),
        (1, "bias_entry_encoder"): ("1.00f", 78),
        (1, "bias_finish_encoder"): ("200.00f", 79),
        (1, "adc_a_1"): ("1.20f", 80),
        (1, "adc_b_1"): ("1.00f", 81),
        (1, "adc_c_l"): ("0.60f", 82),
        (1, "kp_Err"): ("8.00f", 83),
        (1, "kd_Err"): ("12.00f", 84),
        (1, "kp2_Err"): ("0.01f", 85),
        (1, "target_speed"): ("50.00f", 86),
        (1, "kp_Angle"): ("0.92f", 94),
        (1, "kd_Angle"): ("0.78f", 95),
        (1, "diff_inner_gain"): ("0.60f", 96),
        (1, "diff_outer_gain"): ("0.50f", 97),
    }
    for (profile, field), (value, slot) in defaults.items():
        assert re.search(
            rf"config->ring\.profiles\[{profile}\]\.{field}\s*=\s*{re.escape(value)};",
            source,
        )
        assert f"config->ring.profiles[{profile}].{field} = read_float({slot});" in source
        assert f"save_float(config->ring.profiles[{profile}].{field}, {slot});" in source
    assert "config->ring.profile_select = 0;" in source
    assert "config->ring.profile_select = (int16)read_int(87);" in source
    assert "save_int(config->ring.profile_select, 87);" in source
    assert "bias_finish_yaw" not in header
    assert "bias_finish_yaw" not in source
    assert "int16 control_mode;" in header
    assert "config->ring.control_mode = 1;" in source
    assert "config->ring.control_mode = (int16)read_int(100);" in source
    assert "save_int(config->ring.control_mode, 100);" in source
    assert "uint8 date_buff[404];" in source
    assert "extern uint8 date_buff[404];" in header
    assert "#define EEPROM_CONFIG_VERSION 16L" in source


def test_ring_menu_exposes_runtime_mode_and_both_parameter_sets():
    source = _read(MENU_C)

    assert "RING_CONTROL_MODE" not in source
    assert '"mode", &app.ring.control_mode' in source
    assert '"profile", &app.ring.profile_select' in source
    assert '"straight_E", &app.ring.profiles[0].entry_straight_encoder' in source
    assert '"straight_E", &app.ring.profiles[1].entry_straight_encoder' in source
    assert '"P0", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_RING_P0' in source
    assert '"P1", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_RING_P1' in source
    assert '"LEGACY", 0, MENU_META(MENU_ITEM_LINK, 0, 0), MENU_PAGE_RING_LEGACY' in source
    assert "static const MenuItemDef menu_ring_p0_items[]" in source
    assert "static const MenuItemDef menu_ring_p1_items[]" in source
    assert "static const MenuItemDef menu_ring_p1_drive_items[]" in source
    assert "static const MenuItemDef menu_ring_legacy_items[]" in source
    assert "static const MenuItemDef menu_ring_legacy_entry_items[]" in source
    assert "static const MenuItemDef menu_ring_legacy_in_items[]" in source
    assert "static const MenuItemDef menu_ring_legacy_out_items[]" in source
    assert "static const MenuItemDef menu_ring_legacy_adc_items[]" in source
    assert "static const MenuItemDef menu_ring_legacy_drive_items[]" in source
    assert '"gain", &app.ring.profiles[0].bias_entry_gain' in source
    assert '"gain", &app.ring.profiles[1].bias_entry_gain' in source
    assert source.count('"exit_gain", &app.ring.profiles[') == 2
    assert '"finish_Gz"' not in source
    assert source.count('"ring_spd", &app.ring.profiles[') == 2
    assert source.count('"adc_a_1", &app.ring.profiles[') == 3
    assert source.count('"kp_Ang", &app.ring.profiles[0].kp_Angle') == 2
    assert source.count('"kp_Ang", &app.ring.profiles[1].kp_Angle') == 1
    assert source.count('"inner_g", &app.ring.profiles[0].diff_inner_gain') == 2
    assert source.count('"inner_g", &app.ring.profiles[1].diff_inner_gain') == 1
    assert '"pre_r_T", &app.ring.pre_ring_Gyro_target' in source
    assert '"pre_o_T", &app.ring.pre_out_ring_Gyro_target' in source


def test_third_same_direction_ring_stops_only_after_entering_in_ring():
    source = _read(RING_LEGACY_C)
    header = _read(RING_H)
    track = _read(A_RUN_TRACK_ELEMENT_C)
    legacy_update = source[
        source.rindex("static uint8 ring_update_legacy_2ms(int8 ring_dir)"):
        source.rindex("static uint8 ring_update_sensor_bias_2ms(void)")
    ]
    sensor_update = source[
        source.rindex("static uint8 ring_update_sensor_bias_2ms(void)"):
        source.index("void a_run_ring_update_integrals(void)")
    ]
    legacy_in_ring = legacy_update[
        legacy_update.index("case RING_STATE_IN_RING:"):
        legacy_update.index("case RING_STATE_PRE_OUT_RING:")
    ]
    sensor_in_ring = sensor_update[
        sensor_update.index("case RING_STATE_IN_RING:"):
        sensor_update.index("case RING_STATE_OUT_RING:")
    ]
    normal_reset = source[
        source.index("void a_run_ring_reset(void)"):
        source.index("void a_run_ring_pass_count_reset(void)")
    ]
    pass_reset = source[
        source.index("void a_run_ring_pass_count_reset(void)"):
        source.index("uint8 a_run_ring_update_2ms(int8 ring_dir)")
    ]

    assert "#define RING_SPECIAL_STOP_PASS_COUNT 3u" in source
    assert "ring_left_pass_count++;" in source
    assert "ring_right_pass_count++;" in source
    assert source.index("if (ring_entry_count >= RING_ENTRY_CONFIRM_COUNT)") < source.index(
        "ring_left_pass_count++;"
    )
    assert "ring_left_pass_count == RING_SPECIAL_STOP_PASS_COUNT" in source
    assert "ring_right_pass_count == RING_SPECIAL_STOP_PASS_COUNT" in source
    assert ">= RING_SPECIAL_STOP_PASS_COUNT" not in source
    assert legacy_update.count("stop = 1;") == 1
    assert "if (ring_third_stop_active != 0)" in legacy_update
    assert source.count("stop = 1;") == 2
    assert "if (ring_third_stop_active != 0)" in legacy_in_ring
    assert "if (ring_third_stop_active != 0)" in sensor_in_ring
    assert "break;" in legacy_in_ring
    assert "break;" in sensor_in_ring
    assert source.count("ring_data.distance = 0;\n                ring_data.gyro_flat = 0;\n                stop = 1;") == 2
    assert "ring_left_pass_count = 0;" not in normal_reset
    assert "ring_right_pass_count = 0;" not in normal_reset
    assert "ring_third_stop_active = 0;" in normal_reset
    assert "ring_left_pass_count = 0;" in pass_reset
    assert "ring_right_pass_count = 0;" in pass_reset
    assert "stop =" not in pass_reset
    assert "void a_run_ring_pass_count_reset(void);" in header
    assert "a_run_ring_pass_count_reset();" in track
    assert track.count("a_run_ring_pass_count_reset();") == 1

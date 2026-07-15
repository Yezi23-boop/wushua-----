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


def _ring_mode_sources():
    source = _read(RING_LEGACY_C)
    split = source.index("#elif RING_CONTROL_MODE == RING_MODE_SENSOR_BIAS")
    return source[:split], source[split:]


def test_ring_implementations_are_selected_by_one_compile_time_macro():
    header = _read(RING_H)
    source = _read(RING_LEGACY_C)
    legacy, bias = _ring_mode_sources()
    project = _read(UVPROJ)

    assert "#define RING_MODE_LEGACY 0" in header
    assert "#define RING_MODE_SENSOR_BIAS 1" in header
    assert "#define RING_CONTROL_MODE RING_MODE_SENSOR_BIAS" in header
    assert "#if RING_CONTROL_MODE == RING_MODE_LEGACY" in legacy
    assert "#elif RING_CONTROL_MODE == RING_MODE_SENSOR_BIAS" in bias
    assert legacy.count("RingStruct ring_data = {0};") == 1
    assert bias.count("RingStruct ring_data = {0};") == 1
    assert source.count("RingStruct ring_data = {0};") == 2
    assert "a_run_ring_sensor_bias.c" not in project


def test_sensor_bias_uses_outer_virtual_signal_only_during_pre_ring():
    adc = _read(ADC_C)
    _, ring = _ring_mode_sources()
    header = _read(RING_H)
    left_ring_branch = ring[
        ring.index("if (ring_data.flast_l != 0)"):
        ring.index("else if (ring_data.flast_r != 0)")
    ]
    right_ring_branch = ring[ring.index("else if (ring_data.flast_r != 0)"):]

    assert "left_signal = (float)ad11;" in adc
    assert "left_middle_signal = (float)ad22;" in adc
    assert "right_middle_signal = (float)ad33;" in adc
    assert "right_signal = (float)ad44;" in adc
    assert "a_run_ring_apply_adc_bias(&left_signal," in adc
    assert "void a_run_ring_apply_adc_bias(float *left_signal," in header
    assert "ring_state != RING_STATE_PRE_RING" in ring
    assert "*left_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;" in left_ring_branch
    assert "*left_middle_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;" in left_ring_branch
    assert "*right_signal" not in left_ring_branch
    assert "*right_middle_signal" not in left_ring_branch
    assert "*right_middle_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;" in right_ring_branch
    assert "*right_signal *= RING_ACTIVE_PROFILE.bias_entry_gain;" in right_ring_branch
    assert "ad1 *= app.ring.bias_entry_gain" not in adc
    assert "ad4 *= app.ring.bias_entry_gain" not in adc
    assert "a_value * (left_signal - right_signal)" in adc
    assert "a_value * (left_signal + right_signal)" in adc
    assert "b_value * middle_diff" in adc
    assert "c_value * middle_diff_abs" in adc


def test_ring_uses_independent_adc_weights_until_out_ring():
    adc = _read(ADC_C)
    header = _read(RING_H)
    legacy, bias = _ring_mode_sources()

    signature = "void a_run_ring_apply_adc_params(float *a_value, float *b_value, float *c_value)"
    assert signature + ";" in header
    assert signature in legacy
    assert signature in bias
    assert "a_run_ring_apply_adc_params(&a_value, &b_value, &c_value);" in adc
    assert "ring_state != RING_STATE_IDLE && ring_state != RING_STATE_OUT_RING" in legacy
    assert "ring_state == RING_STATE_PRE_RING || ring_state == RING_STATE_IN_RING" in bias
    assert "*a_value = app.ring.profiles[0].adc_a_1;" in legacy
    assert "*b_value = app.ring.profiles[0].adc_b_1;" in legacy
    assert "*c_value = app.ring.profiles[0].adc_c_l;" in legacy
    assert "*a_value = RING_ACTIVE_PROFILE.adc_a_1;" in bias
    assert "*b_value = RING_ACTIVE_PROFILE.adc_b_1;" in bias
    assert "*c_value = RING_ACTIVE_PROFILE.adc_c_l;" in bias


def test_ring_uses_independent_steer_parameters_until_out_ring():
    runner = _read(A_RUN_C)
    header = _read(RING_H)
    legacy, bias = _ring_mode_sources()

    signature = "void a_run_ring_apply_steer_params(float *kp, float *kd, float *kp2)"
    assert signature + ";" in header
    assert signature in legacy
    assert signature in bias
    assert "PID.steer.Kp = app.speed.kp_Err;" in runner
    assert "PID.steer.Kd = app.speed.kd_Err;" in runner
    assert "PID.steer.Kp2 = app.speed.kp2_Err;" in runner
    assert "a_run_ring_apply_steer_params(&PID.steer.Kp," in runner
    assert "*kp = app.ring.profiles[0].kp_Err;" in legacy
    assert "*kd = app.ring.profiles[0].kd_Err;" in legacy
    assert "*kp2 = app.ring.profiles[0].kp2_Err;" in legacy
    assert "*kp = RING_ACTIVE_PROFILE.kp_Err;" in bias
    assert "*kd = RING_ACTIVE_PROFILE.kd_Err;" in bias
    assert "*kp2 = RING_ACTIVE_PROFILE.kp2_Err;" in bias


def test_sensor_bias_ring_uses_yaw_for_entry_and_encoder_only_for_finish():
    _, source = _ring_mode_sources()

    assert "#define RING_ENTRY_CONFIRM_COUNT 5u" in source
    assert "else if (timeadd(&ring_data.time_l, 300))" in source
    assert "ring_state = RING_STATE_PRE_RING;" in source
    assert (
        "ring_data.yaw_delta_sum >= RING_ACTIVE_PROFILE.bias_entry_yaw &&\n"
        "            ring_data.encoder >= RING_ACTIVE_PROFILE.bias_entry_encoder"
    ) in source
    assert "ring_data.gyro_flat = 0;" in source
    assert "if (ring_data.encoder >= RING_ACTIVE_PROFILE.bias_finish_encoder)" in source
    assert "bias_finish_yaw" not in source
    assert "timeadd(&ring_data.out_ring_time, 200)" in source
    assert "ring_data.diff_set = app.ring" not in source
    assert "ring_data.encoder += (speed_l + speed_r) * 0.5f * 0.012f;" in source


def test_profile_is_latched_at_entry_and_speed_covers_full_ring():
    _, source = _ring_mode_sources()
    track = _read(A_RUN_TRACK_ELEMENT_C)

    assert "static uint8 ring_profile_active = 0;" in source
    assert "ring_profile_active = (uint8)app.ring.profile_select;" in source
    assert "#define RING_ACTIVE_PROFILE (app.ring.profiles[ring_profile_active])" in source
    assert "void a_run_ring_apply_speed(float *speed)" in source
    assert "ring_state == RING_STATE_PRE_RING" in source
    assert "ring_state == RING_STATE_IN_RING" in source
    assert "ring_state == RING_STATE_OUT_RING" in source
    assert "*speed = RING_ACTIVE_PROFILE.target_speed;" in source
    assert "stop=1" not in source
    assert "stop = 1" not in source
    assert "a_run_ring_apply_speed(speed);" in track
    assert track.index("a_run_fly_update_release_speed(speed);") < track.index(
        "a_run_ring_apply_speed(speed);"
    )


def test_sensor_bias_profiles_are_independent_and_persisted():
    header = _read(EEPROM_H)
    source = _read(EEPROM_C)

    assert "} AppRingProfileConfig;" in header
    assert "int16 profile_select;" in header
    assert "AppRingProfileConfig profiles[2];" in header

    defaults = {
        (0, "bias_entry_gain"): ("4.00f", 65),
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
        (1, "bias_entry_gain"): ("2.00f", 76),
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

    assert "uint8 date_buff[352];" in source
    assert "extern uint8 date_buff[352];" in header
    assert "#define EEPROM_CONFIG_VERSION 12L" in source


def test_ring_menu_only_exposes_parameters_for_selected_mode():
    source = _read(MENU_C)

    assert "#if RING_CONTROL_MODE == RING_MODE_SENSOR_BIAS" in source
    assert '"profile", &app.ring.profile_select' in source
    assert '"P0_ENTRY"' in source
    assert '"P0_CTRL"' in source
    assert '"P1_ENTRY"' in source
    assert '"P1_CTRL"' in source
    assert '"gain", &app.ring.profiles[0].bias_entry_gain' in source
    assert '"gain", &app.ring.profiles[1].bias_entry_gain' in source
    assert '"finish_Gz"' not in source
    assert source.count('"ring_spd", &app.ring.profiles[') == 2
    assert source.count('"adc_a_1", &app.ring.profiles[') == 3
    assert "#else" in source
    assert '"pre_r_T", &app.ring.pre_ring_Gyro_target' in source
    assert '"pre_o_T", &app.ring.pre_out_ring_Gyro_target' in source


def test_legacy_ring_no_longer_sets_unreleased_stop():
    source, _ = _ring_mode_sources()

    assert "stop = 1;" not in source
    assert "ring_data.diff_set = app.ring.pre_ring_Gyro_target * ring_dir;" in source
    assert "ring_data.diff_set = app.ring.pre_out_ring_Gyro_target * ring_dir;" in source

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RING_H = ROOT / "project" / "user" / "a_run_ring.h"
RING_LEGACY_C = ROOT / "project" / "user" / "a_run_ring.c"
ADC_C = ROOT / "project" / "user" / "ADC.c"
A_RUN_C = ROOT / "project" / "user" / "a_run.c"
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
    assert "*left_signal *= app.ring.bias_entry_gain;" in left_ring_branch
    assert "*left_middle_signal *= app.ring.bias_entry_gain;" in left_ring_branch
    assert "*right_signal" not in left_ring_branch
    assert "*right_middle_signal" not in left_ring_branch
    assert "*right_middle_signal *= app.ring.bias_entry_gain;" in right_ring_branch
    assert "*right_signal *= app.ring.bias_entry_gain;" in right_ring_branch
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
    for source in (legacy, bias):
        assert "*a_value = app.ring.adc_a_1;" in source
        assert "*b_value = app.ring.adc_b_1;" in source
        assert "*c_value = app.ring.adc_c_l;" in source


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
    for source in (legacy, bias):
        assert "*kp = app.ring.kp_Err;" in source
        assert "*kd = app.ring.kd_Err;" in source
        assert "*kp2 = app.ring.kp2_Err;" in source


def test_sensor_bias_ring_uses_yaw_for_entry_and_encoder_only_for_finish():
    _, source = _ring_mode_sources()

    assert "#define RING_ENTRY_CONFIRM_COUNT 5u" in source
    assert "else if (timeadd(&ring_data.time_l, 300))" in source
    assert "ring_state = RING_STATE_PRE_RING;" in source
    assert (
        "ring_data.yaw_delta_sum >= app.ring.bias_entry_yaw &&\n"
        "            ring_data.encoder >= app.ring.bias_entry_encoder"
    ) in source
    assert "ring_data.gyro_flat = 0;" in source
    assert "if (ring_data.encoder >= app.ring.bias_finish_encoder)" in source
    assert "bias_finish_yaw" not in source
    assert "timeadd(&ring_data.out_ring_time, 200)" in source
    assert "ring_data.diff_set = app.ring" not in source
    assert "ring_data.encoder += (speed_l + speed_r) * 0.5f * 0.012f;" in source


def test_sensor_bias_parameters_are_independent_and_persisted():
    header = _read(EEPROM_H)
    source = _read(EEPROM_C)

    for field in (
        "bias_entry_gain",
        "bias_entry_yaw",
        "bias_entry_encoder",
        "bias_finish_encoder",
    ):
        assert f"float {field};" in header

    defaults = {
        "bias_entry_gain": ("4.00f", 65),
        "bias_entry_yaw": ("30.00f", 66),
        "bias_entry_encoder": ("200.00f", 67),
        "bias_finish_encoder": ("1.00f", 69),
        "adc_a_1": ("1.00f", 70),
        "adc_b_1": ("1.20f", 71),
        "adc_c_l": ("0.60f", 72),
        "kp_Err": ("8.00f", 68),
        "kd_Err": ("12.00f", 73),
        "kp2_Err": ("0.06f", 74),
    }
    for field, (value, slot) in defaults.items():
        assert re.search(
            rf"config->ring\.{field}\s*=\s*{re.escape(value)};",
            source,
        )
        assert f"config->ring.{field} = read_float({slot});" in source
        assert f"save_float(config->ring.{field}, {slot});" in source
    assert "bias_finish_yaw" not in header
    assert "bias_finish_yaw" not in source

    assert "uint8 date_buff[300];" in source
    assert "extern uint8 date_buff[300];" in header
    assert "#define EEPROM_CONFIG_VERSION 11L" in source


def test_ring_menu_only_exposes_parameters_for_selected_mode():
    source = _read(MENU_C)

    assert "#if RING_CONTROL_MODE == RING_MODE_SENSOR_BIAS" in source
    assert '"BIAS_ENTRY"' in source
    assert '"FINISH"' in source
    assert '"gain", &app.ring.bias_entry_gain' in source
    assert '"entry_Gz", &app.ring.bias_entry_yaw' in source
    assert '"entry_E", &app.ring.bias_entry_encoder' in source
    assert '"finish_Gz"' not in source
    assert '"finish_E", &app.ring.bias_finish_encoder' in source
    assert '"ADC"' in source
    assert '"adc_a_1", &app.ring.adc_a_1' in source
    assert '"adc_b_1", &app.ring.adc_b_1' in source
    assert '"adc_c_l", &app.ring.adc_c_l' in source
    assert '"kp_Err", &app.ring.kp_Err' in source
    assert '"kd_Err", &app.ring.kd_Err' in source
    assert '"kp2_Err", &app.ring.kp2_Err' in source
    assert "#else" in source
    assert '"pre_r_T", &app.ring.pre_ring_Gyro_target' in source
    assert '"pre_o_T", &app.ring.pre_out_ring_Gyro_target' in source


def test_legacy_ring_no_longer_sets_unreleased_stop():
    source, _ = _ring_mode_sources()

    assert "stop = 1;" not in source
    assert "ring_data.diff_set = app.ring.pre_ring_Gyro_target * ring_dir;" in source
    assert "ring_data.diff_set = app.ring.pre_out_ring_Gyro_target * ring_dir;" in source

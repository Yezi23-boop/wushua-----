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
    out_ring = bias_function[bias_function.index(
        "else if (ring_state == RING_STATE_OUT_RING)"):]
    pre_left = pre_ring[
        pre_ring.index("if (ring_data.ring_dir > 0)"):
        pre_ring.index("else if (ring_data.ring_dir < 0)")
    ]
    pre_right = pre_ring[pre_ring.index("else if (ring_data.ring_dir < 0)"):]
    out_left = out_ring[
        out_ring.index("if (ring_data.ring_dir > 0)"):
        out_ring.index("else if (ring_data.ring_dir < 0)")
    ]
    out_right = out_ring[out_ring.index("else if (ring_data.ring_dir < 0)"):]

    assert "left_signal = (float)ad11;" in adc
    assert "left_middle_signal = (float)ad22;" in adc
    assert "right_middle_signal = (float)ad33;" in adc
    assert "right_signal = (float)ad44;" in adc
    assert "a_run_ring_apply_adc_bias(&left_signal," in adc
    assert "void a_run_ring_apply_adc_bias(float *left_signal," in header
    assert "*left_signal *= ring_entry_gain_active;" in pre_left
    assert "*left_middle_signal *= ring_entry_gain_active;" in pre_left
    assert "*right_middle_signal *= ring_entry_gain_active;" in pre_right
    assert "*right_signal *= ring_entry_gain_active;" in pre_right
    assert "*right_middle_signal *= ring_profile_active->bias_exit_gain;" in out_left
    assert "*right_signal *= ring_profile_active->bias_exit_gain;" in out_left
    assert "*left_signal *= ring_profile_active->bias_exit_gain;" in out_right
    assert "*left_middle_signal *= ring_profile_active->bias_exit_gain;" in out_right
    assert "bias_entry_gain" not in out_ring
    assert "bias_exit_gain" not in pre_ring
    assert "RING_STATE_IN_RING" not in bias_function
    assert "ad1 *= app.ring.bias_entry_gain" not in adc
    assert "ad4 *= app.ring.bias_entry_gain" not in adc
    assert "a_value * (left_signal - right_signal)" in adc
    assert "a_value * (left_signal + right_signal)" in adc
    assert "b_value * middle_diff" in adc
    assert "c_value * middle_diff_abs" in adc


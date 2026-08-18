import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FILTER_H_PATH = ROOT / "project" / "service" / "filter.h"
FILTER_C_PATH = ROOT / "project" / "service" / "filter.c"
PID_C_PATH = ROOT / "project" / "service" / "pid.c"


def _macro_int(source, name):
    match = re.search(rf"#define\s+{name}\s+(\d+)", source)
    assert match is not None
    return int(match.group(1))


FILTER_H_SOURCE = FILTER_H_PATH.read_text(encoding="utf-8")
ENC_ZERO_DEADBAND = _macro_int(FILTER_H_SOURCE, "ENC_ZERO_DEADBAND")
ENC_SIGN_FIX_MIN = _macro_int(FILTER_H_SOURCE, "ENC_SIGN_FIX_MIN")
ENC_SIGN_MAG_TOL = _macro_int(FILTER_H_SOURCE, "ENC_SIGN_MAG_TOL")


def _sign(value):
    if value > ENC_ZERO_DEADBAND:
        return 1
    if value < -ENC_ZERO_DEADBAND:
        return -1
    return 0


def _correct_sequence(sequence):
    history = []
    output = []

    for raw_now in sequence:
        corrected = raw_now
        if len(history) >= 3:
            h0 = history[0]
            h1 = history[1]
            h2 = history[2]
            hist_sign0 = _sign(h0)
            hist_sign1 = _sign(h1)
            hist_sign2 = _sign(h2)
            current_sign = _sign(raw_now)

            if (
                hist_sign0 != 0
                and hist_sign0 == hist_sign1
                and hist_sign1 == hist_sign2
                and current_sign != 0
                and current_sign == -hist_sign0
                and abs(raw_now) >= ENC_SIGN_FIX_MIN
            ):
                hist_abs_avg = (abs(h0) + abs(h1) + abs(h2)) // 3
                if abs(abs(raw_now) - hist_abs_avg) <= ENC_SIGN_MAG_TOL:
                    corrected = -raw_now

        output.append(corrected)
        history.append(raw_now)
        if len(history) > 3:
            history.pop(0)

    return output


def test_first_three_samples_passthrough():
    assert _correct_sequence([50, 52, 51]) == [50, 52, 51]


def test_single_sign_flip_is_corrected():
    assert _correct_sequence([50, 52, 51, -50]) == [50, 52, 51, 50]


def test_two_consecutive_sign_flips_do_not_stay_forced():
    assert _correct_sequence([50, 52, 51, -50, -49]) == [50, 52, 51, 50, -49]


def test_same_sign_noise_is_not_modified():
    assert _correct_sequence([50, 46, 55, 42]) == [50, 46, 55, 42]


def test_near_zero_values_are_not_corrected():
    near_zero = ENC_ZERO_DEADBAND
    assert _correct_sequence([near_zero, -near_zero, near_zero - 1, -near_zero + 1]) == [
        near_zero,
        -near_zero,
        near_zero - 1,
        -near_zero + 1,
    ]


def test_large_magnitude_change_is_not_mistaken_for_sign_error():
    assert _correct_sequence([50, 52, 51, -90]) == [50, 52, 51, -90]

from pathlib import Path


FILTER_H_PATH = Path(__file__).resolve().parents[1] / "project" / "service" / "filter.h"
FILTER_C_PATH = Path(__file__).resolve().parents[1] / "project" / "service" / "filter.c"
PID_C_PATH = Path(__file__).resolve().parents[1] / "project" / "service" / "pid.c"

ENC_ZERO_DEADBAND = 2
ENC_SIGN_FIX_MIN = 8
ENC_SIGN_MAG_TOL = 16


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


def test_source_contains_history_sign_fix_entrypoint():
    filter_header = FILTER_H_PATH.read_text(encoding="utf-8")
    filter_source = FILTER_C_PATH.read_text(encoding="utf-8")
    pid_source = PID_C_PATH.read_text(encoding="utf-8")

    assert "typedef struct" in filter_header
    assert "EncoderSignFixState" in filter_header
    assert "CorrectEncoderSignByHistory" in filter_header
    assert "int32 CorrectEncoderSignByHistory" in filter_source
    assert "CorrectEncoderSignByHistory(raw_left_count, &encoder_sign_fix_left)" in pid_source
    assert "CorrectEncoderSignByHistory(raw_right_count, &encoder_sign_fix_right)" in pid_source
    assert "low_pass_filter_mt(&encoder_l" not in pid_source
    assert "low_pass_filter_mt(&encoder_r" not in pid_source


def test_first_three_samples_passthrough():
    assert _correct_sequence([50, 52, 51]) == [50, 52, 51]


def test_single_sign_flip_is_corrected():
    assert _correct_sequence([50, 52, 51, -50]) == [50, 52, 51, 50]


def test_two_consecutive_sign_flips_do_not_stay_forced():
    assert _correct_sequence([50, 52, 51, -50, -49]) == [50, 52, 51, 50, -49]


def test_same_sign_noise_is_not_modified():
    assert _correct_sequence([50, 46, 55, 42]) == [50, 46, 55, 42]


def test_near_zero_values_are_not_corrected():
    assert _correct_sequence([1, -1, 2, -2]) == [1, -1, 2, -2]


def test_large_magnitude_change_is_not_mistaken_for_sign_error():
    assert _correct_sequence([50, 52, 51, -90]) == [50, 52, 51, -90]

from pathlib import Path


FILTER_H_PATH = Path(__file__).resolve().parents[1] / "project" / "service" / "filter.h"
FILTER_C_PATH = Path(__file__).resolve().parents[1] / "project" / "service" / "filter.c"
PID_C_PATH = Path(__file__).resolve().parents[1] / "project" / "service" / "pid.c"

REMOVED_ENCODER_FILTER_SYMBOLS = (
    "EncoderSignFixState",
    "EncoderMedian3EmaFilterState",
    "CorrectEncoderSignByHistory",
    "FilterEncoderCountMedian3EmaHalf",
    "ENC_ZERO_DEADBAND",
    "ENC_SIGN_FIX_MIN",
    "ENC_SIGN_MAG_TOL",
    "encoder_median3",
    "encoder_sign_fix",
)


def test_filter_public_surface_keeps_only_low_pass_and_trimmed_mean():
    filter_header = FILTER_H_PATH.read_text(encoding="utf-8")

    assert "LowPassFilter_t" in filter_header
    assert "low_pass_filter_mt" in filter_header
    assert "TrimmedMeanFilterFloatState" in filter_header
    assert "TrimmedMeanFilterFloatReset" in filter_header
    assert "TrimmedMeanFilterFloatUpdate" in filter_header

    for symbol in REMOVED_ENCODER_FILTER_SYMBOLS:
        assert symbol not in filter_header


def test_filter_source_keeps_only_low_pass_and_trimmed_mean_implementations():
    filter_source = FILTER_C_PATH.read_text(encoding="utf-8")

    assert "void low_pass_filter_mt" in filter_source
    assert "void TrimmedMeanFilterFloatReset" in filter_source
    assert "float TrimmedMeanFilterFloatUpdate" in filter_source
    assert "filter_sort_float_asc" in filter_source

    for symbol in REMOVED_ENCODER_FILTER_SYMBOLS:
        assert symbol not in filter_source


def test_pid_uses_low_pass_without_removed_encoder_filter_entrypoints():
    pid_source = PID_C_PATH.read_text(encoding="utf-8")

    assert "low_pass_filter_mt(&encoder_filter_left, &left->speed, 0.5f)" in pid_source
    assert "low_pass_filter_mt(&encoder_filter_right, &right->speed, 0.5f)" in pid_source

    for symbol in REMOVED_ENCODER_FILTER_SYMBOLS:
        assert symbol not in pid_source

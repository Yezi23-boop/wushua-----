from pathlib import Path


PID_C = Path(__file__).resolve().parents[1] / "project" / "service" / "pid.c"


def test_uses_float_incremental_pid_update():
    source = PID_C.read_text(encoding="utf-8")

    assert "float delta_output;" in source
    assert "pid->error = target - actual;" in source
    assert "pid->Kp * (pid->error - pid->prev_error)" in source
    assert "pid->Ki * pid->error" in source
    assert "pid->Kd * (pid->error - 2.0f * pid->prev_error + pid->prev2_error)" in source
    assert "pid->output += delta_output;" in source

    assert "int32 error_i" not in source
    assert "int32 kp_i" not in source
    assert "delta_i = kp_i * (error_i - prev_error_i);" not in source
    assert "output_i = (int32)(pid->output);" not in source


def test_encoder_feedback_uses_current_low_pass_path():
    source = PID_C.read_text(encoding="utf-8")

    assert "speed_r = -(int32)encoder_get_count(TIM4_ENCOEDER) * 0.07f;" in source
    assert "speed_l = (int32)encoder_get_count(TIM3_ENCOEDER) * 0.07f;" in source
    assert "low_pass_filter_mt(&encoder_filter_left, &speed_l, 0.5f);" in source
    assert "low_pass_filter_mt(&encoder_filter_right, &speed_r, 0.5f);" in source
    assert "encoder_clear_count(TIM3_ENCOEDER);" in source
    assert "encoder_clear_count(TIM4_ENCOEDER);" in source

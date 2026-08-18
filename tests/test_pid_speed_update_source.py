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


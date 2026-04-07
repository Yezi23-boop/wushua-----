from pathlib import Path


def test_uses_integer_multiply_accumulate():
    source = Path(r"C:\Users\ye\Desktop\龙丘电机\project\service\pid.c").read_text(encoding="utf-8")

    assert "int32 error_i" in source
    assert "int32 kp_i" in source
    assert "delta_i = kp_i * (error_i - prev_error_i);" in source
    assert "output_i = (int32)(pid->output);" in source
    assert "delta_output = pid->Kp *" not in source
    assert "pid->Ki * pid->error" not in source
    assert "pid->Kd * (pid->error - 2.0f * pid->prev_error + pid->prev2_error)" not in source

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INT_USER_C = ROOT / "project" / "user" / "int_user.c"
A_RUN_C = ROOT / "project" / "user" / "a_run.c"
PID_C = ROOT / "project" / "service" / "pid.c"
A_RUN_RING_C = ROOT / "project" / "user" / "a_run_ring.c"
A_RUN_CYLINDER_C = ROOT / "project" / "user" / "a_run_cylinder.c"
A_RUN_WALL_C = ROOT / "project" / "user" / "a_run_wall.c"
A_RUN_FLY_C = ROOT / "project" / "user" / "a_run_fly.c"
EEPROM_C = ROOT / "project" / "service" / "eeprom.c"
IMU_C = ROOT / "project" / "user" / "imu.c"


def _read(path):
    return path.read_text(encoding="utf-8")


def _function_body(source, start_sig, next_sig):
    start = source.index(start_sig)
    end = source.index(next_sig, start)
    return source[start:end]


def test_timer0_is_2ms_and_timer1_remains_10ms():
    source = _read(INT_USER_C)

    assert "#define TIME_0 2" in source
    assert "#define TIME_1 10" in source
    assert "pit_ms_init(TIM0_PIT, TIME_0);" in source
    assert "pit_ms_init(TIM1_PIT, TIME_1);" in source


def test_motor_start_ramp_removed_and_stall_confirm_kept():
    motor = _read(ROOT / "project" / "service" / "motor.c")

    # 起步阶梯机制已整体取消，发车即全功率；堵转确认窗口保留。
    assert "MOTOR_START_PWM_RAMP" not in motor
    assert "motor_start_ramp" not in motor
    assert "#define MOTOR_STALL_CONFIRM_COUNT 80" in motor

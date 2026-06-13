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


def test_conservative_default_control_parameters_for_2ms_loop():
    int_user = _read(INT_USER_C)
    eeprom = _read(EEPROM_C)

    assert "pid_speed_init(&PID.left_speed, 120.0f, 10.0f, 0.0f, 9000.0f, 9000.0f);" in int_user
    assert "pid_speed_init(&PID.right_speed, 120.0f, 10.0f, 0.0f, 9000.0f, 9000.0f);" in int_user
    assert "config->speed.kd_Err = 10.00f;" in eeprom
    assert "config->speed.limiting_Err = 5000.00f;" in eeprom
    assert "config->ring.pre_ring_steer_output = 2200.00f;" in eeprom
    assert "config->ring.pre_out_ring_steer_output = 1800.00f;" in eeprom
    assert "config->angle.kd_Angle = 0.70f;" in eeprom
    assert "config->fly.count_fly_time_1 = 8;" in eeprom
    assert "config->fly.count_fly_time_2 = 75;" in eeprom


def test_steer_encoder_and_ring_scaling_match_2ms_loop():
    runner = _read(A_RUN_C)
    pid = _read(PID_C)
    ring = _read(A_RUN_RING_C)
    imu = _read(IMU_C)
    run_time_1_body = _function_body(runner, "void run_time_1(void)", "void run_time_2(void)")

    assert "if (steer_div_10 >= 3)" in run_time_1_body
    assert "if (steer_div_10 >= 2)" not in run_time_1_body
    assert "pid_angle_update(" not in run_time_1_body
    assert "left_pwm = PID.left_speed.output - steer_output;" in run_time_1_body
    assert "right_pwm = PID.right_speed.output + steer_output;" in run_time_1_body
    assert "motor_output((int32)left_pwm, (int32)right_pwm);" in run_time_1_body
    assert "speed_r = -(int32)encoder_get_count(TIM4_ENCOEDER) * 0.175f;" in pid
    assert "speed_l = (int32)encoder_get_count(TIM3_ENCOEDER) * 0.175f;" in pid
    assert "low_pass_filter_mt(&encoder_filter_left, &speed_l, 0.25f);" in pid
    assert "low_pass_filter_mt(&encoder_filter_right, &speed_r, 0.25f);" in pid
    assert "#define RING_ENTRY_CONFIRM_COUNT 8u" in ring
    assert "#define RING_YAW_DT_SCALE 0.40f" in ring
    assert "ring_data.yaw_delta_sum += delta_angle * RING_YAW_DT_SCALE;" in ring
    assert "ring_data.encoder += (speed_l + speed_r) * 0.002;" in ring
    assert "#define IMU_GYRO_Z_SCALE (0.005f)" in imu


def test_element_counts_keep_original_wall_clock_time_at_2ms():
    cylinder = _read(A_RUN_CYLINDER_C)
    wall = _read(A_RUN_WALL_C)
    fly = _read(A_RUN_FLY_C)

    assert "#define CYLINDER_TOP_WINDOW_COUNT 250u" in cylinder
    assert "#define CYLINDER_TOP_HIT_COUNT 8" in cylinder
    assert "#define CYLINDER_GROUND_CONFIRM_COUNT 8u" in cylinder
    assert "#define CYLINDER_STABLE_DELAY_COUNT 250u" in cylinder
    assert "#define WALL_TIMING_COUNT 500u" in wall
    assert "#define FLY_RECOVER_LINE_STABLE_COUNT 25u" in fly
    assert "#define FLY_RECOVER_PWM_LIMIT_EARLY_COUNT 150u" in fly
    assert "#define FLY_RECOVER_LOST_LINE_ENABLE_COUNT 500u" in fly


def test_motor_start_ramp_keeps_original_wall_clock_time_at_2ms():
    motor = _read(ROOT / "project" / "service" / "motor.c")

    assert "#define MOTOR_START_PWM_RAMP_STEP 48" in motor
    assert "#define MOTOR_STALL_CONFIRM_COUNT 80" in motor

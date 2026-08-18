# 主控制环 2ms 迁移 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `TIME_0` 主控制环从 5ms 迁移到 2ms，并按真实时间等效调整速度反馈、转向外环、圆环、圆桶、墙面、飞坡和起步斜坡参数。

**Architecture:** 保持现有 `run_time_1()` 串行控制链路不变，只修改周期常量、局部缩放和按周期计数的阈值。`TIME_1` 继续 10ms，因此按键、菜单、软定时器、堵转、电压和丢线保护不迁移。

**Tech Stack:** Keil5 C251 / C89 固件代码，Python `pytest` 源码级回归测试，PowerShell 验证命令。

---

## File Structure

- Modify `project/user/int_user.c`: `TIME_0` 改 2ms，速度环默认 `Ki` 改 10。
- Modify `project/user/a_run.c`: 转向外环分频从 2 拍改 3 拍，注释改为 2ms/6ms 语义。
- Modify `project/service/pid.c`: 编码器速度比例改 0.175，编码器低通 `alpha` 改 0.25。
- Modify `project/user/a_run_ring.c`: 圆环入口确认、里程积分、yaw 累计按 2ms 等效。
- Modify `project/user/a_run_cylinder.c`: 圆桶窗口、确认和稳定计数按 2ms 等效。
- Modify `project/user/a_run_wall.c`: 墙面计时按 2ms 等效。
- Modify `project/user/a_run_fly.c`: 飞坡恢复计数按 2ms 等效。
- Modify `project/service/eeprom.c`: 默认 `kd_Err`、`kd_Angle`、飞坡入口/保持计数改为第一版保守值。
- Modify `project/service/eeprom.h`, `project/user/a_run.h`, `project/user/ADC.h`, `project/user/imu.h`, `project/user/isr.c`: 只更新涉及 5ms 主环的注释为 2ms 或周期中性描述。
- Create `tests/test_control_loop_2ms_source.py`: 新增 2ms 迁移的集中源码级测试。
- Modify `tests/test_pid_speed_update_source.py`: 更新编码器比例和低通系数断言。
- Modify `tests/test_track_element_gate_source.py`: 更新转向分频、圆环、圆桶、墙面和飞坡断言。

---

### Task 1: Add 2ms Timing And Defaults Tests

**Files:**
- Create: `tests/test_control_loop_2ms_source.py`

- [ ] **Step 1: Write the failing source tests**

Create `tests/test_control_loop_2ms_source.py` with this exact content:

```python
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
```

- [ ] **Step 2: Run the new tests to verify they fail**

Run:

```powershell
python -m pytest tests\test_control_loop_2ms_source.py -q
```

Expected: FAIL. The first failure should mention `#define TIME_0 2` missing because the current code still uses `TIME_0 5`.

- [ ] **Step 3: Commit only the failing tests**

Run:

```powershell
git add tests\test_control_loop_2ms_source.py
git commit -m "新增2ms主控迁移源码测试"
```

Expected: commit succeeds.

---

### Task 2: Apply Timer And Conservative Defaults

**Files:**
- Modify: `project/user/int_user.c`
- Modify: `project/service/eeprom.c`
- Modify: `project/service/eeprom.h`
- Test: `tests/test_control_loop_2ms_source.py`

- [ ] **Step 1: Change timer and runtime speed Ki defaults**

In `project/user/int_user.c`, change:

```c
#define TIME_0 5  /* 主控控制环周期 */
```

to:

```c
#define TIME_0 2  /* 主控控制环周期 */
```

Keep:

```c
#define TIME_1 10 /* 按键与菜单服务周期 */
```

Change both speed PID init calls from:

```c
pid_speed_init(&PID.left_speed, 120.0f, 25.0f, 0.0f, 9000.0f, 9000.0f);
pid_speed_init(&PID.right_speed, 120.0f, 25.0f, 0.0f, 9000.0f, 9000.0f);
```

to:

```c
pid_speed_init(&PID.left_speed, 120.0f, 10.0f, 0.0f, 9000.0f, 9000.0f);
pid_speed_init(&PID.right_speed, 120.0f, 10.0f, 0.0f, 9000.0f, 9000.0f);
```

- [ ] **Step 2: Change EEPROM default control parameters**

In `project/service/eeprom.c`, change the default values:

```c
config->speed.kd_Err = 8.00f;
config->angle.kd_Angle = 0.40f;
config->fly.count_fly_time_1 = 3;
config->fly.count_fly_time_2 = 30;
```

to:

```c
config->speed.kd_Err = 10.00f;
config->angle.kd_Angle = 0.70f;
config->fly.count_fly_time_1 = 8;
config->fly.count_fly_time_2 = 75;
```

Update nearby comments so `count_fly_time_1` says `8 * 2ms = 16ms` and `count_fly_time_2` says `75 * 2ms = 150ms`.

- [ ] **Step 3: Update EEPROM header comments**

In `project/service/eeprom.h`, change:

```c
int count_fly_time_1;  /**< 飞坡检测确认时间（按 5ms 主环累计的触发次数） */
int count_fly_time_2;  /**< 飞坡状态持续时间（触发后保持该状态的时长，单位：5ms） */
```

to:

```c
int count_fly_time_1;  /**< 飞坡检测确认时间（按主控制环周期累计的触发次数） */
int count_fly_time_2;  /**< 飞坡状态持续时间（触发后保持该状态的时长，单位：主控制环周期） */
```

- [ ] **Step 4: Run focused tests**

Run:

```powershell
python -m pytest tests\test_control_loop_2ms_source.py::test_timer0_is_2ms_and_timer1_remains_10ms tests\test_control_loop_2ms_source.py::test_conservative_default_control_parameters_for_2ms_loop -q
```

Expected: 2 passed.

- [ ] **Step 5: Commit timer and defaults**

Run:

```powershell
git add project\user\int_user.c project\service\eeprom.c project\service\eeprom.h
git commit -m "调整主控周期和默认控制参数"
```

Expected: commit succeeds.

---

### Task 3: Apply 2ms Feedback And Ring Scaling

**Files:**
- Modify: `project/user/a_run.c`
- Modify: `project/service/pid.c`
- Modify: `project/user/a_run_ring.c`
- Modify: `project/user/a_run.h`
- Modify: `project/user/ADC.h`
- Modify: `project/user/imu.h`
- Modify: `project/user/isr.c`
- Modify: `tests/test_pid_speed_update_source.py`
- Modify: `tests/test_track_element_gate_source.py`
- Test: `tests/test_control_loop_2ms_source.py`

- [ ] **Step 1: Change steering outer loop divider**

In `project/user/a_run.c`, change:

```c
static int steer_div_10 = 0; /* 5ms 主环分频：用于每 10ms 更新一次转向环 */
```

to:

```c
static int steer_div_10 = 0; /* 2ms 主环分频：每 3 拍约 6ms 更新一次转向环 */
```

Change:

```c
if (steer_div_10 >= 2)
```

to:

```c
if (steer_div_10 >= 3)
```

Update the function header comments in the same file from `5ms` to `2ms` where they describe `run_time_1()` and element arbitration.

- [ ] **Step 2: Change encoder speed scaling and filtering**

In `project/service/pid.c`, change:

```c
speed_r = -(int32)encoder_get_count(TIM4_ENCOEDER) * 0.07f;
speed_l = (int32)encoder_get_count(TIM3_ENCOEDER) * 0.07f;
low_pass_filter_mt(&encoder_filter_left, &speed_l, 0.5f);
low_pass_filter_mt(&encoder_filter_right, &speed_r, 0.5f);
```

to:

```c
speed_r = -(int32)encoder_get_count(TIM4_ENCOEDER) * 0.175f;
speed_l = (int32)encoder_get_count(TIM3_ENCOEDER) * 0.175f;
low_pass_filter_mt(&encoder_filter_left, &speed_l, 0.25f);
low_pass_filter_mt(&encoder_filter_right, &speed_r, 0.25f);
```

- [ ] **Step 3: Change ring timing and physical accumulation**

In `project/user/a_run_ring.c`, change:

```c
#define RING_ENTRY_CONFIRM_COUNT 3u /* 圆环入口连续确认次数，5ms 调用下约 15ms。 */
```

to:

```c
#define RING_ENTRY_CONFIRM_COUNT 8u /* 圆环入口连续确认次数，2ms 调用下约 16ms。 */
#define RING_YAW_DT_SCALE 0.40f     /* 主环 2ms 后，圆环 yaw 累计保持原 5ms 等效角度。 */
```

Change:

```c
ring_data.yaw_delta_sum += delta_angle;
```

to:

```c
ring_data.yaw_delta_sum += delta_angle * RING_YAW_DT_SCALE;
```

Change:

```c
ring_data.encoder += (speed_l + speed_r) * 0.005;
```

to:

```c
ring_data.encoder += (speed_l + speed_r) * 0.002;
```

- [ ] **Step 4: Update comments that mention 5ms main loop**

Use targeted edits only. Replace comments that describe the main control loop as 5ms with 2ms or with period-neutral wording in:

```text
project/user/a_run.h
project/user/ADC.h
project/user/imu.h
project/user/isr.c
```

Do not change any 10ms comments in key/menu/soft timer code.

- [ ] **Step 5: Update existing source tests for new high-frequency values**

In `tests/test_pid_speed_update_source.py`, change the expected encoder lines to:

```python
assert "speed_r = -(int32)encoder_get_count(TIM4_ENCOEDER) * 0.175f;" in source
assert "speed_l = (int32)encoder_get_count(TIM3_ENCOEDER) * 0.175f;" in source
assert "low_pass_filter_mt(&encoder_filter_left, &speed_l, 0.25f);" in source
assert "low_pass_filter_mt(&encoder_filter_right, &speed_r, 0.25f);" in source
```

In `tests/test_track_element_gate_source.py`, change the steering and ring assertions to:

```python
assert "if (steer_div_10 >= 3)" in run_time_1_body
assert "if (steer_div_10 >= 2)" not in run_time_1_body
```

and:

```python
assert "#define RING_ENTRY_CONFIRM_COUNT 8u" in ring_source
assert "#define RING_YAW_DT_SCALE 0.40f" in ring_source
assert "ring_data.yaw_delta_sum += delta_angle * RING_YAW_DT_SCALE;" in ring_source
assert "ring_data.encoder += (speed_l + speed_r) * 0.002;" in ring_source
```

- [ ] **Step 6: Run focused tests**

Run:

```powershell
python -m pytest tests\test_control_loop_2ms_source.py::test_steer_encoder_and_ring_scaling_match_2ms_loop tests\test_pid_speed_update_source.py tests\test_track_element_gate_source.py::test_track_element_gate_is_wired_directly_in_5ms_control_chain tests\test_track_element_gate_source.py::test_ring_state_is_split_and_directional -q
```

Expected: all selected tests pass.

- [ ] **Step 7: Commit feedback and ring scaling**

Run:

```powershell
git add project\user\a_run.c project\service\pid.c project\user\a_run_ring.c project\user\a_run.h project\user\ADC.h project\user\imu.h project\user\isr.c tests\test_pid_speed_update_source.py tests\test_track_element_gate_source.py
git commit -m "按2ms主环调整反馈和圆环缩放"
```

Expected: commit succeeds.

---

### Task 4: Apply Element Timing And Start Ramp Scaling

**Files:**
- Modify: `project/user/a_run_cylinder.c`
- Modify: `project/user/a_run_cylinder.h`
- Modify: `project/user/a_run_wall.c`
- Modify: `project/user/a_run_wall.h`
- Modify: `project/user/a_run_fly.c`
- Modify: `project/service/motor.c`
- Modify: `tests/test_track_element_gate_source.py`
- Test: `tests/test_control_loop_2ms_source.py`

- [ ] **Step 1: Change cylinder counts**

In `project/user/a_run_cylinder.c`, change:

```c
#define CYLINDER_TOP_WINDOW_COUNT 100u
#define CYLINDER_TOP_HIT_COUNT 3
#define CYLINDER_GROUND_CONFIRM_COUNT 3u
#define CYLINDER_STABLE_DELAY_COUNT 100u
```

to:

```c
#define CYLINDER_TOP_WINDOW_COUNT 250u
#define CYLINDER_TOP_HIT_COUNT 8
#define CYLINDER_GROUND_CONFIRM_COUNT 8u
#define CYLINDER_STABLE_DELAY_COUNT 250u
```

Update the inline comments to say `2ms * 250 = 500ms`, `2ms * 8 = 16ms`, and `2ms * 250 = 500ms`.

- [ ] **Step 2: Change wall timing**

In `project/user/a_run_wall.c`, change:

```c
#define WALL_TIMING_COUNT 200u
```

to:

```c
#define WALL_TIMING_COUNT 500u
```

Update the comment to say `2ms * 500 = 1000ms`.

- [ ] **Step 3: Change fly recovery counts**

In `project/user/a_run_fly.c`, change:

```c
#define FLY_RECOVER_LINE_STABLE_COUNT 10u
#define FLY_RECOVER_PWM_LIMIT_EARLY_COUNT 60u
#define FLY_RECOVER_LOST_LINE_ENABLE_COUNT 200u
```

to:

```c
#define FLY_RECOVER_LINE_STABLE_COUNT 25u
#define FLY_RECOVER_PWM_LIMIT_EARLY_COUNT 150u
#define FLY_RECOVER_LOST_LINE_ENABLE_COUNT 500u
```

Update comments in that file from `5ms` units to `2ms` units for the changed counters. Keep `FLY_RELEASE_SPEED_STEP` unchanged.

- [ ] **Step 4: Change motor start ramp**

In `project/service/motor.c`, change:

```c
#define MOTOR_START_PWM_RAMP_STEP 120
```

to:

```c
#define MOTOR_START_PWM_RAMP_STEP 48
```

Update the comment to say `2ms 非零输出加 48，约 250ms 从 3000 放开到 9000。`

- [ ] **Step 5: Update headers and existing tests**

In `project/user/a_run_cylinder.h` and `project/user/a_run_wall.h`, change `5ms 更新` wording to `主控制环周期更新` or `2ms 更新`.

In `tests/test_track_element_gate_source.py`, update element timing assertions to:

```python
assert "CYLINDER_TOP_WINDOW_COUNT 250u" in cylinder_source
assert "CYLINDER_TOP_HIT_COUNT 8" in cylinder_source
assert "CYLINDER_STABLE_DELAY_COUNT 250u" in cylinder_source
assert "WALL_TIMING_COUNT 500u" in wall_source
assert "#define FLY_RECOVER_LINE_STABLE_COUNT 25u" in fly_source
assert "#define FLY_RECOVER_PWM_LIMIT_EARLY_COUNT 150u" in fly_source
assert "#define FLY_RECOVER_LOST_LINE_ENABLE_COUNT 500u" in fly_source
```

- [ ] **Step 6: Run focused tests**

Run:

```powershell
python -m pytest tests\test_control_loop_2ms_source.py::test_element_counts_keep_original_wall_clock_time_at_2ms tests\test_control_loop_2ms_source.py::test_motor_start_ramp_keeps_original_wall_clock_time_at_2ms tests\test_track_element_gate_source.py::test_cylinder_wall_and_fly_are_separate_simple_state_machines -q
```

Expected: all selected tests pass.

- [ ] **Step 7: Commit element timing and ramp scaling**

Run:

```powershell
git add project\user\a_run_cylinder.c project\user\a_run_cylinder.h project\user\a_run_wall.c project\user\a_run_wall.h project\user\a_run_fly.c project\service\motor.c tests\test_track_element_gate_source.py
git commit -m "按2ms主环调整特殊元素计时"
```

Expected: commit succeeds.

---

### Task 5: Full Verification And Integration Check

**Files:**
- Read: all modified source and tests
- No production edits unless verification exposes a mismatch with this plan

- [ ] **Step 1: Run all Python tests**

Run:

```powershell
python -m pytest -q
```

Expected: all tests pass, including `tests/test_control_loop_2ms_source.py`.

- [ ] **Step 2: Check Keil tool availability**

Run:

```powershell
Test-Path 'D:\keil_5\UV4\UV4.exe'; Test-Path 'D:\keil_5\C251\BIN\C251.EXE'
```

Expected on a machine with Keil installed: both lines print `True`. If either line prints `False`, record that Keil compilation cannot be run in this environment.

- [ ] **Step 3: Run Keil build when available**

Only if `D:\keil_5\UV4\UV4.exe` exists, run:

```powershell
& 'D:\keil_5\UV4\UV4.exe' -b 'project\mdk\seekfree.uvproj'
```

Then inspect:

```powershell
Select-String -Path 'project\mdk\out_file\SEEKFREE.build_log.htm' -Pattern 'Error\(s\)|Warning\(s\)'
```

Expected: build log reports `0 Error(s)`. Warnings should be reviewed and summarized.

- [ ] **Step 4: Inspect final diff**

Run:

```powershell
git status --short
git diff --stat HEAD
```

Expected: after Task 1-4 commits, `git status --short` is empty. If a verification-only fix was needed in Task 5, the status shows only that fix.

- [ ] **Step 5: Final report**

Report these facts in Chinese:

```text
1. TIME_0 已迁移到 2ms，TIME_1 保持 10ms。
2. 转向外环为 6ms。
3. 速度反馈、速度 Ki、圆环、圆桶、墙面、飞坡和起步斜坡已按 2ms 等效迁移。
4. Python 测试结果。
5. Keil 编译是否执行，以及原因或结果。
6. 当前是否有未提交变更。
```

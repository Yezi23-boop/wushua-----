# gyro_z 语义统一 Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改变现有实车标定逻辑的前提下，统一 `gyro_z` 的代码语义、注释和调试用法。

**Architecture:** 保留 `imu.c` 中现有 `gyro_z` 计算公式，把 `gyro_z` 统一视为标定后的控制反馈量。只修改注释、调试显示和重复缩放点，不新增原始变量，不调整控制参数。

**Tech Stack:** STC AI8051U, Keil C251, C89, PowerShell verification script

---

## Chunk 1: Verification Gate

### Task 1: Add a regression check for gyro_z semantics

**Files:**
- Create: `scripts/check_gyro_z_semantics.ps1`

- [ ] **Step 1: Write the failing check script**

Create a PowerShell script that verifies:
- `project/user/imu.h` does not describe `gyro_z` as `deg/s`
- `project/service/test.c` does not multiply `gyro_z` by `0.082f`
- `project/service/debug_view.c` does not multiply `gyro_z` by `0.082f`
- `project/user/imu.c` keeps the calibrated `gyro_z` assignment

- [ ] **Step 2: Run the script to verify it fails**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check_gyro_z_semantics.ps1
```

Expected:
- script exits with code `1`
- current files fail the old-semantics checks

## Chunk 2: Source Cleanup

### Task 2: Align source comments and usages

**Files:**
- Modify: `project/user/imu.h`
- Modify: `project/user/imu.c`
- Modify: `project/user/a_run.c`
- Modify: `project/service/pid.c`
- Modify: `project/service/test.c`
- Modify: `project/service/debug_view.c`

- [ ] **Step 1: Update gyro_z comments**

Change comments so `gyro_z` is described as a calibrated control-feedback quantity instead of standard physical angular velocity.

- [ ] **Step 2: Remove duplicate scaling**

Update `test.c` and `debug_view.c` so they use `gyro_z` directly.

- [ ] **Step 3: Keep the calibrated source assignment**

Do not change the calibrated `gyro_z` assignment in `imu.c`.

## Chunk 3: Verification

### Task 3: Re-run regression checks

**Files:**
- Test: `scripts/check_gyro_z_semantics.ps1`

- [ ] **Step 1: Run the check script again**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check_gyro_z_semantics.ps1
```

Expected:
- exit code `0`
- all checks report `PASS`

- [ ] **Step 2: Search for stale duplicate scaling**

Run:

```powershell
rg -n "gyro_z\\s*\\*\\s*0\\.082f|度/s" project
```

Expected:
- no stale `gyro_z * 0.082f` use sites remain
- no `gyro_z` comment still labels it as `deg/s`

# Track Element Wall Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a wall element between cylinder and the next left ring so wall/down-wall signals cannot immediately re-trigger ring entry.

**Architecture:** Extend the existing `a_run_track_element.c` element arbiter with `ELEMENT_WALL` and a small 5ms wall state machine. The wall detector uses `ad1/ad4` horizontal validity and the `ad2/ad3` high threshold, then runs a fixed down-wall timing window.

**Tech Stack:** Keil C251 C89 firmware, existing 5ms `run_time_1()` control chain, existing IPS114 menu debug display.

---

## Chunk 1: Wall State Machine

### Task 1: Add wall element and detector state

**Files:**
- Modify: `project/user/a_run_track_element.c`
- Modify: `project/user/a_run_track_element.h`

- [ ] Add wall thresholds:
  - `WALL_AD_SIDE_THRESHOLD = 40`
  - `WALL_AD_HIGH_THRESHOLD`
  - `WALL_TIMING_COUNT = 200` for 1000ms at 5ms.
- [ ] Add `ELEMENT_WALL`.
- [ ] Add `WallStep` enum and state variables.
- [ ] Add wall reset/start/update helpers.
- [ ] Add wall state getter.

### Task 2: Rewire element sequence

**Files:**
- Modify: `project/user/a_run_track_element.c`

- [ ] Change sequence to `LEFT_RING -> CYLINDER -> WALL -> LEFT_RING`.
- [ ] In `ELEMENT_WALL`, call `circle_check_l(0)` and consume stale ring finish events.
- [ ] When wall update completes, set `expected_element = ELEMENT_LEFT_RING`.

## Chunk 2: Public Debug API

### Task 3: Expose wall state through mode layer

**Files:**
- Modify: `project/user/a_run_mode.c`
- Modify: `project/user/a_run_mode.h`

- [ ] Add `a_run_mode_get_wall_state()`.
- [ ] Forward to `a_run_track_element_get_wall_state()`.

### Task 4: Show wall state in menu

**Files:**
- Modify: `project/service/menu.c`

- [ ] Add compact `W` display near existing `X`/`C` element debug values.

## Chunk 3: Verification

### Task 5: Build and inspect logs

**Files:**
- Read: `project/mdk/out_file/SEEKFREE.build_log.htm`

- [ ] Run Keil build:
  `& 'D:\keil_5\UV4\UV4.exe' -b 'project\mdk\seekfree.uvproj'`
- [ ] Confirm `0 Error(s), 0 Warning(s)`.
- [ ] If needed, run full rebuild or inspect object timestamp for modified files.

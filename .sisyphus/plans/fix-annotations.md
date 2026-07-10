# 修复用户代码注释错误

## TL;DR
> **Summary**: 修复 7 个用户代码文件中共 22 处注释错误，包括数值不一致、语义相反、命名误导、无信息量注释等问题。
> **Deliverables**: 注释修正后的源文件
> **Effort**: Quick
> **Parallel**: NO（纯文本修改，顺序执行即可）
> **Critical Path**: eeprom.h → eeprom.c → pid.c → ADC.c → a_run_fly.c → key.c → motor.c

## Context
### Original Request
用户要求检查并修复用户代码中不符合 AGENTS.md 注释规范的问题。

### 问题来源
调参过程中修改了代码值但忘记同步更新注释，导致注释与代码不一致。

## Work Objectives
### Core Objective
修复所有注释与代码不一致的问题，确保注释准确反映代码实际行为。

### Deliverables
- 7 个源文件的注释修正

### Definition of Done
- [ ] 所有注释数值与代码赋值一致
- [ ] 所有命名与实际功能对应
- [ ] 无信息量注释已删除
- [ ] 被注释掉的旧代码已清理或添加说明

### Must NOT Have
- 不修改任何代码逻辑
- 不修改任何变量名、函数名、结构体名（仅修改注释文本）
- 不添加新功能

## Verification Strategy
> 修改完成后人工复核注释与代码的一致性。

## Execution Strategy
### 顺序执行（纯文本修改，无需并行）

---

## TODOs

- [ ] 1. 修复 `eeprom.h` 结构体注释

  **What to do**:
  - L20: `@brief 速度环 PID 相关配置结构体` → `@brief 转向差速环 PID 相关配置结构体`
  - L21: `@details 主要用于电感差比和转向控制` → `@details 基于电感偏差的转向差速控制，包含基础运行速度设定`

  **Must NOT do**: 不修改结构体名 `AppSpeedConfig`（改动范围超出注释修复）

  **References**:
  - `project/service/eeprom.h:20-21`

  **Acceptance Criteria**:
  - [ ] brief 和 details 描述一致，且与成员变量实际用途匹配

---

- [ ] 2. 修复 `eeprom.c` 默认参数注释（8 处）

  **What to do**:
  - L38: `/* 默认平地负压百分比 70*/` → `/* 默认平地负压百分比 90 */`
  - L47: `/* 速度环 PID 默认参数 */` → `/* 转向差速环 PID 默认参数 */`
  - L51: `/* 默认基础速度 50 */` → `/* 默认基础速度 60 */`
  - L56: 删除 `// 0.85`（无信息量）
  - L68: `/* in_ring->pre_out_ring累计转角阈值150 */` → `/* in_ring->pre_out_ring累计转角阈值 220 */`
  - L69: `/* pre_out_ring固定目标角速度 15*/` → `/* pre_out_ring固定目标角速度 25 */`
  - L70: `/* pre_out_ring->drive_out_ring累计转角阈值160 */` → `/* pre_out_ring->drive_out_ring累计转角阈值 310 */`
  - L87: `/* 默认停止等待模式 */` → `/* 默认飞坡模式（0=飞坡，1=停止等待） */`

  **Must NOT do**: 不修改代码赋值

  **References**:
  - `project/service/eeprom.c:38,47,51,56,68,69,70,87`
  - `project/service/eeprom.h:81` — seesaw_mode 定义：0=飞坡，1=停止等待

  **Acceptance Criteria**:
  - [ ] 所有注释数值与代码赋值一致
  - [ ] seesaw_mode 注释与 eeprom.h 定义一致

---

- [ ] 3. 修复 `pid.c` 无信息量注释和过时注释

  **What to do**:
  - L73: `执行组合滤波后再转换为速度` → `执行低通滤波后再转换为速度`
  - L128: 删除 `// 更新输出并限幅`
  - L129: 删除 `//   pid->output += delta_output;`
  - L139: 删除 `// 更新误差历史`

  **Must NOT do**: 不修改代码逻辑

  **References**:
  - `project/service/pid.c:73,128,129,139`

  **Acceptance Criteria**:
  - [ ] 无信息量注释已删除
  - [ ] 函数头描述与实际实现一致

---

- [ ] 4. 清理 `ADC.c` 被注释掉的旧接线方案

  **What to do**:
  - L244~L247: 删除或添加说明 `/* 历史接线方案 v1（已废弃）：raw_buffer[0]=P01 ... */`
  - L253~L256: 删除或添加说明 `/* 历史接线方案 v2（已废弃）：raw_buffer[0]=P10 ... */`

  **Must NOT do**: 不修改当前生效的接线代码

  **References**:
  - `project/user/ADC.c:244-256`

  **Acceptance Criteria**:
  - [ ] 旧代码不再以无说明的注释形式保留

---

- [ ] 5. 修复 `a_run_fly.c` 缺失注释

  **What to do**:
  - L510: 在 `stop=1;` 前添加注释 `/* COOLDOWN 阶段锁停车，由 a_run_fly_update_release_speed() 负责阶梯释放速度 */`

  **Must NOT do**: 不修改代码逻辑

  **References**:
  - `project/user/a_run_fly.c:509-511`

  **Acceptance Criteria**:
  - [ ] COOLDOWN 阶段的 stop=1 行为有明确解释

---

- [ ] 6. 修复 `key.c` 引脚注释与实际接线不一致

  **What to do**:
  - L10: `/* 上/增加 P26*/` → `/* 上/增加 P37 */`
  - L11: `/* 下/减少 P41*/` → `/* 下/减少 P40 */`
  - L12: `/* 确定/切换 P40*/` → `/* 确定/切换 P41 */`
  - L13: `/* 返回/取消 P37*/` → `/* 返回/取消 P26 */`

  **Must NOT do**: 不修改宏定义的引脚值

  **References**:
  - `project/service/key.c:10-13`

  **Acceptance Criteria**:
  - [ ] 注释中的引脚号与宏定义的引脚号一致

---

- [ ] 7. 修复 `motor.c` 电机控制注释左右颠倒

  **What to do**:
  - L183: `/* --- 右电机控制逻辑 (硬件映射可能交叉) --- */` → `/* --- 左电机控制逻辑（lpwm_limited → P13/P14） --- */`
  - L199: `/* --- 左电机控制逻辑 --- */` → `/* --- 右电机控制逻辑（rpwm_limited → P52/P53） --- */`

  **Must NOT do**: 不修改代码逻辑

  **References**:
  - `project/service/motor.c:183,199`

  **Acceptance Criteria**:
  - [ ] 注释中的左右电机与实际变量名一致

---

## Commit Strategy
- Message: `fix(注释): 修复用户代码中 22 处注释错误`
- Files: eeprom.h, eeprom.c, pid.c, ADC.c, a_run_fly.c, key.c, motor.c

## Success Criteria
- [ ] 所有 22 处注释错误已修复
- [ ] 无代码逻辑变更
- [ ] 编译通过（`& 'D:\Keil_v5\UV4\UV4.exe' -b 'project\mdk\seekfree.uvproj'`）

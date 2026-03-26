# Speed Loop Dual Iteration Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `pwm_map -> pwm_identify -> air_dual -> ground_dual` 固定成一条不易跑偏的分阶段调参流程，并把 `air_dual` 改成双轮直测、每批 10 组、人工决定是否继续的交互式细调工具。

**Architecture:** 保留 `pwm_map` 和 `pwm_identify` 的前置职责不变，把调参主线集中在 host 侧。`air_dual` 从按 stage 拆开的单轮/双轮混合搜索改成双轮一起测、单维扰动的批次式搜索；`ground_dual` 从对称 `PidGains` 升级成左右轮独立 `WheelPidGains`，直接承接 `air_dual.best_pid`。

**Tech Stack:** Python 3、串口 VOFA 主机脚本、JSON profile、`unittest`、现有 `speed_loop_autotune` host/common/profile 工具。

---

## File Map

**Create**
- `docs/superpowers/plans/2026-03-26-speed-loop-dual-iteration-plan.md`

**Modify**
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\air_dual.py`
  - 去掉按左轮/右轮/双轮 stage 分离的搜索主流程
  - 引入双轮直测的批次 10 组候选、交互 `continue/stop`、统一 `combined_score`
  - 批次结束时只输出全批最佳，不在 stage 切换时重置最佳值
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\ground_dual.py`
  - 从对称 `PidGains` 升级为左右轮独立 `WheelPidGains`
  - 默认起点优先读取 `ground_dual.best_pid -> air_dual.best_pid -> pwm_identify.seed_pi`
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\common.py`
  - 扩展 profile 读写兼容逻辑
  - 支持 `ground_dual.best_pid` 的左右轮结构与旧对称结构兼容
  - 维护 `air_dual.last_batch_best / batch_round / batch_history`
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\vofa_autotune.py`
  - `--candidate-limit` 文案调整为“每批候选上限”
  - 增加 `--interactive-batches`
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_vofa_autotune.py`
  - 增加批次交互、全批最佳、`combined_score` 一致性、`ground_dual` 独立 PID 的测试
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\README.md`
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\docs\protocol.md`
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\docs\tuning_rules.md`
- `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\docs\ground_dual_tuning_rules.md`

**Do Not Modify**
- 固件 VOFA 协议与串口命令字
- `pwm_map` CSV 结构
- `shared_targets.custom_sequences` 的现有写法
- `AT_FUYA` 的独立处理逻辑与 `0~4000` 范围

## Implementation Constraints

- `air_dual` 本轮不再使用“左轮单独 -> 右轮单独 -> 双轮 refine”的流程。
- `air_dual` 每批 `10` 组，按全批统一计数，而不是按 stage 分开计数。
- 双轮一起测，但每次候选只改 1 个维度，优先搜索 `Kp/Ki`，`Kd` 默认不进首批。
- 搜索、展示、批次最佳、继续细化、最终写回全部统一用 `combined_score`。
- `continue` 不是从中断点恢复，而是以上一批最佳 PID 作为新 baseline，重新开一批 10 组。
- `stop` 时才执行最终验证并写 `air_dual.best_pid`；非交互单批运行只打印结果，不覆盖最终 best。
- `ground_dual` 本轮直接升级成左右轮独立 PID，不再折叠成对称单轮起点。

## Chunk 1: Profile And Shared Data Contracts

### Task 1: 固定 profile 的新增字段和兼容规则

**Files:**
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\common.py`
- Test: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_vofa_autotune.py`

- [ ] **Step 1: 为 `air_dual` 批次字段定义稳定结构**

在计划里采用下面这个目标结构，避免实现时字段漂移：

```json
"air_dual": {
  "baseline_pid": {"left": {...}, "right": {...}},
  "best_pid": {"left": {...}, "right": {...}},
  "last_summary": {
    "a_score": 0.0,
    "b_score": 0.0,
    "combined_score": 0.0,
    "autotune_sequence": "...",
    "verify_sequence": "..."
  },
  "last_batch_best": {
    "batch_round": 1,
    "stage_stopped": "batch-stop",
    "best_pid": {"left": {...}, "right": {...}},
    "a_score": 0.0,
    "b_score": 0.0,
    "combined_score": 0.0
  },
  "batch_round": 1,
  "batch_history": []
}
```

- [ ] **Step 2: 固定 `ground_dual.best_pid` 的新结构**

把目标结构统一成左右轮独立：

```json
"ground_dual": {
  "baseline_pid": {"left": {...}, "right": {...}},
  "best_pid": {"left": {...}, "right": {...}},
  "last_summary": {...}
}
```

兼容规则固定为：
- 旧 profile 若还是对称 `{"kp": ..., "ki": ..., "kd": ...}`，读取时复制到 `left/right`
- 新 profile 一律写回独立结构

- [ ] **Step 3: 写兼容性单测**

新增测试覆盖：
- 旧 `ground_dual.best_pid` 对称结构可读
- 缺少 `air_dual.last_batch_best / batch_round / batch_history` 时可自动补默认值
- `batch_history` 超过 20 条时裁剪最早记录

- [ ] **Step 4: 运行共享层测试**

Run:

```bash
python -m unittest project.speed_loop_autotune.tests.test_vofa_autotune
```

Expected:
- 新增的 profile 兼容测试通过
- 既有 `custom_sequences` / `shared_targets` 测试不回归

- [ ] **Step 5: Commit**

```bash
git add project/speed_loop_autotune/host/common.py project/speed_loop_autotune/tests/test_vofa_autotune.py
git commit -m "feat: add dual-iteration profile contracts"
```

## Chunk 2: `air_dual` 批次式双轮直测

### Task 2: 去掉 stage 主导搜索，改成双轮一起测的 10 组批次

**Files:**
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\air_dual.py`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\vofa_autotune.py`
- Test: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_vofa_autotune.py`

- [ ] **Step 1: 明确 1 批 10 组的候选生成规则**

实现前先把规则写死，避免运行时越改越散：

```text
组1  baseline
组2  L.Kp - step
组3  L.Kp + step
组4  L.Ki - step
组5  L.Ki + step
组6  R.Kp - step
组7  R.Kp + step
组8  R.Ki - step
组9  R.Ki + step
组10 adaptive slot
```

其中 `adaptive slot` 规则固定为：
- 若 `Kd` 搜索未开启，优先补最差轮的主导维度二次细化
- 若 `Kd` 搜索条件满足，则第 10 组改成对应轮的 `Kd` 候选

- [ ] **Step 2: 把候选评估改成统一的双轮评估入口**

引入一个清晰的候选评估函数，输入输出固定为：

```python
def evaluate_dual_candidate(client, args, trial, verify_trial, score_config, gains_pair):
    return {
        "gains": gains_pair,
        "a_score": ...,
        "b_score": ...,
        "combined_score": ...,
        "a_result": ...,
        "b_result": ...,
    }
```

要求：
- 所有候选都走同一套 `air_primary + air_verify`
- 不再出现单轮 stage 专用 evaluator
- `combined_score` 是唯一主排序值

- [ ] **Step 3: 把批次最佳改成全批全局最优**

删除或替换当前按 stage 重置的 `CandidateLimitTracker` 语义，目标行为固定为：
- 整批开始时初始化 `batch_best=None`
- 每评估 1 组候选，若 `combined_score` 更低，则刷新 `batch_best`
- 不管当前在第几组、做了哪种维度扰动，`batch_best` 都是全批唯一最佳

- [ ] **Step 4: 实现批次停止与交互**

行为固定为：
- 达到 `candidate_limit` 后立即结束本批候选生成
- 打印：
  - `batch_round`
  - `candidate_count`
  - 本批最佳左右轮 PID
  - `a_score / b_score / combined_score`
  - 下一批 baseline
- 若 `--interactive-batches` 开启且终端可交互：
  - 输入 `continue`：开启下一批
  - 输入 `stop`：执行最终验证
- 若非交互：
  - 只结束当前批次并退出
  - 不写最终 `air_dual.best_pid`

- [ ] **Step 5: 实现最终验证和写回**

停止后行为固定为：
- 用最后一批最佳 PID 再跑一次完整最终验证
- 最终验证也统一用 `combined_score`
- 验证结果写入：
  - `air_dual.best_pid`
  - `air_dual.baseline_pid`
  - `air_dual.last_summary`
  - `air_dual.last_batch_best`
  - `air_dual.batch_round`
  - `air_dual.batch_history`

- [ ] **Step 6: 更新 CLI 参数语义**

`vofa_autotune.py` 中调整：
- `--candidate-limit`
  - 文案改为“每批候选上限”
  - 默认保持 `10`
- 新增：

```text
--interactive-batches
```

默认对 `air-dual` 开启

- [ ] **Step 7: 写批次行为测试**

新增测试覆盖：
- 单批正好 10 组时停止
- 一批内 best 不重置
- `continue` 以后下一批 baseline 等于上一批最佳
- `stop` 后执行最终验证并写 `air_dual.best_pid`
- 非交互模式下只跑一批、不写最终 best
- evaluator 与打印结果统一使用 `combined_score`

- [ ] **Step 8: 运行 host 层回归**

Run:

```bash
python -m unittest project.speed_loop_autotune.tests.test_vofa_autotune
python -m py_compile project\speed_loop_autotune\host\air_dual.py project\speed_loop_autotune\host\vofa_autotune.py
```

Expected:
- 批次交互测试全部通过
- `air_dual` 语法检查通过

- [ ] **Step 9: Commit**

```bash
git add project/speed_loop_autotune/host/air_dual.py project/speed_loop_autotune/host/vofa_autotune.py project/speed_loop_autotune/tests/test_vofa_autotune.py
git commit -m "feat: add interactive dual-batch air tuning"
```

## Chunk 3: `ground_dual` 升级为左右轮独立 PID

### Task 3: 把 `ground_dual` 从对称 `PidGains` 升级到 `WheelPidGains`

**Files:**
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\ground_dual.py`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\host\common.py`
- Test: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\tests\test_vofa_autotune.py`

- [ ] **Step 1: 固定 `ground_dual` 起始 PID 优先级**

把读取顺序改成：
1. `ground_dual.best_pid`
2. `air_dual.best_pid`
3. `pwm_identify.seed_pi`
4. CLI 默认值复制到左右轮

不允许再用“取 left 轮折叠成对称 PID”的旧逻辑。

- [ ] **Step 2: 把内部候选与应用函数切到左右轮独立**

把 `ground_dual` 内部搜索从：

```python
PidGains(kp, ki, kd)
```

升级成：

```python
WheelPidGains(
    PidGains(...),
    PidGains(...),
)
```

要求：
- 应用 PID 时左右轮分别下发
- profile 写回时左右轮分别记录
- 保持当前下地 trial、冷却、回程、`AT_FUYA` 逻辑不变

- [ ] **Step 3: 保持当前定位，不把 `ground_dual` 变成第二个 `air_dual`**

限制本轮范围：
- 不改 `ground_dual` 的模式定位
- 不引入批次式 continue/stop 交互
- 不反向覆盖 `air_dual` 的职责
- 只把起点和内部 PID 结构升级成左右轮独立

- [ ] **Step 4: 写 `ground_dual` 兼容与起点测试**

新增测试覆盖：
- `ground_dual.best_pid` 缺失时优先用 `air_dual.best_pid`
- `air_dual.best_pid` 缺失时再回退 `pwm_identify.seed_pi`
- 旧对称 `ground_dual.best_pid` 可兼容读取
- 新运行结果写回独立左右轮结构

- [ ] **Step 5: 运行 `ground_dual` 回归**

Run:

```bash
python -m unittest project.speed_loop_autotune.tests.test_vofa_autotune
python -m py_compile project\speed_loop_autotune\host\ground_dual.py
```

Expected:
- `ground_dual` 新旧 profile 兼容测试通过
- 语法检查通过

- [ ] **Step 6: Commit**

```bash
git add project/speed_loop_autotune/host/ground_dual.py project/speed_loop_autotune/host/common.py project/speed_loop_autotune/tests/test_vofa_autotune.py
git commit -m "feat: make ground dual use independent wheel pid"
```

## Chunk 4: Docs, Rules, And Validation

### Task 4: 把流程说明同步到项目文档

**Files:**
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\README.md`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\docs\protocol.md`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\docs\tuning_rules.md`
- Modify: `C:\Users\ye\Desktop\龙丘电机\project\speed_loop_autotune\docs\ground_dual_tuning_rules.md`

- [ ] **Step 1: 更新 README 的标准流程**

明确写成：
- `pwm_map`：新电机摸底
- `pwm_identify`：种子 `PI`
- `air_dual`：双轮直测、每批 10 组、人工继续/停止
- `ground_dual`：以 `air_dual.best_pid` 为起点的最终带载收敛

- [ ] **Step 2: 更新 protocol 和 tuning rules**

文档必须明确：
- `air_dual` 默认使用 `combined_score`
- 每批默认 10 组
- 每批候选是双轮一起测、单维扰动
- `continue` 和 `stop` 的行为
- `ground_dual` 已升级成左右轮独立 PID

- [ ] **Step 3: 做最终命令级验证**

Run:

```bash
python -m unittest project.speed_loop_autotune.tests.test_vofa_autotune
python project\speed_loop_autotune\host\vofa_autotune.py --help
python tools\vofa_autotune.py --help
```

Expected:
- 所有 `unittest` 通过
- `--candidate-limit`、`--interactive-batches` 文案正确
- 现有 mode 和 `custom_sequences` 行为不回归

- [ ] **Step 4: Commit**

```bash
git add project/speed_loop_autotune/README.md project/speed_loop_autotune/docs/protocol.md project/speed_loop_autotune/docs/tuning_rules.md project/speed_loop_autotune/docs/ground_dual_tuning_rules.md
git commit -m "docs: document dual-batch tuning workflow"
```

## Acceptance Checklist

- [ ] `pwm_map` 与 `pwm_identify` 的前置链路不回归
- [ ] `air_dual` 不再按单轮 stage 搜索，而是双轮一起测
- [ ] `air_dual` 每批 10 组，按全批统一计数
- [ ] `air_dual` 批次最佳按 `combined_score` 统一选出
- [ ] `air_dual` 支持 `continue / stop`
- [ ] `stop` 后才写最终 `air_dual.best_pid`
- [ ] `ground_dual` 已改成左右轮独立 PID
- [ ] `ground_dual` 默认优先继承 `air_dual.best_pid`
- [ ] 旧 profile 兼容可读
- [ ] 文档与实现口径一致

## Notes For Execution

- 先做 `common.py` 的 profile 契约，再做 `air_dual`，最后做 `ground_dual`。不要反过来，以免 profile 结构还没稳定就反复返工。
- `air_dual` 的批次交互优先通过单元测试和 fake client 覆盖，先别直接依赖真机验证主逻辑。
- `ground_dual` 本轮只升级 PID 结构和起点优先级，不做第二套批次交互，避免范围膨胀。
- 文档用语必须与最终 CLI 行为一致，尤其是：
  - “每批 10 组”
  - “双轮一起测”
  - “continue 后以上一批最佳重开新批”
  - “stop 后才写最终 best”

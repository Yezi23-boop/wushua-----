# 软件定时器模块实现方案 (修订版)

## 1. 摘要
根据您的需求，实现一个非阻塞的超时检测与后台自动累加定时器。
提供 `timeadd(uint32 *count, uint32 target_time)` 函数：在调用时自动将 `count` 注册到 10ms 定时器任务 (`run_time_2`) 中进行后台累加。当 `*count` 达到 `target_time` 时返回 1。
提供 `timedestroy(uint32 *count)` 函数：当计时完成后，调用此函数将变量从后台定时器队列中移除，并将其清零。

## 2. 详细设计

### 2.1 核心机制
- 在 `run_time_2` (10ms) 中维护一个指针数组 `timer_list`。
- 每过 10ms，遍历数组，将所有非空的指针指向的变量累加 10。
- `timeadd` 函数负责检查变量是否已注册，若未注册则加入数组，并判断当前值是否达标。
- `timedestroy` 负责从数组中注销该指针并清零变量。

### 2.2 新增文件
**`project/service/soft_timer.h`**:
```c
#ifndef __SOFT_TIMER_H__
#define __SOFT_TIMER_H__

#include "zf_common_typedef.h"

/* 注册到后台 10ms 累加队列，达到 target_time 返回 1，否则返回 0 */
int timeadd(uint32 *count, uint32 target_time);

/* 停止该变量的后台累加并清零 */
void timedestroy(uint32 *count);

/* 供 10ms 定时器中断调用的后台更新函数 */
void soft_timer_update_10ms(void);

#endif
```

**`project/service/soft_timer.c`**:
- 实现 `timeadd`：遍历 `timer_list`，若不存在则存入空闲位（需要关中断保护）。判断 `*count >= target_time` 并返回结果。
- 实现 `timedestroy`：在 `timer_list` 中找到对应指针并置 `NULL`（关中断保护），然后 `*count = 0`。
- 实现 `soft_timer_update_10ms`：遍历 `timer_list`，如果指针不为空，则 `*ptr += 10`。

### 2.3 集成修改
- 修改 **`project/user/a_run.c`**:
  - 引入 `#include "../service/soft_timer.h"`。
  - 在 `run_time_2()` (10ms) 函数内，追加调用 `soft_timer_update_10ms()`。

## 3. 使用示例
```c
static uint32 my_delay_count = 0;

void my_task() {
    // 开始/继续计时，当累加到 1000ms 时触发
    if (timeadd(&my_delay_count, 1000)) {
        // 1000ms 已到，执行逻辑
        // ...
        // 销毁并清零，以便下次重新计时
        timedestroy(&my_delay_count);
    }
}
```

## 4. 验证计划
- 编写代码并确保符合 C89 标准（变量声明在块首）。
- 确保对 `timer_list` 的修改受 `EA = 0 / EA = 1` 保护，避免中断冲突。
- 确认 `run_time_2` 调用不受阻塞。

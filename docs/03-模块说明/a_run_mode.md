# a_run_mode 模块

## 模块职责

`a_run_mode.c` 负责与赛道模式和业务策略相关的逻辑，包括起跑状态、飞坡速度处理、负压触发和测试入口。

## 对外入口函数

- `a_run_mode_update_start_state()`
- `a_run_mode_update_fuya_state()`
- `a_run_mode_update_fly_speed(int *speed)`
- `run_test_speed()`
- `run_test_angle()`

## 依赖与被依赖关系

依赖：

- `app`
- `FUYA`
- 外部状态 `flat_statr`

被依赖：

- `a_run.c`
- `isr.c` 中的历史测试路径注释

## 关键运行数据

- `flat_statr`：起跑状态
- `flat_fly`：飞坡阶段标志
- `count_fly_1`
- `count_fly_2`

## 高频路径注意事项

- 模式逻辑会直接改速度目标和部分输出行为，必须控制复杂度
- 飞坡逻辑应尽量只基于已经准备好的状态，不要在这里引入大块新增计算
- 起跑和负压触发路径属于业务逻辑，不应反向污染基础控制模块

## 调参与常见风险

- 如果起跑逻辑反应不稳定，先检查 `flat_statr` 的状态推进条件
- 如果飞坡阶段速度切换突兀，先看计数器和切换阈值是否合理
- 如果负压没及时介入，先确认 `start_flag`、`flat_statr` 和 `fuya_update_simple()` 的调用条件

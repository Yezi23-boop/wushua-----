# a_run_mode 模块

## 模块职责

`a_run_mode.c` 负责启停状态机、负压启停条件和赛道元素状态的只读桥接。当前飞坡/跷跷板策略已经拆到 `a_run_fly.c`，圆环、圆桶和墙面仲裁拆到 `a_run_track_element.c`。

## 对外入口函数

- `a_run_mode_update_start_state()`
- `a_run_mode_get_start_state()`
- `a_run_mode_update_fuya_state()`
- `a_run_mode_get_ring_state()`
- `a_run_mode_get_expected_element()`
- `a_run_mode_get_cylinder_state()`
- `a_run_mode_get_wall_state()`
- `a_run_mode_get_cylinder_vz()`

## 依赖与被依赖关系

依赖：

- `app`
- `FUYA`
- `a_run_track_element`

被依赖：

- `a_run.c`
- 菜单和调试显示

## 关键运行数据

- `current_start_state`：内部启停状态，0-停止，1-预启动，2-运行
- `press_debounce_cnt`：P36 启动按键消抖计数
- `key_released`：释放锁存，防止长按重复触发
- `start_delay_ticks`：预启动到运行态的确认倒计时

## 高频路径注意事项

- `a_run_mode_update_start_state()` 在 10ms 状态链中运行，只做按键消抖和状态推进
- 5ms 主控制链只通过 `a_run_mode_get_start_state()` 读取运行态，避免把按键扫描放进高频路径
- 元素状态查询函数只转发只读状态，不负责推进状态机

## 调参与常见风险

- 如果按键触发不稳定，先检查 P36 电平、`START_DEBOUNCE_TIME` 和释放锁存
- 如果按下后没有立即跑车，要确认当前是否处于 1s 预启动确认窗口
- 如果菜单显示的圆环、圆桶、墙面状态异常，继续看 `a_run_track_element.md`
- 如果飞坡阶段速度切换突兀，继续看 `a_run_fly.md`

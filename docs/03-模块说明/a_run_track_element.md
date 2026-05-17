# a_run_track_element 模块

## 模块职责

`a_run_track_element.c` 负责左圆环、圆桶、跷跷板/飞坡和墙面的串行仲裁。它用当前期望元素限制入口识别，避免圆桶或墙面过渡段误触发下一次圆环。元素顺序由 `app.start.element_len` 和 `app.start.element_seq[]` 决定，便于比赛现场临时调整。

## 对外入口函数

- `a_run_track_element_update_gate(int *speed, float *angle_target)`
- `a_run_ring_get_state()`
- `a_run_track_element_get_expected_element()`
- `a_run_cylinder_get_state()`
- `a_run_wall_get_state()`
- `a_run_track_element_get_cylinder_vz()`

## 依赖与被依赖关系

依赖：

- `app.start.element_enable`
- `app.ring`
- `ad1~ad4`
- `gyro_z`
- `speed_l / speed_r`
- `imu_get_gravity_vz()`
- `a_run_fly`
- `FUYA` 的圆桶过顶参数切换接口

被依赖：

- `a_run.c`
- `a_run_mode.c`
- 菜单和调试显示

## 元素仲裁顺序

默认串行顺序为：

```text
1 左圆环 -> 3 圆桶 -> 5 跷跷板/飞坡 -> 4 墙面 -> 1 左圆环
```

实际顺序由 `element_len` 和 `element_seq[0..5]` 决定。编号含义：

| 编号 | 元素 | 当前处理 |
| --- | --- | --- |
| 0 | 空槽 | 跳过 |
| 1 | 左圆环 | 可执行 |
| 2 | 右圆环 | 可执行，入口和角速度方向取反 |
| 3 | 圆桶 | 可执行 |
| 4 | 墙面 | 可执行 |
| 5 | 跷跷板/飞坡 | 可执行，是否进入由元素序列决定 |

如果关闭 `app.start.element_enable`，模块会清理圆环、圆桶、墙面和飞坡状态，并对外显示为空元素；重新开启后的第一拍才会扫描 `E1~E6`，进入序列中的第一个有效元素。全空、全非法或当前不可执行时，运行期会进入空状态，不会改写 `app.start.element_seq[]` 或 EEPROM。

运行中修改序列不会打断当前元素，当前元素完成后才按新序列跳转。填入右圆环编号 `2` 时当前会跳过，后续实现右环后可直接接入。

## 各阶段职责

- 左圆环：入口要求四路电感满足特征，并使用连续确认和上升沿约束；环内通过 `ring_data.diff_set` 覆盖目标角速度。
- 圆桶：通过电感强信号窗口、回地确认和稳定延迟判断完成，并在过顶阶段调用负压模块切换 angle 参数。
- 跷跷板/飞坡：在 `ELEMENT_SEESAW` 中推进 HOLD/RECOVER；完成后由 `a_run_fly` 的 COOLDOWN 后处理继续阶梯增速，不依赖下一个元素类型。
- 墙面：圆桶或跷跷板后等待墙面强信号，再计时完成，完成后重新开放左圆环。

## 高频路径注意事项

- `a_run_track_element_update_gate(int *speed, float *angle_target)` 运行在 5ms 主控制链中，应避免串口输出和复杂计算。
- 圆环角度累计依赖 `gyro_z` 已按 5ms 周期准备好；若 IMU 缩放或周期改变，圆环阈值要重新标定。
- `expected_element` 是误触发防线，不应被菜单或调试代码直接改写。

## 调参与常见风险

- 圆环误触发：优先看入口阈值、连续确认次数和 `expected_element` 是否确实为左圆环。
- 圆桶完成过早：提高强信号命中条件或延长稳定延迟。
- 跷跷板没有触发：确认序列中是否包含 `TRACK_ELEMENT_SEESAW`，以及飞坡入口弱磁条件是否能成立。
- 墙面后不重新开放圆环：检查墙面强信号阈值和 `WALL_TIMING_COUNT` 是否符合实际路段。

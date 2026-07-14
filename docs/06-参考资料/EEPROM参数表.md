# EEPROM 参数表

## 参数分组

当前 EEPROM 配置由全局 `AppConfig app` 承载。

### `start`

| 字段 | 含义 |
| --- | --- |
| `start_flag` | 起跑/业务开关相关标志 |
| `element_enable` | 整体赛道元素识别开关 |
| `track_mode` | 赛道元素模式，当前主要使用 0 |
| `fuya_xili` | 平地负压百分比 |
| `fuya_wall_percent` | 墙面负压百分比保留字段，当前固定负压策略不使用 |
| `element_len` | 元素序列有效长度，范围 1~6 |
| `element_seq[0..5]` | 元素序列槽位，0空、1左环、2右环预留、3圆桶、4墙面、5跷跷板 |

### `speed`

| 字段 | 含义 |
| --- | --- |
| `kp_Err` | 转向外环比例参数 |
| `kd_Err` | 转向外环微分参数 |
| `gyro_damp_Err` | 转向外环 gyro 阻尼参数 |
| `speed_run` | 基础运行目标速度 |
| `limiting_Err` | 转向外环输出限幅 |
| `kp2_Err` | 转向外环非线性误差项系数 |

### `angle`

| 字段 | 含义 |
| --- | --- |
| `kp_Angle` | 圆环角速度环比例参数 |
| `kd_Angle` | 圆环角速度环微分参数 |
| `gyro_feedback_scale` | 圆环 gyro_z 反馈缩放系数 |
| `limiting_Angle` | 圆环角速度环差速 PWM 限幅 |
| `A_1` | 主亮度权重 |
| `B_1` | 竖向差分权重 |
| `C_l` | 弱信号分母补偿权重 |

### `ring`

| 字段 | 含义 |
| --- | --- |
| `ring_entry_encoder` | ring 阶段编码器积分阈值 |
| `pre_ring_Gyro_target` | pre_ring 固定目标角速度 |
| `pre_ring_Gyroz` | pre_ring 累计转角阈值 |
| `in_ring_Gyroz` | in_ring 累计转角阈值 |
| `pre_out_ring_Gyro_target` | pre_out_ring 固定目标角速度 |
| `pre_out_ring_Gyroz` | pre_out_ring 累计转角阈值 |

### `fly`

| 字段 | 含义 |
| --- | --- |
| `count_fly_speed` | 飞坡/跷跷板目标速度 |
| `count_fly_time_1` | 弱磁入口确认次数，按 5ms 累计 |
| `count_fly_time_2` | HOLD 保持时间，按 5ms 累计 |

## 存储策略说明

- 当前实现保留旧地址布局
- `eeprom_init()` 负责加载和初始化
- `eeprom_flash()` 负责回写
- 业务上统一通过 `config_save()` 和 `config_load()` 驱动
- 旧 EEPROM 的新槽位可能是随机值；配置层不再校验元素序列，运行期由赛道元素仲裁跳过不可执行槽位或进入空状态

## 使用建议

- 新增参数前，先判断是否必须掉电保存
- 调试阶段的临时变量不要随意塞进 EEPROM
- 会保存的参数应优先归入 `app`

## 槽位索引

| EEPROM 槽位 | 字段 |
| --- | --- |
| 30 | `element_len` |
| 31 | `element_seq[0]` |
| 32 | `element_seq[1]` |
| 33 | `element_seq[2]` |
| 34 | `element_seq[3]` |
| 35 | `element_seq[4]` |
| 36 | `element_seq[5]` |

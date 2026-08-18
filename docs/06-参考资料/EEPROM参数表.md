# EEPROM 参数表

本文档描述当前固件的 EEPROM 配置存储布局（表驱动槽位模型），实现位于 `project/service/eeprom.c` / `eeprom.h`。

## 存储模型

- 配置表 `eeprom_config_items[]` 用 `EEPROM_INT/FLOAT(member, slot, default)` 宏描述"字段 → 槽位"映射，运行时统一从全局结构体 `AppConfig app` 读写。
- 每个槽位 4 字节；RAM 缓冲区 `date_buff[400]` 覆盖槽位 0~99，整体映射到 Flash 配置扇区 `0x200`（512 字节扇区，400 字节不跨扇区）。
- 槽位 99 为初始化标志（`EEPROM_INIT_FLAG_SLOT`），位于缓冲区末尾：标志随整包最后写入，读到标志有效即整包参数已写完。首次上电、Flash 被擦除或写中途掉电时，加载默认值并整包刷写。
- 全部 float 参数读取时统一过 `eeprom_float_is_abnormal()` NaN 检测，脏数据逐项回退默认值，防止污染控制链。
- 现场烧录为整片重新烧录（EEPROM 配置扇区一并擦除重写），因此修改槽位布局或默认值无需旧数据版本迁移；现场调参后需重新保存。

## 参数分组

参数结构体定义在 `project/service/eeprom.h`。

### `start`（AppStartConfig）

| 字段 | 含义 |
| --- | --- |
| `start_flag` | 启动标志：1-运行，0-待机 |
| `element_enable` | 赛道元素识别总开关：1-开启，0-关闭 |
| `fuya_xili` | 平地负压吸附百分比，范围 0~100 |
| `encoder_stop_distance_cm` | 上电累计里程达到该值后停车（cm） |
| `element_seq[0..7]` | 元素序列 E1~E8：0空、1左环、2右环、3大圆环左、4大圆环右、5圆桶、6墙面、7跷跷板、8双十字、9单十字；其他值运行期跳过 |

### `speed`（AppSpeedConfig，转向差速环）

| 字段 | 含义 |
| --- | --- |
| `kp_Err` | 转向环比例系数 |
| `kd_Err` | 转向环微分系数 |
| `gyro_damp_Err` | 转向环陀螺仪阻尼，抑制高速摆振 |
| `speed_run` | 赛道基础运行速度 |
| `limiting_Err` | 转向输出限幅 |
| `kp2_Err` | 转向环二次项非线性增强系数 |

### `angle`（AppAngleConfig，电感偏差解算/角速度内环）

| 字段 | 含义 |
| --- | --- |
| `kp_Angle` | 角速度内环比例系数 |
| `kd_Angle` | 角速度内环微分系数 |
| `gyro_feedback_scale` | 角速度反馈缩放，匹配 gyro_z 量级 |
| `limiting_Angle` | 角速度内环输出限幅 |
| `A_1` | 横向主差分权重 |
| `B_1` | 竖向差分权重，斜入/斜出姿态修正 |
| `C_l` | 分母补偿权重，弱信号时抑制偏差放大 |

### `ring`（AppRingConfig）

小圆环 `small_profile` 与大圆环 `large_profile` 各持一套独立的 `AppRingProfileConfig`（19 个参数），共用同一圆环状态机；仲裁层进入圆环元素时把对应参数组指针交给状态机。

| 字段（小/大各一套） | 含义 |
| --- | --- |
| `gain_speed_slope` | 进环增益随目标速度的补偿斜率 |
| `bias_entry_gain` | 进环阶段同侧两路电感放大倍数 |
| `bias_exit_gain` | 出环阶段对侧两路电感放大倍数 |
| `entry_straight_encoder` | 入口识别后零角速度直走距离（cm） |
| `bias_entry_yaw` | 结束进环偏置的累计转角阈值（度） |
| `bias_entry_encoder` | 结束进环偏置的里程阈值（cm） |
| `bias_finish_encoder` | 出环判定的里程阈值（cm） |
| `bias_finish_yaw` | 出环满圈角度积分阈值（度），与里程双条件确认 |
| `target_speed` | 进环/环内/出环目标速度 |
| `adc_a_1` / `adc_b_1` / `adc_c_l` | 圆环阶段横向主差分 / 辅助电感差分 / 分母补偿权重 |
| `kp_Err` / `kd_Err` / `kp2_Err` | 圆环阶段方向环比例 / 微分 / 非线性增强系数 |
| `kp_Angle` / `kd_Angle` | 圆环阶段角速度内环比例 / 微分系数 |

### `fly`（AppFlyConfig，飞坡 + 停止等待）

| 字段 | 含义 |
| --- | --- |
| `seesaw_mode` | 模式选择：0-飞坡，1-停止等待 |
| `fly_speed` | 飞坡 LOW 阶段目标速度 |
| `fly_detect_count` | 飞坡入口弱磁确认次数 |
| `fly_recover_speed` | 飞坡 COOLDOWN 恢复速度 |
| `fly_land_confirm_count` | 飞坡落地回升连续确认次数 |
| `fly_release_step` | 飞坡 COOLDOWN 调节步长 |
| `seesaw_speed` | 停止等待 CREEP 阶段目标速度 |
| `seesaw_detect_count` | 跷跷板入口命中次数 |
| `seesaw_wait_count` | 停车等待时间（×2ms） |
| `seesaw_creep_cm` | 停止等待前挪距离（cm） |
| `seesaw_release_step` | 停止等待 COOLDOWN 调节步长 |
| `center_a_1` / `center_b_1` / `center_c_l` | 释放期居中 ABC 权重（两模式共用） |

### `cylinder`（AppCylinderConfig，圆桶）

| 字段 | 含义 |
| --- | --- |
| `encoder_target` | 圆桶编码器积分退出阈值 |
| `ad_both_high_threshold` | 圆桶双路强信号识别阈值 |
| `adc_a_1` / `adc_b_1` / `adc_c_l` | 圆桶专用 ABC 权重 |
| `kp_Err` / `kd_Err` | 圆桶专用方向环系数 |
| `exit_slow_speed` | 圆桶确认后阶梯减速的最低目标速度 |

### `wall`（AppWallConfig，墙面）

| 字段 | 含义 |
| --- | --- |
| `entry_speed` | 墙面全程目标速度，低于 speed_run 减速、高于则加速 |
| `timing_count` | 墙面阶段下墙计时（×2ms） |
| `encoder_target` | 墙面退出编码器积分阈值 |

### `cross`（AppCrossConfig，双十字）与 `cross_single`（AppCrossSingleConfig，单十字）

两者字段完全相同，各持独立默认值与槽位：

| 字段 | 含义 |
| --- | --- |
| `encoder_target` | 退出编码器积分阈值 |
| `adc_a_1` / `adc_b_1` / `adc_c_l` | 专用 ABC 权重 |
| `kp_Err` / `kd_Err` / `kp2_Err` | 专用方向环系数 |

## 槽位布局（按菜单显示顺序）

| 槽位 | 内容 |
| --- | --- |
| 1~8 | 元素序列 E1~E8（放最前便于现场调整序列） |
| 9~13 | START |
| 14~19 | CTRL |
| 20~25 | MODEL |
| 26~42 | 小圆环 RING |
| 43~59 | 大圆环 RING（紧跟小圆环，与菜单 RING 页顺序一致） |
| 60~67 | CYLINDER |
| 68~70 | WALL |
| 71~84 | FLY |
| 85~91 | CROSS（双十字） |
| 92~98 | CROSSS（单十字） |
| 99 | 初始化标志 |

## 存储流程

- `eeprom_init()`：读配置扇区到 `date_buff` → 检查槽位 99 标志 → 无效则先加载默认值、写标志、`eeprom_flash()` 整包刷写；有效则按表读入 `app`。
- `eeprom_flash()`：把 `app` 按表序列化到 `date_buff` 后单次 IAP 刷写。菜单保存参数时调用，Flash 擦写耗时长，严禁在运行态（电机未断脱）调用。
- 新增持久化参数 = 在对应结构体加字段 + 参数表加一行（槽位 + 默认值），读写/默认值加载全部由表驱动完成。

## 使用建议

- 新增参数前先判断是否必须掉电保存。
- 调试阶段的临时变量不要随意塞进 EEPROM。
- 会保存的参数应优先归入 `app`。
- 改默认值不会影响现场已刷写的车；调整槽位布局后按整片重烧处理，现场调参需重新保存。

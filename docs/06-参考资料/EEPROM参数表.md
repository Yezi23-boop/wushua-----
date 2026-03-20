# EEPROM 参数表

## 参数分组

当前 EEPROM 配置由 `AppConfig g_aapp_config 一承载。

### `start`

| 字段           | 含义                  |
| -------------- | --------------------- |
| `start_flag`   | 起跑/业务开关相关标志 |
| `circle_flags` | 圆环相关标志          |
| `fuya_xili`    | 负压输出基准参数      |

### `speed`

| 字段           | 含义                   |
| -------------- | ---------------------- |
| `kp_Err`       | 转向环比例参数         |
| `kd_Err`       | 转向环微分参数         |
| `speed_run`    | 运行目标速度           |
| `limiting_Err` | 转向环输出限幅         |
| `kp2_Err`      | 转向环非线性误差项系数 |

### `angle`

| 字段             | 含义             |
| ---------------- | ---------------- |
| `kp_Angle`       | 角度环比例参数   |
| `kd_Angle`       | 角度环微分参数   |
| `limiting_Angle` | 角度环输出限幅   |
| `A_1`            | 电感误差映射参数 |
| `B_1`            | 电感误差映射参数 |
| `C_l`            | 电感误差映射参数 |

### `ring`

| 字段                    | 含义                   |
| ----------------------- | ---------------------- |
| `ring_encoder`          | 圆环编码器阈值相关参数 |
| `pre_ring_Gyro_set`     | 进环前姿态设定参数     |
| `in_ring_Gyroz`         | 圆环内角速度相关参数   |
| `pre_out_ring_Gyro_set` | 出环前姿态设定参数     |
| `pre_out_ring_Gyroz`    | 出环前角速度相关参数   |
| `pre_out_ring_encoder`  | 出环前编码器阈值       |

### `fly`

| 字段               | 含义               |
| ------------------ | ------------------ |
| `count_fly_speed`  | 飞坡速度参数       |
| `count_fly_time_1` | 飞坡阶段计数阈值 1 |
| `count_fly_time_2` | 飞坡阶段计数阈值 2 |
| `count_fly_angle`  | 飞坡角度相关参数   |

## 存储策略说明

- 当前实现保留旧地址布局
- `eeprom_init()` 负责加载和初始化
- `eeprom_flash()` 负责回写
- 业务上统一通过 `config_save()` 和 `config_load()` 驱动

## 使用建议

- 新增参数前，先判断是否必须掉电保存
- 调试阶段的临时变量不要随意塞进 EEPROM
- 会保存的参数应优先归入 `g_app_app_config

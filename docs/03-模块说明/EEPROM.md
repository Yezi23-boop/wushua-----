# EEPROM 模块

## 模块职责

`eeprom.c` 负责项目配置参数的持久化读写。当前已经把原先分散的参数收拢成 `AppConfig g_app_config`。app_config

## 对外入口函数

- `eeprom_init()`
- `eeprom_flash()`

## 对外配置对象

- `app`

其内部按语义分成：

- `start`
- `speed`
- `angle`
- `ring`
- `fly`

## 依赖与被依赖关系

依赖：

- IAP/EEPROM 硬件接口

被依赖：

- `int_user.c`
- `menu.c`
- `vofa.c`
- `ADC.c`
- `a_run_mode.c`
- `FUYA.c`

## 当前存储策略

当前实现保留了旧地址布局，因此：

- 不需要额外迁移旧板子的历史数据
- 参数地址不变，兼容原有 EEPROM 存储习惯

初始化时会：

1. 读取 EEPROM 缓冲区
2. 判断是否已初始化
3. 未初始化则写入默认配置
4. 已初始化则按旧地址布局恢复到 `g_app_coapp_config

## 高频路径注意事项

- 高频控制路径不要直接做 EEPROM 读写
- 保存动作应集中由 `config_save()` 驱动
- 加载动作应集中由 `config_load()` 驱动

## 调参与常见风险

- 如果在线调参后重启丢失，多数是因为没有执行 `SAVE`
- 如果参数已经加载但当前控制器没变化，多数是因为没有走统一应用链
- 如果以后再扩展参数，优先保持结构清晰，其次再考虑是否扩展地址布局

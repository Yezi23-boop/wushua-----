# VOFA 模块

## 模块职责

`vofa.c` 负责通过无线串口接收 VOFA 命令、解析参数、在线调参与执行保存加载等操作。

## 对外入口函数

- `vofa_init()`
- `vofa_parse_from_fifo()`
- `vofa_get_command()`
- `handle_vofa_command()`
- `debug_vofa_service()`

## 依赖与被依赖关系

依赖：

- 无线串口 FIFO
- `g_app_config`
- `control_apply_config()`
- `config_save()`
- `config_load()`

被依赖：

- `main.c` 的可选 VOFA 服务入口

## 协议与当前实现

当前协议本质上仍是 FireWater 风格的 ASCII 命令，以 `!` 作为结束标记。  
解析流程是：

1. 串口收到字节
2. 写入无线串口 FIFO
3. `vofa_parse_from_fifo()` 从 FIFO 读出
4. `vofa_parse_byte()` 按字节累积
5. 检测到 `!` 后形成一条完整命令
6. `handle_vofa_command()` 解析并执行

## 当前常用命令

配置型命令：

- `A_KP`
- `A_KD`
- `SPEED_RUN`

运行型命令：

- `L_KP`
- `L_KI`
- `L_KD`
- `R_KP`
- `R_KI`
- `R_KD`
- `TEST_speed`
- `TEST_angle`
- `MOTOR`
- `ERR`
- `START`
- `STOP`
- `FUYA`

配置持久化命令：

- `SAVE`
- `LOAD`
- `INFO`

## 参数流转规则

当前 VOFA 已整理成两类路径：

### 会进入配置并可保存的参数

- 改 `g_app_config`
- 调 `control_apply_config()`
- `SAVE` 后进入 EEPROM

### 只改当前运行值的参数

- 直接改运行中的 PID 或测试变量
- 不进入 EEPROM
- 重启或 `LOAD` 后不会保留

## 高频路径注意事项

- VOFA 不是比赛态核心路径，通常只在调试和调参时开启
- 当前命令处理仍使用 `atof` 和较多 `printf`，调试方便，但不适合高负载常开
- 现场使用时建议只在需要时打开，避免和菜单同时重度占用主循环

## 与历史文档的关系

仓库 `project/code` 下原有两份 VOFA 文档已经整合进本知识库。  
需要注意的是，那两份文档中提到的部分接入位置已经过时，例如：

- `int_user()` 中主动调用 `vofa_init()` 的建议与当前代码不完全一致
- 某些示例命令与当前实际支持命令不完全一致

后续请以本页和 [VOFA 调参](../04-调参与策略/VOFA调参.md) 为准。

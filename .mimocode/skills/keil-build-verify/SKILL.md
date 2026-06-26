---
name: keil-build-verify
description: Keil MDK 编译验证循环 — 编译项目、解析错误日志、自动修复常见编译错误
---

# Keil 编译验证

智能车项目的 Keil MDK 编译验证流程。适用于 STC AI8051U (C251) 项目。

## 前置条件

- Keil 安装路径：`D:\Keil_v5\UV4\UV4.exe`
- 工程文件：`project/mdk/seekfree.uvproj`
- 编译日志：`project/mdk/out_file/SEEKFREE.build_log.htm`

## 流程

### 1. 执行编译

```powershell
& 'D:\Keil_v5\UV4\UV4.exe' -b 'project\mdk\seekfree.uvproj'
```

超时设为 60 秒。如果增量构建不重编修改过的文件，先删除对应的 `.obj` 文件再编译。

### 2. 读取编译日志

编译完成后读取 `project/mdk/out_file/SEEKFREE.build_log.htm`，重点关注尾部：
- `N Error(s), N Warning(s)` 汇总行
- 具体错误行格式：`../path/file.c(line): error: message`

### 3. 解析并修复常见错误

| 错误类型 | 特征 | 修复方法 |
|----------|------|----------|
| **C53 前向声明** | `error C53: can't resolve forward reference` | 将 static 函数的前向声明移到文件前部声明区（~line 70-100），放在调用点之前 |
| **C98 指针类型不匹配** | `warning C98: pointer to different objects` | 配置结构体参数类型改为 `int` 以匹配 `Menu_Process_Int_Value(int*, int)` |
| **C173 有符号/无符号比较** | `warning C173: signed/unsigned type mismatch` | 对 `int` 类型值添加显式 `(uint16)` cast |
| **未声明标识符** | `error: use of undeclared identifier` | 检查是否缺少 include、是否在 `#if 0` 块中、是否已删除但未清理引用 |
| **重复定义** | `error: redefinition` | 检查头文件 include guard 或 static 变量冲突 |

### 4. 报告结果

编译成功：报告 `0 Error(s), 0 Warning(s)` + code/data/edata 占用。
编译失败：列出每个 Error 的文件、行号、消息，给出修复建议。

## 注意事项

- `unsigned char xdata uint8` 是双重类型声明错误，`uint8` 已经是 `unsigned char` 的 typedef
- EEPROM 槽位越界不会产生编译错误，需运行时检查
- 大规模重构后死代码不会被编译器报错，需手动清理
- Keil 路径是 `D:\Keil_v5`（注意大写 K），不是 `D:\keil_5`

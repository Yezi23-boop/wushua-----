# Speed Loop Autotune

这个专题目录集中管理“上位机反复试验速度环 + 固件试验接口 + 调参规则与调试记忆”。

目录约定：
- `host/`：上位机 Python 脚本真实实现
- `firmware/`：固件侧命令处理、试验状态机、专题服务入口
- `docs/`：协议、评分、调参规则、调试记忆
- `tests/`：上位机脚本专题测试

兼容入口继续保留：
- `tools/vofa_autotune.py`
- `tests/test_vofa_autotune.py`

调试前默认先读：
- `docs/tuning_rules.md`
- `docs/debug_memory.md`

## 文档分工

- `docs/tuning_rules.md`
  - 记录当前默认调参方法
  - 包含速度序列、隔离方法、异常样本处理、调参顺序、验收标准
  - 新对话默认优先沿用这里的规则

- `docs/debug_memory.md`
  - 记录每一轮实际调试得到的结论
  - 先写已验证事实，再写推断
  - 只有重复验证过的结论，才升级回 `tuning_rules.md`

## 当前默认原则

- 端口默认 `COM8`
- 模式默认架空 `autotune`
- 负压默认关闭
- 左右轮允许使用不同 PID
- 单轮调试时，另一侧直接设为 `0/0/0`
- `miss` 当前默认先忽略，不直接当作 PID 差
- 编码器速度绝对值大于 `200` 时，默认视为异常样本

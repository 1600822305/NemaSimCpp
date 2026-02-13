# Step 115: 咽部批量填充 — I2-6 + M1/2/5 + MI

> 日期: 2026-02-13

---

## 动机

完成全部 20/20 咽部神经元。之前已有: MC(2), M3(2), M4, I1(2), RIP(2), NSM(2) = 11。
本步添加: I2(2), I3, I4, I5, I6, M1, M2(2), M5, MI = +11 → 20/20。

## 新增神经元 (+11)

| 神经元 | 类型 | NT | 说明 |
|--------|------|------|------|
| I2L/R | INTER | GLU | 接收 I1 输入 |
| I3 | INTER | GLU | I2→I3 链 |
| I4 | INTER | GLU | MI→I4→M4 |
| I5 | INTER | ACh | I5→M5 终端球 |
| I6 | INTER | ACh | 抑制性→M1 |
| M1 | MOTOR | ACh | 前峡部运动 |
| M2L/R | MOTOR | ACh | 后咽体运动 |
| M5 | MOTOR | ACh | 终端球运动 |
| MI | MOTOR | ACh | 运动/中间混合 |

## 新增连接

| 连接 | 功能 |
|------|------|
| I1→I2 | 中继链 |
| I2→I3 | 中间链 |
| M1→I3 | 运动反馈 |
| I4→M4 | 峡部运动 |
| I5→M5 | 终端球运动 |
| I6→M1 | 抑制性 |
| MC→M2 | 起搏→后咽体 |
| MI→I4 | 混合输出 |
| M2↔M2 | L/R 协调 |
| I2↔I2 | L/R 协调 |

## Diag 验证

| 神经元 | V mean | S(release) | 状态 |
|--------|--------|------------|------|
| I2L | -40 mV | 0.360 | ✅ |
| M1 | -40 mV | 0.219 | ✅ |

## 系统计数: 299 neurons, 692 syn, 242 gj
## 咽部完成: 20/20 (100%)

## 修改文件
- `src/connectome/connectome_builder.cpp`
- `src/simulation/regression_test.cpp`
- `src/simulation/diag_main.cpp`

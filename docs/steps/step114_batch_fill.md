# Step 114: 批量填充 — IL1/IL2侧 + SDQL + RME侧 + PDA/B + PVW + CAN

> 日期: 2026-02-13

---

## 动机

批量补全非咽部缺失神经元，推进 302 覆盖率。

## 新增神经元 (+13)

| 神经元 | 类型 | NT | 说明 |
|--------|------|------|------|
| IL1L/R | SENSORY | GLU | 内唇侧对，完成 6/6 |
| IL2L/R | SENSORY | ACh | 内唇2侧对，完成 6/6 |
| SDQL | SENSORY | GLU | 左体侧感觉，完成 SDQL/R 对 |
| RMEL/RMER | MOTOR | GABA | RME 侧对，完成 4/4 |
| PDA | MOTOR | ACh | 尾部背侧运动 |
| PDB | MOTOR | ACh | 尾部背侧运动 |
| PVWL/R | INTER | ACh | 后腹侧中间 |
| CANL/R | INTER | UNK | 排泄管神经元（无突触） |

## 新增连接

| 连接 | 功能 |
|------|------|
| IL1L/R→RMD | 头部退缩 |
| IL1L↔IL1R | L/R 协调 |
| IL2L/R↔RMG | hub-and-spoke |
| SDQL→RMH | 镜像 SDQR |
| RMEL↔RMED, RMER↔RMEV | 四象限协调 |
| AVA→PDA, AVB→PDB | 后退/前进→尾运动 |
| PVW→AVA | 后腹→后退 |
| PVW↔PVW | L/R 协调 |

## Diag 验证

| 神经元 | V mean | S(release) | 状态 |
|--------|--------|------------|------|
| IL1L | -40 mV | 0.122 | ✅ |
| SDQL | -40 mV | 0.123 | ✅ |
| RMEL | -40 mV | 0.178 | ✅ |
| PDA | -40 mV | 0.268 | ✅ |
| PVWL | -40 mV | 0.288 | ✅ |

## 系统计数: 288 neurons, 681 syn, 240 gj

## 修改文件
- `src/connectome/connectome_builder.cpp`
- `src/simulation/regression_test.cpp`
- `src/simulation/diag_main.cpp`

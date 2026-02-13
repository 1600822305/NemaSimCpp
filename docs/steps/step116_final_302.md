# Step 116: 302/302 — RIG→RIGL/RIGR + RMDL/RMDR

> 日期: 2026-02-13

---

## 动机

补齐最后 3 个缺失神经元，达到完整 302/302 雌雄同体神经元集合。

### 问题发现
- **RIG**: 之前注册为单个无对神经元，但 Emmons 2024 明确标注 "RIG(L/R)"，为双侧对
- **RMD**: White 1986 原文有 6 个 RMD 神经元 (RMDDL/RMDDR/RMDL/RMDR/RMDVL/RMDVR)，我们只有 4 个（D/V 象限），缺 RMDL/RMDR 侧对

### 修复
- RIG (1 个) → RIGL + RIGR (2 个)：净增 +1
- 新增 RMDL + RMDR：净增 +2
- **总计 +3 → 299→302**

## 新增连接

| 连接 | 功能 |
|------|------|
| RIGL↔RIGR | 双侧协调 (gap junction w=2) |
| RMDL↔RMDDL, RMDR↔RMDDR | 侧-背协调 |
| RMDL↔RMDVL, RMDR↔RMDVR | 侧-腹协调 |
| IL1L→RMDL, IL1R→RMDR | 鼻触→侧头退缩 |
| 所有原 RIG 连接改为 RIGL/RIGR 双侧 | DVC/PVT/AVH→RIG→AIY/AIZ/RIA/AVK |

## Diag 验证

| 神经元 | V mean | S(release) | 状态 |
|--------|--------|------------|------|
| RIGL | -40 mV | 0.405 | ✅ (DVC+PVT+AVH 输入) |
| RMDL | -60 mV | 0.014 | ✅ (IL1 输入 + gj) |

## 最终系统计数
| 指标 | 值 |
|------|------|
| **神经元** | **302/302 (100%)** |
| 化学突触 | 697 |
| 间隙连接 | 247 |
| regtest | 20/20 pass |

## 修改文件
- `src/connectome/connectome_builder.cpp`: RIG→RIGL/RIGR + RMDL/RMDR
- `src/simulation/regression_test.cpp`: 基线 302/697/247
- `src/simulation/diag_main.cpp`: RIGL/RMDL 跟踪

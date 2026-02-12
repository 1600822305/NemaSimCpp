# Step 100: 系统状态刷新 + FLP-11/DOP-3 覆盖修复

> 日期: 2026-02-13

---

## 动机

progress.md「当前系统状态」代码块在 Step 84-99 大规模扩展后严重过时，多处数据与代码不符。文档审查还发现了 2 个代码级 bug。

## 代码修复

### Bug 1: FLP-11 DMSR-1 body_mn 覆盖不完整 + 错误靶向

**问题**: `setup_neuromodulation.cpp` 的 body_mn[] 数组：
- 缺失 Step 84-87 扩展的胆碱能 MN: DA06-09, VA06-12, VB08-11
- 错误包含 GABAergic MN: DD01-05, VD01-05

**生物学依据**: Rossi 2025 Current Biology — *"dmsr-1 induces sleep by acting in cholinergic neurons"*
- DMSR-1 是 Gi/o 偶联 GPCR，抑制胆碱能信号传递
- DD/VD 是 GABAergic（非胆碱能），不表达 DMSR-1
- DD/VD 静默通过间接效应实现：ACh 驱动↓ → LGC-46 输入↓ → GABA 输出↓

**修复**: 
- 添加: DA06-09(4), VA06-12(7), VB08-11(4) = +15 个胆碱能靶点
- 移除: DD01-05(5), VD01-05(5) = -10 个 GABAergic 靶点
- 净变化: body_mn 34→39 个靶点

### Bug 2: DOP-3 B-class VB08-11 缺失

**问题**: `setup_neuromodulation.cpp` 的 b_class_names[] 只有 DB01-07 + VB01-07，遗漏 Step 87 扩展的 VB08-11。

**生物学依据**: Chase 2004 Nature Neuroscience — *"expression of DOP-3 in ventral cord cholinergic motor neurons using the acr-2 promoter was sufficient"*
- acr-2 启动子驱动所有 VNC 胆碱能 MN 的表达
- DOP-3 (D2-like, Gαo) 减少 ACh 释放 → 肌肉收缩↓ → 减速

**修复**: b_class_names[] 添加 VB08-11 (14→18 个 B-class MN)

### 注释修复

- 文件头 "6 neuromodulators" → "7 neuromodulators" (FLP-11 在 Step 71 加入)

## 文档刷新

progress.md「当前系统状态」全面更新：

| 条目 | 旧值 | 新值 |
|------|------|------|
| 感觉神经元 | 61 | **63** (CEP 4×非2×) |
| 运动神经元 | 98 | **96** (精确重计) |
| 突触 | ~215+~56 | **513+185** |
| 运动映射 | 29 | **75** |
| DA 靶标 | 9 | **27** (+18 DOP-3 B-class) |
| SER-4 speed | -0.40 | **-0.60** |
| 文件数 | 44 | **62** |
| FLP-11 靶标 | 未列出 | **完整列出** |
| B-class basal slowing | 14 | **18** |

## 修改文件

- `src/simulation/setup_neuromodulation.cpp`: Bug 1 + Bug 2 + 注释
- `docs/progress.md`: 当前系统状态代码块全面刷新

## 参考文献

- Rossi 2025 Current Biology — FLP-11/DMSR-1 dual role in sleep
- Chase 2004 Nature Neuroscience — DOP-3 extrasynaptic on cholinergic motor neurons
- Sawin 2000 Neuron — Basal slowing response

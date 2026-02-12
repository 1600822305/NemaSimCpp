# Step 96: 社交/独居进食行为 — NPR-1/RMG Hub 通路

**Date**: 2025-02-13
**Status**: Complete

## 动机

C. elegans 的社交进食行为是经典的行为多态性：
- **N2 (实验室株)**: npr-1(215V) 功能获得 → RMG 抑制 → 独居进食
- **Hawaiian CB4856**: npr-1(lf) → RMG 活跃 → 社交聚集

RMG hub 神经元已在 Step 75 添加，但缺少：
1. hub-and-spoke gap junction 网络（URX/ASK/ADL/ASH↔RMG）
2. NPR-1 对 RMG 的直接抑制
3. NPR-1 add_synaptic_current 被 I_syn_ reset 清零的 bug

## 生物学基础

- **Macosko 2009 Nature**: NPR-1 cell-autonomously inhibits RMG hub
- **de Bono 2002 Nature**: npr-1(215V) = solitary, npr-1(lf) = social
- **Rogers 2003**: FLP-21 is endogenous NPR-1 ligand
- **Busch 2012, Fenk & de Bono 2015**: URX↔RMG for O₂-dependent aggregation
- **机制**: RMG 是 hub-and-spoke gap junction 网络的中心
  - URX(O₂) + ASK(pheromone) + ADL(nociception) + ASH(polymodal) + AWB(pathogen)
  - NPR-1 抑制 RMG → 切断 hub → 感觉信号不被放大 → 独居
  - npr-1(lf) → RMG 活跃 → hub 放大信号 → 聚集行为

## 实现细节

### 修改 1: RMG hub-and-spoke gap junctions (connectome_builder.cpp)
- URX↔RMG: 2 sections (O₂→聚集)
- ASK↔RMG: 1 section (pheromone→吸引)
- ADL↔RMG: 1 section (伤害→回避)
- ASH↔RMG: 1 section (多模态→放大)
- 共 8 个新 gap junctions

### 修改 2: NPR-1 对 RMG 直接抑制 (simulation_engine.h/cpp)
- npr1_rmg_ = -20 pA (N2 默认)
- CLI: --npr1 0 模拟 Hawaiian 株

### 修改 3: Bug fix — NPR-1 电流位置 (simulation_engine.cpp)
- **根因**: `add_synaptic_current` 在 `apply_sensory_systems()` 中调用
  → 在 `compute_synaptic_currents()` 的 `reset_synaptic_current()` **之前**
  → NPR-1 电流被清零，完全无效！
- **修复**: 将 AUA 和 RMG 的 NPR-1 抑制移到 step() 中 I_syn_ reset 之后
- 同时修复了 AUA 上已存在的 NPR-1 抑制（也有同样的 bug）

### 修改 4: diag 社交行为检测 (diag_main.cpp)
- Section 34: 报告 NPR-1 设置、RMG 电压/释放概率
- Solitary: RMG S < 0.05, Social: RMG S > 0.15

## 验证结果

| 菌株 | NPR-1 | RMG V | RMG S(release) | 表型 |
|------|-------|-------|----------------|------|
| N2 | -20 pA | -61 mV | 0.005 | ✅ Solitary |
| Hawaiian | 0 pA | -40 mV | 0.25 | ✅ Social |

- RMG 活性差异: **56x** (0.005 vs 0.28)
- reversal rate: N2=0.15/s, Hawaiian=0.17/s (+13%)
- regtest: 20/20 PASS
- Gap junctions: 171 → 179 (+8)

## 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `src/connectome/connectome_builder.cpp` | RMG hub gap junctions (+8) |
| `src/simulation/simulation_engine.h` | npr1_rmg_ 参数 + set/get 方法 |
| `src/simulation/simulation_engine.cpp` | NPR-1 移到 reset 后 (bug fix) |
| `src/simulation/apply_sensory_systems.cpp` | 移除旧 NPR-1 位置 |
| `src/simulation/diag_main.cpp` | 社交行为检测 + --npr1 CLI |
| `src/simulation/regression_test.cpp` | gap junction 基线 171→179 |

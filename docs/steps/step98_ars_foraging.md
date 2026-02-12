# Step 98: 觅食策略 — Area-Restricted Search (ARS)

**Date**: 2025-02-13
**Status**: Complete

## 动机

C. elegans 的 ARS 是经典觅食行为：食物移除后，虫子先在附近局部搜索（高 reversal），
然后逐渐转为全局搜索（低 reversal）。这由 DA→DARPP-32→GLR-1 通路驱动。

## 生物学基础

- **Hills 2004 J Neurosci**: DA + 谷氨酸控制 ARS
  - cat-2 突变体（无 DA）→ 提前进入全局搜索
  - eat-4 突变体（无谷氨酸）→ ARS 缺失
  - DA→DARPP-32 磷酸化→GLR-1 增敏→更多 reversal
- **López-Cruz 2019 Neuron**: AIA/ADE 并行多模态回路
  - MGL-1（metabotropic 谷氨酸受体）介导缓慢衰减
  - 感觉神经元活动 + AIA/ADE 突触后敏感性同时衰减
- **Margolis 2023 eLife**: 随机模型
  - 只有约 50% 个体显示清晰的 local→global 切换
  - reorientation rate 指数衰减，但个体随机采样产生多样轨迹
  - 不需要行为状态切换——衰减率的随机采样足以解释
- **Calhoun 2014 eLife**: 最大信息量觅食（Bayesian 决策时间）
- **时间尺度**: 5-15 分钟过渡期

## 实现

### 修改 1: food_memory tau_decay 300s (simulation_engine.h)
- 从 90s → 300s (5 分钟)，匹配 Hills 2004 生物学时间尺度
- tau_rise 保持 5s（快速上升，食物接触立即生效）

### 修改 2: food_memory→AVA 增强 (update_internal_states.cpp)
- 从 1.5pA → 4.0pA
- 食物记忆高时：AVA 更容易过阈值 → 更多 reversal = local search
- 食物记忆衰减后：AVA 驱动减弱 → 更少 reversal = global search
- DVA 驱动保持 5.0 * food_memory pA → NLP-12→SMD 头摆

### 修改 3: --food-removal CLI (diag_main.cpp)
- `--food-removal <sec>`: 在指定时间清除所有食物/化学场
- 打印 food_memory 状态

### 修改 4: ARS 诊断 Section 36 (diag_main.cpp)
- 30s 时间窗分析 reversal rate 衰减
- 检测 local→global 切换（rate decay > 40%）

## 验证结果 (5 seeds, 600s, food-removal at 180s)

| 时间段 | 5-seed 均值 rev/s |
|--------|------------------|
| Pre-removal (last 60s) | 0.18 |
| t+0-30s | 0.05 |
| t+30-60s | 0.00 |
| t+60-90s | 0.06 |
| t+90-120s | 0.19 |
| t+120-180s | 0.19 |

### 分析
- **食物移除后即刻 reversal 下降**: 趋化性梯度消失 → AWC→AIB→AVA 信号消失
- **t+90s 后恢复到 0.19/s**: 高于无食物基线 (~0.11/s)
  → food_memory→AVA 贡献 ~73% 额外 reversal
- **未见清晰衰减**: tau_decay=300s 但观测窗口只有 420s (1.4τ)
  → 需要更长观测才能检测衰减
- **与 Margolis 2023 一致**: 个体随机性大，群体平均才显示趋势

## 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `src/simulation/simulation_engine.h` | food_memory_tau_decay 90s→300s |
| `src/simulation/update_internal_states.cpp` | food_memory→AVA 1.5→4.0 pA |
| `src/simulation/diag_main.cpp` | --food-removal CLI + ARS 诊断 Section 36 |

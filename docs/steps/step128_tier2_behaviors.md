# Step 128: Tier 2 Behaviors — 6 Sensory/Learning Enhancements

## 概述
代码审计 → 文献调研 → 实现 6 个 Tier 2 缺失行为：
- 3 个新感觉通道 (PVM, FLP 热, ADL 信息素)
- 1 个感觉可塑性 (长期嗅觉适应)
- 1 个新应激回路 (ALA 应激静止)
- 1 个新学习规则 (嗅觉联想条件化)

## 实现清单

### 1. PVM 后体轻触激活 (★☆)
- **问题**: PVM 连接组已有 (Step 106: PVM→PVC, PVM→AVA) 但无感觉驱动
- **修复**: `apply_touch_stimulus()` 中 `rear_touch` 分支加 PVM=40pA (PLM 的 50%)
- REF: Chalfie 1985, Way & Chalfie 1989

### 2. FLP 热伤害感受 (★★)
- **问题**: FLP 仅有机械感觉 (鼻触+粗触), 缺热伤害通道
- **修复**: T > 33°C → TRPA-1 激活 → FLP 获热伤害电流 (max 40pA)
- 多模态整合: `max(mechanical, thermal)` — 共用 TRPA-1 通道
- REF: Chatzigeorgiou & Schafer 2010 Neuron, Liu 2012 Nature

### 3. ADL 信息素场采样 (★★)
- **问题**: ADL 有化学斥力响应 (chemo_mappings ON) 但不采样信息素场
- **修复**: ADL 检测 `pheromone_field` → ascaroside 回避 (max 20pA)
- 多模态: `max(repellent_response, pheromone_drive)` — ADL 是多模态伤害感受器
- REF: Jang 2012, Troemel 1997, Macosko 2009

### 4. 长期嗅觉适应 (★★)
- **问题**: ChemoTransducer 仅有 5s 适应 (Weber-Fechner), 缺分钟级适应
- **修复**: 加第三个超慢滤波器 `lta_exposure_` (tau=300s for AWC, 600s for AWA)
- 效果: 持续暴露 >5min → gain 降低最多 70% (AWC) / 50% (AWA)
- 机制: EGL-4/PKG 核定位 → 基因表达改变 → 感觉疲劳
- ASE (味觉) 不受影响 — 仅嗅觉通路
- REF: Colbert & Bargmann 1995, L'Etoile 2002 Neuron

### 5. ALA 应激诱导静止 (★★)
- **问题**: ALA 神经元已注册 (Step 106) + 连接组已有 (FLP→ALA, ALA⊣AVA, ALA↔RIS)
  但无感觉传导代码
- **修复**: 应激检测 → ALA 钙平台电位 (tau ~30s 慢速斜坡)
  - 应激源: (1) 温度 >33°C (2) ASH 强激活 (化学伤害代理)
  - ALA plateau → ALA⊣AVA 抑制反转, ALA→RID 背侧调制, ALA↔RIS 协调
- 与 RIS 睡眠不同: RIS=内稳态疲劳; ALA=急性应激保护
- REF: Van Buskirk & Bhatt 2007, Hill 2014 Curr Biol

### 6. 嗅觉联想条件化 (★★★)
- **问题**: 已有盐学习 (ASER) 和病原体学习 (AWC+sickness), 缺经典丁酮适应
- **修复**: `update_olfactory_conditioning()` — 新学习规则
  - 学习信号: `satiety - 0.3` (饥饿时为负, 进食时为正)
  - AWC 活跃 + 饥饿 → AWC→AIY w_mod↓ + AWC→AIB w_mod↑ (减少趋化)
  - AWC 活跃 + 进食 → AWC→AIY w_mod↑ + AWC→AIB w_mod↓ (增强趋化)
  - 比病原体学习慢 (lr=0.0005 vs 0.003): 条件化需要更长时间
  - 不需要 sickness: 仅需 "气味+饥饿" 配对
- REF: Colbert & Bargmann 1995, Cho 2016 Cell Rep, Torayama 2007

## 修改文件

| 文件 | 修改内容 |
|------|----------|
| `src/simulation/apply_sensory_systems.cpp` | PVM 激活, FLP 热伤害, ADL 信息素, ALA 应激 |
| `src/environment/sensory_transducer.h` | ChemoTransducer 加 LTA (长期适应) |
| `src/simulation/simulation_engine.cpp` | AWC/AWA 启用 LTA |
| `src/simulation/simulation_engine.h` | `ala_stress_`, `update_olfactory_conditioning()` 声明 |
| `src/simulation/update_learning.cpp` | `update_olfactory_conditioning()` 实现 |

## 验证
- 全量构建通过 (0 errors)
- touch_analyzer 4/4 通过
- multisensory_analyzer 6/6 场景无回归

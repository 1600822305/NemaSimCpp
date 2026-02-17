# Step 127: Osmotic Avoidance + CO₂ Avoidance + Tier 1 Behavior Audit

## 概述
文献调研 6 个教科书级缺失行为 → 发现其中 4 个已在之前的 Step 中实现。
新增两个环境感觉通道：
1. **渗透压回避** — ASH 多模态伤害感受 (OSM-9/TRPV) 检测高渗溶液
2. **CO₂ 回避** — BAG 通过独立 CO₂ 化学场检测环境 CO₂

## Tier 1 行为审计结果

| # | 行为 | 状态 | 实现位置 |
|---|------|------|----------|
| 1 | 渗透压回避 | ✅ **Step 127 新增** | environment + apply_sensory_systems |
| 2 | CO₂ 回避 | ✅ **Step 127 新增** | environment + apply_sensory_systems |
| 3 | 鼻触回避 | ✅ 已有 Step 33+73 | OLQ/IL1/FLP/RIH hub-and-spoke |
| 4 | PVD 粗触 | ✅ 已有 Step 36 | 壁碰 + 本体感觉双模态 |
| 5 | DMP 排泄 | ✅ 已有 Step 56+71 | 45s Ca²⁺振荡器 + 3阶段 AVL/DVB |
| 6 | 产卵程序 | ✅ 已有 Step 38 | egg_pressure → HSN → VC → 产卵 |

## 渗透压回避 (Osmotic Avoidance)

### 生物学基础
- **急性模式**: ASH 通过 OSM-9/TRPV 通道检测极高渗透压 (≥1 Osm) → 立即反转
- **渐进模式**: AQR/PQR/URX 通过 TAX-2 cGMP 通道检测温和高渗 → 逐渐增加转向率
- REF: Colbert 1997, Hilliard 2004, Kunitomo 2017 eNeuro

### 实现
- **环境层**: `Environment::sample_osmolarity()` — 圆形高渗区域，sigmoid 边界 (过渡宽度 ~1mm)
- **感觉层**: ASH 多模态整合 — `noci_input = max(repellent, osmolarity)` (共用 OSM-9 通道)
- **下游**: 复用已有 ASH → AIB → AVA → 反转回路

### 关键设计决策
- **max 而非 sum**: ASH 的 repellent 和 osmolarity 通过同一 OSM-9/TRPV 通道，
  不会叠加，取最强刺激
- **sigmoid 边界**: `1/(1+exp(-(r-R)*4))` 模拟真实的 ~1mm 体径空间分辨率
- **环形实验**: 经典 Colbert 1997 实验范式 — 以虫起点为中心的高渗环

### 验证结果 (300s, seed 42)
```
  baseline     CI=0.028  均距=19.3mm
  food+osmo    CI=0.022  均距=19.4mm  ← CI 下降 (渗透压阻碍趋化)
```

## CO₂ 回避 (CO₂ Avoidance)

### 生物学基础
- **BAG**: 通过 GCY-9 受体鸟苷酸环化酶 + TAX-2/TAX-4 cGMP 通道检测 CO₂
- **NPR-1 调制**: N2 品系 NPR-1 抑制 URX → 允许 CO₂ 回避
- **双源 CO₂**: (1) 食物来源 (细菌代谢) (2) 独立环境源 (腐烂水果、捕食者)
- REF: Hallem & Sternberg 2008, Bretscher 2008, Carrillo 2013 J Neurosci

### 实现
- **环境层**: `Environment::co2_field()` — 独立 CO₂ 化学场 (σ=15mm, 气体扩散快)
- **感觉层**: BAG 采样 `max(food_derived_co2, external_co2)` — 多源整合
- **下游**: 复用已有 BAG Step 35 回路 (tonic + phasic + URX 交叉抑制)

### 关键设计决策
- **max 而非 sum**: 两种 CO₂ 来源经同一 GCY-9 受体，取更高浓度
- **标度**: 外部 CO₂ strength 1.0 → 5% (远超 0.5% 阈值)
- **广扩散**: σ=15mm (气体扩散比液态化学物快得多)

## 修改文件

| 文件 | 修改内容 |
|------|----------|
| `src/environment/environment.h` | 加 `sample_osmolarity()`, `set_osmotic_region()`, `sample_co2()`, `set_co2_source()`, `co2_field()` |
| `src/environment/environment.cpp` | 实现渗透压场 (sigmoid)、CO₂ 场 (Gaussian σ²=225) |
| `src/simulation/apply_sensory_systems.cpp` | ASH 多模态: `max(repellent, osmolarity)`; BAG: `max(food_co2, ext_co2)` |
| `src/simulation/simulation_engine.h` | `reset_transducers()` 加渗透压采样 |
| `src/diagnostics/multisensory_analyzer_main.cpp` | 加场景 E (渗透压环形实验) 和 F (CO₂+食物冲突) |
| `docs/research_tier1_behaviors.md` | 6 个缺失行为的文献调研汇总 |

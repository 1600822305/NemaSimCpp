# Step 25: 化学回避 + ASH 伤害感觉

## 概述

实现 ASH 伤害感觉神经元驱动的化学回避回路。线虫的导航是**引诱+排斥竞争**的结果，此前只有引诱（ASE/AWC/AWA→食物），缺少排斥。

## 生物学基础

### ASH 是线虫最强的多模态伤害感觉神经元

| 模态 | 响应 | 受体 |
|------|------|------|
| 高渗透压 | 回避 | OSM-9 (TRPV) |
| 苦味化学物 (1-octanol) | 回避 | 谷氨酸释放 |
| 鼻触 | 后退 | MEC 通道 |
| 强光 (UV) | 回避 | lite-1 |

### 核心回路: AIB 整合枢纽 (Summers et al. 2015 J Neurosci)

```
ASH (谷氨酸) → AIB 双受体:
  GLR-1 (AMPA类, 阳离子): 兴奋性 → 在突起上
  AVR-14 (GluCl, 氯离子): 抑制性 → 在胞体上

Off food: ASH↑ → AIB↑ → RIM → omega → 逃跑
On food:  ASH↑ → AIB被5-HT抑制(MOD-1 Cl⁻) → 继续前进
```

**关键洞察**: 线虫在食物上时会**忍受危险继续觅食**（Summers 2015）。5-HT通过MOD-1氯离子通道抑制AIB，使回避行为在进食时被压制。

### 连接组数据 (Cook 2019, OpenWorm Connectome Toolbox)

ASHL 化学突触输出确认:
- ASHL → AVAL (2 sections) — 已有
- ASHL → AVDL (3 sections) — 已有
- **ASHL → AIBL (3 sections)** — 新增
- **ASHL → RIML (1 section)** — 新增
- ASHL → AVBL, RIAL, AIAL 等 — 已有或次要

## 实现

### 1. 排斥化学场 (Environment)

- `Environment` 添加独立的 `repellent_field_` (ChemicalField)
- 与引诱物场完全独立：自己的高斯源、梯度、扩散
- `sample_repellent(pos)` / `repellent_field()` 访问器
- σ² = 144mm² (与引诱物相同扩散模型)

### 2. ASH 感觉转导

- ASH 从 `chemo_mappings_` 移到 `noci_mappings_`
- ON 型：排斥物浓度增加→兴奋
- gain=60, baseline=3pA (与之前相同参数)
- **不受饱食调制**: 伤害感觉本身不被 satiety 抑制
  - (5-HT 通过下游 AIB 抑制来实现行为调制 — Summers 2015)

### 3. 新增突触 (Cook 2019 确认)

| 连接 | Sections | 类型 | 功能 |
|------|----------|------|------|
| ASH→AIB (L/R) | 3 | 兴奋性 (GLR-1) | 核心回避决策 |
| ASH→RIM (L/R) | 1 | 兴奋性 | 促进 omega 转弯 |

总计新增: 4 个化学突触

### 4. 5-HT → AIB 抑制 (MOD-1)

- MOD-1: 5-HT 门控 Cl⁻ 通道
- -6 pA 抑制性 (与 AIY 上的 MOD-1 -5pA 类似)
- 在食物上: 5-HT↑ → AIB↓ → 回避行为被压制
- 离食物: 5-HT↓ → AIB 正常 → 完整回避响应

### 5. STP 参数

ASH→AIB/RIM 纳入 touch circuit STP:
- tau_rec=4000ms, alpha_d=0.0005, tau_f=300ms
- 持续刺激时习惯化 (n_ss ≈ 0.38)

## 信号链

```
排斥物 → ASH (ON型) → 三条平行通路:

1. ASH → AVA (2 sections) → 直接后退 (快速逃跑)
2. ASH → AIB (3 sections) → AVA → 后退 + omega (决策路径)
3. ASH → RIM (1 section) → omega 转弯 (方向重置)

调制:
  5-HT (MOD-1) ⊣ AIB: 在食物上时抑制路径2
  AIA ⊣ AIB: 引诱物信号也抑制路径2
  → AIB 整合: 引诱 vs 排斥 竞争决策
```

## 预期涌现行为

| 场景 | 行为 |
|------|------|
| 无排斥物 | 正常趋化 (ASH 静默, CI≈0.75) |
| 排斥物挡在食物前 | 接近→ASH激活→后退→omega→绕路 (CI↓) |
| 在食物上遇排斥物 | 5-HT抑制AIB→忍受危险继续觅食 |
| 排斥物包围食物 | 多次尝试→找薄弱点突破→或放弃 |

## 验证结果

### Regtest (无排斥源): 12 pass, 0 FAIL
- 新增突触和 ASH 分离未破坏现有系统

### Diag (排斥源 at 30,30, 食物 at 35,35):
- CI: 0.75 → 0.437 (排斥有效阻碍导航)
- reversals: 15 (↑), omega: 10 (↑)
- time_near_food: 41.8% (仍能找到食物)
- ASHL I_ext: 5.6 pA (检测到排斥物)

## 修改文件

| 文件 | 修改 |
|------|------|
| `environment.h` | 添加 `repellent_field_`, `sample_repellent()` |
| `environment.cpp` | 排斥场初始化/step/sample |
| `simulation_engine.h` | `noci_mappings_`, `aib_ids_`, 非const `environment()` |
| `simulation_engine.cpp` | ASH→noci, AIB ID收集, 排斥场采样, 5-HT→AIB MOD-1, STP |
| `connectome_loader.cpp` | ASH→AIB(3), ASH→RIM(1) |
| `diag_main.cpp` | 排斥源设置 + nociception诊断输出 |

## 参考文献

- Summers et al. 2015 J Neurosci 35:10331 — AIB多感觉整合
- Cook et al. 2019 Nature 571:63 — 全动物连接组
- Bargmann & Kaplan 1998 — ASH多模态伤害感觉
- Harris et al. 2009, 2011 — 5-HT/食物调制回避行为

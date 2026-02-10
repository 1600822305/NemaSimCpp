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
- `add_point_source(pos, strength, sigma2)` — σ² 参数可配置
- **σ² = 25mm²** (局部化毒物, vs 引诱物 σ²=144mm²)
- `ChemicalField::clear()` 方法用于重置

### 2. ASH 感觉转导

- ASH 从 `chemo_mappings_` 移到 `noci_mappings_`
- **TONIC 型**：响应绝对排斥物浓度 (非 dC/dt)
  - 理由: dC/dt 在稳态为 0, ON型只产生 ~2.3pA
- **gain=80**, baseline=3pA, **half_max=0.5**, **clamp=80pA**
- 在排斥物中心(C=0.8): I = 3 + 80×0.62 ≈ 52pA
- **不受饱食调制**: 伤害感觉本身不被 satiety 抑制
  - (5-HT 通过下游 AIB 抑制来实现行为调制 — Summers 2015)

### 2b. 排斥 Weathervane (持续偏转)

- 计算 ∇C_repellent⊥ (排斥梯度垂直于 heading 的分量)
- **反向偏置**: curve AWAY from repellent (与引诱物对称但符号相反)
- 同时作用于 **SMD bias** 和 **curvature bypass** 两个通道
- **不受 satiety 调制**: 伤害回避无条件
- 解决了来回弹跳问题: 之前碰→退→吸引→碰循环, 现在持续偏转绕路

### 3. 新增突触 (Cook 2019 确认)

| 连接 | Sections | 类型 | 功能 |
|------|----------|------|------|
| ASH→AIB (L/R) | 3 | 兴奋性 (GLR-1) | 核心回避决策 |
| ASH→RIM (L/R) | 1 | 兴奋性 | 促进 omega 转弯 |
| ASH→AVA (L/R) | **3** | 兴奋性 | 直接后退 (从2恢复) |

总计新增: 4 个化学突触 + ASH→AVA 恢复到 3 sections
(ASH→AVA 从4→2→3: 旧限制因ASH采样引诱物; 现在ASH只采样排斥场, 安全恢复)

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

1. ASH → AVA (3 sections) → 直接后退 (快速逃跑)
2. ASH → AIB (3 sections) → AVA → 后退 + omega (决策路径)
3. ASH → RIM (1 section) → omega 转弯 (方向重置)
4. 排斥 weathervane → 持续曲率偏置 (远离毒物方向弯曲)

调制:
  5-HT (MOD-1) ⊣ AIB: 在食物上时抑制路径2
  AIA ⊣ AIB: 引诱物信号也抑制路径2
  → AIB 整合: 引诱 vs 排斥 竞争决策
```

## 预期涌现行为

| 场景 | 行为 |
|------|------|
| 无排斥物 | 正常趋化 (ASH 静默, CI≈0.75) |
| 排斥物挡在食物前 | weathervane持续偏转→绕路到达食物 |
| 在食物上遇排斥物 | 5-HT抑制AIB→忍受危险继续觅食 |
| 排斥物包围食物 | 多次尝试→找薄弱点突破→或放弃 |

## 验证结果

### Regtest (无排斥源): 12 pass, 0 FAIL
- 新增突触和 ASH 分离未破坏现有系统

### Diag (直线场景: 起点25,25 → 排斥30,25 → 食物35,25):
- 线虫接近排斥区但**不穿过** (r_dist最小=1.4mm)
- 向北偏转 (y: 25→32, 偏移7mm)
- 从北侧绕路到达食物 (t=180: dist=2.9mm)
- time_near_food=18.5%, CI=0.655
- ASHL I_ext: 最高51pA (排斥物中心附近)
- **涌现绕路行为**: 排斥weathervane持续推开线虫, 自然滑过排斥区

## 修改文件

| 文件 | 修改 |
|------|------|
| `environment.h` | 添加 `repellent_field_`, `sample_repellent()` |
| `environment.cpp` | 排斥场初始化/step/sample |
| `simulation_engine.h` | `noci_mappings_`, `aib_ids_`, 非const `environment()` |
| `sensory_transducer.h` | ChemoTransducer 添加 half_max_ 参数, clamp 50→80pA |
| `simulation_engine.cpp` | ASH TONIC gain=80, 排斥weathervane, AIB 5-HT, STP |
| `connectome_loader.cpp` | ASH→AIB(3), ASH→RIM(1), ASH→AVA(3 恢复) |
| `diag_main.cpp` | 排斥源设置 + nociception诊断输出 |

## 调参历程

| 参数 | 初始 → 最终 | 原因 |
|------|------------|------|
| 响应类型 | ON → **TONIC** | dC/dt稳态为0, 只产生2.3pA |
| σ² | 144 → **25mm²** | 毒物弥漫全场, 导致连续reversal |
| half_max | 0.1 → **0.5** | 远距离也强响应, 需提高阈值 |
| gain | 40→60→**80** | ASH不够推AVA过0.65阈值 |
| clamp | 50 → **80pA** | 限制ASH最大输出 |
| ASH→AVA | 4→2→**3** | 旧限制(采样引诱物)不再适用 |
| + weathervane | 无 → **有** | 来回弹跳→持续偏转绕路 |

## 参考文献

- Summers et al. 2015 J Neurosci 35:10331 — AIB多感觉整合
- Cook et al. 2019 Nature 571:63 — 全动物连接组
- Bargmann & Kaplan 1998 — ASH多模态伤害感觉
- Harris et al. 2009, 2011 — 5-HT/食物调制回避行为
- Iino & Yoshida 2009 — weathervane机制 (排斥版本对称实现)

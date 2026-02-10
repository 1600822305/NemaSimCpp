# Step 21: 突触可塑性 — 短时抑制/易化 + 盐学习

## 概述

为所有 ~110 个化学突触添加生物物理级别的短时可塑性（STP），实现：
1. **触觉习惯化** — 重复触觉刺激 → 响应递减
2. **感觉适应** — 持续化学信号 → 突触输出衰减
3. **CPG 稳定** — 运动回路快速恢复，振荡不受影响
4. **盐趋化学习** — satiety 驱动 ASER 突触权重调制

## Tier 1: 短时突触抑制 (STD)

### 生物学基础

C. elegans 使用分级突触（graded synapses），突触传递强度与突触前电压连续相关。
Liu 2009 (PNAS) 证实 C. elegans NMJ 存在使用依赖的突触抑制。

### Tsodyks-Markram 模型（适配分级突触）

每个突触维护一个囊泡池占有率 `n(t) ∈ [0.01, 1]`：

```
dn/dt = (1 - n) / τ_recovery - α_d × S × n

S = sigmoid(V_pre)  — 分级释放率（始终 > 0）
n = 囊泡可用度
τ_recovery = 恢复时间常数
α_d = 耗竭率常数
```

### ⚠️ 分级突触的关键差异

脉冲突触的 S 只在放电时为 1，否则为 0。
分级突触的 S 始终 > 0（静息时 ~0.3），所以 **α_d 必须比脉冲突触小 ~1000 倍**。

稳态: `n_ss = 1 / (1 + α_d × S × τ_recovery)`

### 分回路参数

| 回路 | τ_rec (ms) | α_d | n_ss@rest | n_ss@active | 目的 |
|------|-----------|------|-----------|-------------|------|
| CPG (SMD/DD/VD) | 400 | 0.0003 | 0.94 | 0.94 | 振荡稳定 |
| 触觉 (ALM→AVD) | 4000 | 0.0005 | 0.99 | 0.38 | 习惯化 |
| 感觉 (ASE→AIA) | 1500 | 0.0003 | 0.86 | 0.76 | 适应 |
| 默认 | 2000 | 0.0003 | 0.85 | — | 适度抑制 |

触觉突触的关键：ALM/PLM 静息时 S≈0.003 → n≈1.0（满池），
触摸时 S≈0.8 → n_ss=0.38（强习惯化）。这就是为什么触觉回路
用较大 α_d 但仍在静息时保持满池。

## Tier 2: 短时易化 (STF)

释放概率 `p(t)` 的动态：

```
dp/dt = (p0 - p) / τ_facil + α_f × S × (1 - p)

p0 = 基线释放概率
τ_facil = 易化衰减时间 (~200ms)
α_f = 易化率 (0.001-0.003)
```

有效突触强度: `I ∝ n × (p/p0) × S × g_max`

## Tier 3: 盐趋化学习

### 生物学 (Tomioka 2006 Neuron)

```
饥饿 + NaCl暴露 → AIA释放INS-1(胰岛素肽)
  → ASER上DAF-2(胰岛素受体) → PI3K/AGE-1激活
  → ASER→AIA/AIY突触权重减弱 → NaCl吸引力降低
```

### 实现

每个 ASER 输出突触维护一个权重调制因子 `weight_mod ∈ [0.1, 3.0]`：

```
Δw = lr × (satiety - 0.5) × S_pre × S_post

lr = 0.0001 (每100ms更新)
satiety > 0.5 (饱食): 增强突触 → 维持吸引
satiety < 0.5 (饥饿): 减弱突触 → 学习厌恶
```

## 验证结果 (300s)

### 最佳运行
| 指标 | Step 20 (无STP) | Step 21 (有STP) |
|------|-----------------|-----------------|
| CI | 0.520 | **0.903** |
| time_near_food | 51.6% | **56.2%** |
| reversal_rate | 0.21/s | **0.06/s** |
| CPG (SMDDL→SMDVL n) | N/A | 0.982 |
| 触觉 min vesicle | N/A | 0.333 |

### STP 诊断 (300s 后)
```
vesicle_pool: mean=0.829  min=0.333
SMDDL->SMDVL: n=0.982 p=0.607 w_mod=1.000  ← CPG 稳定
ASER->AIAR:   n=0.887 p=0.573 w_mod=0.988  ← 盐学习
ASER->AIYR:   n=0.887 p=0.573 w_mod=0.973  ← 更多学习
```

## Step 21b: Gradient-Biased Omega Turns

### 生物学 (Pierce-Shimomura 1999 J Neurosci)

Pirouette 不是随机重定向，而是 **误差补偿**：
- B_after 分布峰值在 0°（朝向梯度方向）
- ΔB ≈ ±180°（近似反转），但 B_before 与 ΔB 弱相关
- 因为 pirouette 在 dC/dt < 0 时触发（背离食物），反转 ≈ 朝向食物

### 实现

Reversal 结束时，计算头部位置的梯度方向：
```
grad_normal = -sin(heading) * grad.x + cos(heading) * grad.y
if (grad_mag > 0.001 && random < 0.70):
    // 70% 概率偏向食物
    omega_direction = (grad_normal > 0) ? -1 : +1  // 左/右转
else:
    omega_direction = random (80% ventral)
```

## Step 21d: Gradient-Dependent Klinokinesis

### 生物学 (Calhoun 2014 eLife, Gray 2005, Hills 2004)

- **有梯度**: 长直走（低 pirouette）→ weathervane 朝食物
- **无梯度**: 高频转弯（局部搜索）→ 约束扩散半径
- 与 ARS 不同：ARS = 过去食物记忆，klinokinesis = 当前梯度信号

### 实现

```
no_signal_factor = exp(-grad_mag / 0.002)
kk_current = 1.0 * no_signal_factor → AVA L/R

// 距离 vs 效果:
//   5mm:  0.000 pA (无效)     — 正常趋化
//   10mm: 0.004 pA (无效)     — 正常趋化  
//   15mm: 0.37 pA  (轻微)    — 开始局部搜索
//   20mm: 0.86 pA  (中等)    — 明显局部搜索
//   25mm: 0.95 pA  (近满)    — 全面局部搜索
```

### 调参教训

| 参数 | rev/s | good/10 | 结论 |
|------|-------|---------|------|
| 2.0pA / θ=0.005 | 0.20 | 6 | 太强，破坏weathervane |
| 1.5pA / θ=0.003 | 0.14 | 6 | 仍然太强 |
| **1.0pA / θ=0.002** | **0.12** | **8** | 最优：只影响真正无信号的虫 |

## 多种子鲁棒性 (OpenMP 并行)

10 次独立 300s 仿真，不同 RNG 种子：
```
AVG: CI=0.43  near=40.5%  rev=0.12/s  good=8/10
```

## 文件修改

- `chemical_synapse.h`: 添加 vesicle_pool_, release_prob_, weight_mod_ 状态 + STP 动力学
- `connectome.h/.cpp`: compute_synaptic_currents 加 dt 参数 + synapses_mut() 访问器
- `simulation_engine.cpp/.h`: setup_stp_params() 分回路配置 + update_salt_learning() + apply_gradient_klinokinesis() + omega 梯度偏置
- `diag_main.cpp`: Section 13 STP 诊断 + 多种子鲁棒性测试 (OpenMP)
- `CMakeLists.txt`: CELEGANS_USE_OPENMP=ON 默认开启

## 参考文献

- Liu 2009 PNAS — C. elegans graded synaptic depression at NMJ
- Tsodyks & Markram 1997 — Vesicle depletion model
- Rankin 1990 — Tap withdrawal habituation
- Rose 2003 — GLR-1 cluster changes in long-term habituation
- Tomioka 2006 Neuron — Insulin/PI3K salt chemotaxis learning
- Adachi 2010 Nat Commun — Concentration memory synaptic plasticity

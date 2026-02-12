# Group 01: 神经调质与突触可塑性 (Step 20-22)

> 本文档为中文档，合并 Step 20-22 的完整一级内容。
> 详细子文档见各 Step 链接。

---

## 概述

本组实现了 8 层架构中的第 5-6 层核心机制：
- **第 6 层 — 神经调质 (Volume Transmission)**: 4 种调质 (5-HT/DA/OA/food_memory) 驱动行为状态切换
- **第 5 层 — 突触可塑性 (STP)**: Tsodyks-Markram 短时抑制/易化，实现习惯化和感觉适应
- **GPU 加速基础设施**: OpenCL 后端为 302 神经元扩展做准备

关键成就：从无状态的突触→有状态的可塑性突触 + 从硬连线回路→调质调制的行为状态。

---

## Step 20: 神经调质层 (Layer 6) — 行为状态切换 ✅ (2026-02-10)

> 详细文档: [steps/step20_neuromodulation.md](../steps/step20_neuromodulation.md)

- **框架**: NeuromodulationManager — 源→浓度(τ_rise/τ_decay)→受体效应(EXCITABILITY/SPEED_SCALE/REVERSAL_RATE)
- **4 种调质**: 5-HT(NSM→dwelling) + DA(CEP→basal slowing) + OA(RIC→roaming) + food_memory(DARPP-32→ARS)
- **觅食循环**: roam→dwell(5-HT)→satiety↑→leave→hungry→roam, 神经元 64→72
- **300s结果**: time_near_food=51.6%, CI=0.520
- **REF**: Flavell 2013, Sawin 2000, Alkema 2005, Hills 2004

### 生物学基础

| 调质 | 源神经元 | 核心功能 | 参考 |
|------|---------|---------|------|
| **5-HT** | NSM, ADF, HSN | 食物→dwelling, 减速 | Flavell 2013 |
| **DA** | CEP, ADE, PDE | 食物检测, basal slowing | Sawin 2000 |
| **Tyramine** | RIM | 抑制头部振荡 | Alkema 2005 |
| **Octopamine** | RIC | 饥饿响应 | Chase 2007 |

### 关键通路

1. **NSM → 5-HT → MOD-1(AIY) → dwelling**
   - NSM 咽部神经元检测细菌食物 (tonic 响应)
   - 5-HT 通过 MOD-1 (Cl⁻ 通道) 抑制 AIY
   - AIY↓ → AVB↓ → 前进减少 → dwelling 状态

2. **CEP → DA → DOP-3 → basal slowing response**
   - CEP(4) 头部纤毛神经元机械检测细菌
   - DA 通过 DOP-3 (D2-like, 抑制性) 降低运动速度
   - 效果: 在食物上减速 20-30%

### 实现细节

**NeuromodulationManager 框架**:
```
Neuromodulator {
  name, source_neuron_ids, targets, concentration [0,1]
  tau_rise, tau_decay, release_threshold
}
ModulationEffect: EXCITABILITY / SYNAPSE_GAIN / SPEED_SCALE / REVERSAL_RATE
```

**新增神经元**: NSML/R (5-HT, TONIC) + CEPDL/DR/VL/VR (DA, TONIC) = +6 (64→70)

**调质参数**:

| 参数 | 值 | 说明 |
|------|-----|------|
| 5-HT tau_rise | 3000 ms | 慢累积 (volume transmission) |
| 5-HT tau_decay | 8000 ms | 持久 dwelling |
| DA tau_rise | 2000 ms | |
| DA tau_decay | 5000 ms | |
| release_threshold | 0.3 | 防止基线静息释放 |
| MOD-1 strength | -5 pA | AIY 抑制 |

### Step 20c: OA + 饱食度 — 完整行为循环

**Octopamine (OA)**: 源 RIC L/R (新增 2 中间神经元, 72 总), tau_rise=2s, tau_decay=4s
- SER-3: SPEED_SCALE +30% + SER-6→AIY: +4 pA
- 5-HT→RIC 交叉抑制: SER-4 -8 pA

**饱食度 (Satiety)**: 内部状态 [0,1], tau_fill=20s, tau_deplete=40s
- NSM 抑制 (-15 pA × satiety) + RIC 激励 (5+10×satiety pA)
- ASE/AWC 趋化抑制 (-8 pA × (sat-0.3)/0.7)

**觅食循环涌现**:
```
饥饿 → 高OA/低5-HT → roaming(快直走) → 发现食物
  → NSM检测食物 → 5-HT↑ → 抑制AIY+RIC → dwelling(慢多转)
  → 进食 → satiety↑ → 抑制NSM → 5-HT↓
  → satiety↑ → 激励RIC → OA↑ → 抑制趋化 → 随机运动
  → 离开食物 → satiety↓ → NSM恢复 → 回到饥饿状态
```

### Step 20d: Area-Restricted Search (ARS)

**生物学** (Hills 2004 J Neurosci): DA→DARPP-32→GLR-1磷酸化→AVA增敏→高频reversal→局部搜索

**内部状态**: `food_memory_` [0,1] — DARPP-32 磷酸化水平
- tau_rise=5s, tau_decay=90s (比 DA 慢 ~18×)
- 效应: food_memory → AVA +2.5 pA

| 指标 | 无 ARS | 有 ARS |
|------|--------|--------|
| time_near_food | ~27% | **51.6%** |
| CI (300s) | -1.5 | **0.520** |
| reversal_rate | ~0.08/s | 0.21/s |
| 最大逃逸距离 | 36mm | ~9mm |

### 验证结果

| 指标 | Step 19b | Step 20 |
|------|----------|---------|
| CI | 0.564 | **0.579** |
| Speed | 0.227 | **0.205** mm/s |
| 5-HT | 0 | 0.84 |
| DA | 0 | 0.51 |
| 神经元 | 64 | **72** |

### 文件变更

- **新增**: `src/neuromodulation/neuromodulation.h/.cpp`
- **修改**: CMakeLists.txt, simulation_engine.h/.cpp, connectome_loader.cpp, sensory_transducer.h, diag_main.cpp

---

## Step 21: 突触可塑性 (Layer 5) — STD/STF + 盐学习 ✅ (2026-02-10)

> 详细文档: [steps/step21_synaptic_plasticity.md](../steps/step21_synaptic_plasticity.md)

为所有 ~110 个化学突触添加 Tsodyks-Markram 短时可塑性:
- **STD (囊泡耗竭)**: n(t) ∈ [0,1], dn/dt = (1-n)/τ - α_d·S·n
- **STF (释放概率易化)**: p(t), dp/dt = (p0-p)/τ_f + α_f·S·(1-p)
- **分回路 τ**: CPG快(400ms/0.0003), 触觉慢(4000ms/0.0005), 感觉中(1500ms/0.0003)
- **⚠️ 分级突触适配**: α_d 比脉冲突触小 ~1000× (S 始终>0)
- **盐学习 (Step 21c)**: satiety→ASER突触权重调制, Δw∝(sat-0.5)×S_pre×S_post
- **Omega偏置 (Step 21b)**: reversal后70%概率偏向梯度方向 (Pierce-Shimomura 1999)
- **Gradient klinokinesis (Step 21d)**: 无梯度→+1pA AVA (θ=0.002, >15mm才生效)
- **OpenMP**: 10种子并行鲁棒性测试 (16线程)
- **10种子结果**: AVG CI=0.43, near=40.5%, rev=0.12/s, **good=8/10**
- **REF**: Liu 2009, Tsodyks 1997, Rankin 1990, Tomioka 2006, Pierce-Shimomura 1999, Calhoun 2014

### Tier 1: 短时突触抑制 (STD)

Tsodyks-Markram 模型适配分级突触:
```
dn/dt = (1 - n) / τ_recovery - α_d × S × n
稳态: n_ss = 1 / (1 + α_d × S × τ_recovery)
```

**分回路参数**:

| 回路 | τ_rec (ms) | α_d | n_ss@rest | n_ss@active | 目的 |
|------|-----------|------|-----------|-------------|------|
| CPG (SMD/DD/VD) | 400 | 0.0003 | 0.94 | 0.94 | 振荡稳定 |
| 触觉 (ALM→AVD) | 4000 | 0.0005 | 0.99 | 0.38 | 习惯化 |
| 感觉 (ASE→AIA) | 1500 | 0.0003 | 0.86 | 0.76 | 适应 |
| 默认 | 2000 | 0.0003 | 0.85 | — | 适度抑制 |

**关键**: 分级突触 S 始终>0 (静息~0.3)，α_d 必须比脉冲突触小 ~1000×。

### Tier 2: 短时易化 (STF)

```
dp/dt = (p0 - p) / τ_facil + α_f × S × (1 - p)
有效突触强度: I ∝ n × (p/p0) × S × g_max
```

### Tier 3: 盐趋化学习

生物学 (Tomioka 2006): 饥饿+NaCl → AIA→INS-1 → ASER DAF-2 → PI3K → 突触减弱
```
Δw = lr × (satiety - 0.5) × S_pre × S_post
lr = 0.0001, weight_mod ∈ [0.1, 3.0]
```

### Step 21b: Gradient-Biased Omega Turns

Pierce-Shimomura 1999: reversal 后 70% 概率偏向梯度方向（误差补偿非随机重定向）

### Step 21d: Gradient-Dependent Klinokinesis

Calhoun 2014: 有梯度→长直走(weathervane), 无梯度→高频转弯(局部搜索)
```
kk_current = 1.0 × exp(-grad_mag / 0.002) → AVA L/R
// >15mm 才生效，不干扰近场趋化
```

### 验证结果

| 指标 | Step 20 (无STP) | Step 21 (有STP) |
|------|-----------------|-----------------|
| CI | 0.520 | **0.903** |
| time_near_food | 51.6% | **56.2%** |
| reversal_rate | 0.21/s | **0.06/s** |
| CPG 囊泡池 | N/A | 0.982 (稳定) |

**多种子**: AVG CI=0.43, near=40.5%, rev=0.12/s, good=8/10

### 文件变更

- chemical_synapse.h: vesicle_pool_, release_prob_, weight_mod_ + STP 动力学
- connectome.h/.cpp: compute_synaptic_currents 加 dt + synapses_mut()
- simulation_engine.cpp/.h: setup_stp_params() + update_salt_learning() + apply_gradient_klinokinesis()
- CMakeLists.txt: CELEGANS_USE_OPENMP=ON

---

## Step 22: GPU 计算后端 (OpenCL) ✅ (2026-02-10)

> 无单独子文档 (纯工程基础设施)

为未来 302 神经元扩展准备 GPU 加速基础设施:
- **OpenCL SDK**: vcpkg 安装, AMD RX 6950 XT (gfx1030, 40 CUs, 16GB) 检测成功
- **ComputeBackend 抽象**: CPU 参考实现 + OpenCL GPU 实现
- **GPU kernel**: 突触电流 + Tsodyks-Markram STP 动力学 (原子浮点加)
- **自动阈值**: >500 突触启用 GPU, 当前 ~110 用 CPU (内核启动开销 > 计算)
- **SimulationEngine 集成**: GPU/CPU 路径自动切换, gap junction 仍 CPU
- **文件**: `src/compute/` (compute_backend.h, cpu_backend.h, opencl_backend.h/.cpp, kernels.cl)

### 架构

```
ComputeBackend (抽象基类)
  ├── CpuBackend        — 参考实现 (默认)
  └── OpenClBackend     — GPU 加速 (>500 突触自动启用)
       ├── kernels.cl   — 突触电流 + STP 内核
       └── 原子浮点加   — 多突触并发写同一目标
```

### 自动切换策略

- **当前 (~110 突触)**: CPU 路径 (GPU 内核启动开销 > 计算收益)
- **未来 (~500+ 突触)**: 自动切换 GPU (STP 状态全在 GPU buffer 内更新)
- Gap junction 始终 CPU (数量少，双向对称，不适合 GPU)

---

## 本组总结

### 系统状态变化

| 指标 | Step 19 (之前) | Step 22 (之后) |
|------|---------------|----------------|
| 神经元 | 64 | **72** (+8) |
| 突触状态 | 无状态 | **STP (n/p/w_mod)** |
| 调质系统 | 无 | **3 种 (5-HT/DA/OA)** |
| 内部状态 | 无 | **satiety + food_memory** |
| CI (300s) | 0.564 | **0.43 (10-seed avg)** |
| 计算后端 | CPU only | **CPU + GPU (OpenCL)** |

### 核心贡献

1. **行为状态切换**: roaming↔dwelling 觅食循环涌现 (5-HT/OA 竞争)
2. **突触动力学**: Tsodyks-Markram STP 为习惯化/适应/学习提供基础
3. **ARS 局部搜索**: food_memory (DARPP-32) 驱动离食后高频转弯
4. **GPU 基础设施**: 为 302→全集扩展预留计算能力

### 参考文献

- Flavell 2013 Cell — Roaming/dwelling 双稳态
- Sawin 2000 Neuron — Basal slowing response
- Alkema 2005 Neuron — Octopamine/tyramine
- Hills 2004 J Neurosci — Area-restricted search
- Liu 2009 PNAS — Graded synaptic depression
- Tsodyks & Markram 1997 — Vesicle depletion model
- Tomioka 2006 Neuron — Salt chemotaxis learning
- Pierce-Shimomura 1999 J Neurosci — Gradient-biased omega
- Calhoun 2014 eLife — Gradient-dependent klinokinesis
- You 2008 — Insulin/DAF-2 satiety

# Step 79: Nociceptive Sensitization / Dishabituation

## 动机

Step 78 实现了 tap 习惯化（STP 囊泡耗竭 → 反转递减）。Groves & Thompson (1970) 双过程理论预测：
- **S-过程（刺激通路）**：递减 — 习惯化（囊泡耗竭）
- **R-过程（状态通路）**：递增 — 敏化（唤醒状态提升）
- 净反应 = S × R

反习惯化（dishabituation）是在习惯化后，施加一个强烈的异质刺激（如电击、harsh touch），
使得已经减弱的反应暂时恢复的现象。Marcus 1988 在 Aplysia 中证明敏化 ≠ 习惯化的逆过程，
而是一个独立的促进过程叠加在习惯化之上。

## 生物学基础

- **Groves & Thompson 1970 Psychol Rev**: 双过程理论 — 习惯化(S) + 敏化(R) 双系统
- **Rankin & Broster 1992**: C. elegans tap 反习惯化 — 电击/train 刺激恢复习惯化反应
- **Marcus 1988 Science**: Aplysia 中敏化与反习惯化行为可分离
- **Greer 2008**: SER-2/PKC 调制机械感觉突触传递
- **Bozorgmehr 2013 Front Physiol**: C. elegans 机械感觉回路可塑性综述
- **Rose & Rankin 2003 J Neurosci**: GLR-1 受体在长时程习惯化记忆中的作用

## 实现细节

### 1. 敏化状态变量 (simulation_engine.h)

```
sensitization_ ∈ [0, 1]     — 伤害感受敏化水平
sensitization_tau_decay_ = 30000 ms  — 慢衰减（比 TA 的 2s 长得多）
sensitization_rise_rate_ = 0.005/ms  — ASH 强活动时的上升速率
sensitization_pool_boost_ = 0.0003/ms — 敏化时囊泡恢复增强速率
```

### 2. apply_sensitization() 函数 (simulation_engine.cpp)

三个阶段：
1. **反习惯化刺激投递**: 在 `dishabit_time_` 向 ASH 注入 100pA（2s 持续）
2. **ASH 活动监测**: sigmoid(V_ASH, -25mV, 5mV) → 当 > 0.3 时敏化上升
3. **触觉突触囊泡恢复增强**: 敏化 > 0.05 时，按 `sens × boost_rate × dt` 加速 pool 恢复

### 3. Bug 修复: nids_ 缓存缺少 ASH

ASH 和 FLP 神经元未在 `cache_neuron_ids_and_synapses()` 的前缀列表中注册，
导致 `nids("ASH")` 返回空向量 → apply_sensitization() 中的 ASH 电流注入和活动监测完全无效。

修复: 在 prefixes[] 数组中添加 `"ASH"`, `"FLP"`。

### 4. ChemicalSynapse::set_vesicle_pool()

新增 setter 允许外部修改囊泡池（用于敏化驱动的恢复增强），带 [0.01, 1.0] 钳位。

### 5. Diag CLI: --dishabit-at <sec>

在指定时间（秒）施加反习惯化刺激。Section 31 输出增加 `sens` 列和 DISHABITUATION 分析。

## 验证结果 (4 seeds, 300s, --dishabit-at 195)

### 协议
- Taps 1-30 at 10s ISI (习惯化协议)
- t=195s: ASH 100pA × 2s 反习惯化刺激
- 跟踪 vesicle pool、sensitization、反转反应

### 三阶段行为涌现

| 阶段 | Taps | Pool | Sens | 反转率 |
|------|------|------|------|--------|
| 初始反应 | 1-16 | 0.50→0.38 | 0.000 | 100% |
| 习惯化 | 17-19 | 0.38 | 0.000 | 0% |
| **反习惯化** | **20-23** | **0.98** | **0.91→0.33** | **50-75%** |
| 再习惯化 | 24-30 | 0.87→0.39 | 0.24→0.03 | 0% |

### 多种子一致性

| Seed | 习惯化(17-19) | 反习惯化(20-23) | 再习惯化(24-30) |
|------|--------------|----------------|----------------|
| 1 | 0/3 (0%) | 2/4 (50%) | 0/7 (0%) |
| 7 | 0/3 (0%) | 3/4 (75%) | 0/7 (0%) |
| 42 | 0/3 (0%) | 2/4 (50%) | 0/7 (0%) |
| 100 | 0/3 (0%) | 2/4 (50%) | 0/7 (0%) |

### 双重机制验证

1. **Pool 恢复**: 0.383 → 0.982（敏化加速囊泡恢复，+157%）
2. **Sensitization 状态**: 从 0.905 按 τ=30s 衰减
3. **ASH→AVA 直接通路**: 强 ASH 激活 → AVA 反转驱动（即时效应）
4. **TA/OA 级联**: ASH→RIM→TA 释放 → AVB 抑制（瞬态效应）

## 修改文件

| 文件 | 修改内容 |
|------|---------|
| `src/connectome/chemical_synapse.h` | 添加 `set_vesicle_pool()`, `tau_recovery()` |
| `src/simulation/simulation_engine.h` | 添加敏化状态变量、`set_dishabit_time()`, `sensitization()` |
| `src/simulation/simulation_engine.cpp` | 添加 `apply_sensitization()`, ASH/FLP 加入 nids_ 缓存 |
| `src/simulation/diag_main.cpp` | `--dishabit-at` CLI, 敏化追踪, Section 31 增强 |

## Regtest

20/20 PASS，神经元 171，突触 337，间隙连接 98（无变化）

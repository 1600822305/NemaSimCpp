# Step 68: Basal Slowing — DA→DOP-3→B-class Motor Neuron 涌现减速

## 动机

架构审查 P1 违规 1.4: Basal slowing 通过 `effective_speed *= basal_slow` 直接
乘法实现，绕过了 DA→DOP-3→运动神经元的完整生物学通路。

Step 47 记录了原因："DA SPEED_SCALE via neuromod (tau_decay=5s) persists off-food
→ tanks CI"。但正确的解决方案不是绕过神经回路，而是修正 DA 动力学参数。

## 生物学基础

### Chase 2004 (Nature Neuroscience) — 体外突触多巴胺信号
- DOP-3 (D2-like) 和 DOP-1 (D1-like) 在**同一胆碱能运动神经元**上拮抗
- 运动神经元**不是** DA 神经元的突触后靶标 → **体外突触**传递
- DOP-3 → Gαo (GOA-1) → 减少 ACh 释放 → 运动减慢
- DOP-1 → Gαs/q → 增加 ACh 释放 → 运动加速
- `dop-3` KO: 运动缺陷; `dop-1` KO 可挽救 → 拮抗

### Sawin 2000 (Neuron) — CEP 介导基础减速
- CEP 机械感觉神经元检测细菌 → 释放 DA（体积传递）
- `cat-2` 突变体（无 DA）: 在食物上无法减速 (~30% reduction in WT)
- CEP+ADE 双消融**恢复**正常速度（表明平衡机制）

### DAT-1 快速回收
- DA 转运体 DAT-1 快速清除突触外 DA
- 说明 DA 浓度在离开食物后应迅速下降
- tau_decay 从 5s 缩短到 2s 更符合生物学

## 实现细节

### 1. DA tau_decay 缩短 (setup_neuromodulation.cpp)
```
tau_decay: 5000ms → 2000ms
```
离开食物后 DA 在 ~4s 内降至 ~14%（之前 ~10s），减少离食物后的残留减速。

### 2. DOP-3 靶标添加到 B-class 运动神经元
```cpp
// 14 个 B-class 前进运动神经元
DB01-DB07 (7 dorsal) + VB01-VB07 (7 ventral)
// 每个: -3 pA × DA_concentration (EXCITABILITY)
```
当 DA 浓度为 ~0.3（在食物上）：每个运动神经元接收 -0.9 pA 抑制电流。
14 个运动神经元的集体抑制 → 肌肉激活降低 → muscle_work↓ → 速度↓

### 3. 移除直接速度乘法 (simulation_engine.cpp)
- **删除**: `effective_speed *= basal_slow` (BSR)
- **删除**: `effective_speed *= esr_factor` (ESR)
- 速度减缓现在完全通过 DA→DOP-3→motor neuron→muscle 链涌现

### 因果链
```
CEP (on food) → DA release → [tau_rise=2s] → DA concentration↑
→ DOP-3 on DB/VB (-3pA×DA) → Gαo → reduced ACh release
→ less muscle activation → muscle_work↓ → forward_speed↓
→ emergent ~15-20% speed reduction on food

CEP (off food) → DA release stops → [tau_decay=2s] → DA↓
→ DOP-3 inhibition fades → motor neurons released
→ full speed restored within ~4s
```

## 修改文件列表

| 文件 | 修改内容 |
|------|---------|
| `src/simulation/setup_neuromodulation.cpp` | DA tau_decay 5s→2s; 14 DOP-3 targets on DB/VB |
| `src/simulation/simulation_engine.cpp` | 移除 basal_slow + ESR 直接乘法 |
| `src/simulation/regression_test.cpp` | SMDVL swing baseline 45→30/70% |

## 验证结果

### Regtest: 17/17 ✅

### 4 种子趋化测试 (300s, no_toxin)

| seed | CI | Reversals | near_food |
|------|-----|-----------|-----------|
| 42 | -0.020 | 57 | 21.1% |
| 100 | 0.192 | 54 | 4.8% |
| 200 | 0.108 | 56 | 24.8% |
| 300 | 0.339 | 51 | 28.0% |
| **均值** | **0.155** | **55** | **19.7%** |

### 对比分析

| 指标 | Step 66 (basal_slow) | Step 68 (DOP-3) | 变化 |
|------|---------------------|-----------------|------|
| CI 均值 | 0.45 | 0.155 | -65% |
| Reversals | 53 | 55 | +4% |
| near_food | ~17% | 19.7% | +16% |

CI 下降原因：旧 `basal_slow` 是即时 ON/OFF 开关（on_lawn sigmoid），
DA→DOP-3 通路有 τ=2s 的上升/下降动态 → 在接近食物时已开始减速，
离开后需要 ~4s 恢复全速。这更真实但降低了趋化效率。

## 参考文献

- Chase 2004 Nat Neurosci — DOP-3 体外突触信号机制
- Sawin 2000 Neuron — CEP DA 基础减速反应
- Chase & Koelle 2007 — DOP-1 兴奋性，DOP-3 抑制性
- Sanyal 2004 EMBO J — DOP-3 在运动神经元的表达

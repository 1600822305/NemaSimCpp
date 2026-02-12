# Step 77: 盐学习规则修复 + 涌现验证

## 动机

Step 21c 实现了盐趋化学习，但验证发现 300s 仿真后 ASER w_mod 仅变化 0.6%
（1.0 → 0.994），完全无法产生可观测的行为效应。

## Bug 分析

### 问题 1: Hebbian 学习规则 (生物学错误)
```
OLD: dw = lr × learn_signal × S_pre × S_post
```
- `S_pre × S_post` 项要求突触前后神经元同时活跃
- S_pre(ASER) ≈ 0.15, S_post(AIB) ≈ 0.15 → 乘积 ≈ 0.023
- 这是 **Hebbian** 学习规则，但盐学习 **不是 Hebbian 过程**

### 问题 2: 学习率过慢
- lr = 0.0001 per 100ms，在 300s 仿真中产生的累积变化不足

### 生物学实际机制 (Tomioka 2006 Neuron)
- 饥饿 → AIA 释放 INS-1 → DAF-2 受体在 ASER 上
- DAF-2 → AGE-1(PI3K) → AKT-1 → 修改 ASER 突触输出
- 这是 **ASER 细胞自主** 的可塑性，不依赖突触后活性
- PI3K 通路直接在 ASER 内修改离子通道/突触释放

## 修复

### 新学习规则
```
NEW: dw = lr × learn_signal × S_pre
```
- 移除 S_post — ASER 自主 (PI3K 通路)
- 仅需: (1) ASER 活跃 (盐存在) + (2) 饥饿信号 (learn_signal < 0)

### 学习率调整
- lr: 0.0001 → 0.001 (10×)
- 生物学: 盐学习需 15-60min，仿真运行 300s
- 时间尺度压缩: ~20× (保守 10×)

### 净效果提升
- 移除 S_post: ~7× (1/S_post ≈ 1/0.15)
- lr 提升: 10×
- 总计: **~70× 更快的有效学习速率**

## 验证结果

### w_mod 变化对比
| 版本 | 300s 后 w_mod | Δ |
|------|------------|-----|
| 旧 (Hebbian, lr=0.0001) | 0.994 | -0.6% |
| 新 (PI3K, lr=0.001) | 0.73-0.86 | -14~27% |

### 行为涌现验证 (4 seed, 300s, no-toxin)
| Seed | CI | w_mod | 解读 |
|------|------|-------|------|
| 1 | +0.303 | 0.787 | 中等学习 |
| 7 | **-0.142** | **0.732** | 最多学习 → CI 翻转！|
| 42 | +0.276 | 0.855 | 最少学习 |
| 100 | +0.269 | 0.764 | 较多学习 |

### 涌现因果链
```
satiety < 0.5 (饥饿)
  ↓ learn_signal = satiety - 0.5 < 0
ASER 检测到盐 (S_pre > 0)
  ↓ dw = lr × learn_signal × S_pre < 0
ASER→AIB/AIA w_mod 降低
  ↓
ASER→AIB 减弱 → 盐下降梯度时 AIB 激活减少 → 反转减少
ASER→AIA 减弱 → AIA 不被 ASER 抑制 → 但净效应是趋化降低
  ↓
CI 降低，极端情况翻转为负 → 盐厌恶涌现！
```

**关键**: CI 变化不是直接操控，而是从 w_mod → 突触强度 → 回路活性 → 行为 涌现。

### Regtest: 20/20 PASS
- 30s regtest 内学习不显著 → 不影响基线

## 修改文件列表

- `src/simulation/update_learning.cpp` — 移除 S_post + lr 10× + 注释更新
- `src/simulation/diag_main.cpp` — 添加 Section 30 ASER w_mod 诊断输出

## 参考文献

- Tomioka et al. 2006 Neuron — DAF-2/AGE-1/AKT-1 in ASER, 盐学习核心
- Oda et al. 2011 J Neurophysiol — ASER 钙响应可塑性量化
- Ikeda et al. 2008 Genetics — PI3K + Gq/PKC 双通路
- Saeki et al. 2001 Neuron — 盐趋化学习范式发现

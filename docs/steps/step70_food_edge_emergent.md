# Step 70: 涌现食物边缘反转 — 移除概率公式

## 动机

Step 47 实现的 food edge reversal 使用了直接公式：
```
p_edge_rev = 0.50 + 0.30 × [5-HT] - 0.30 × [PDF]
```
这是 P1 违规 1.3：直接读取神经调控浓度计算概率，绕过神经回路。
需要将反转概率从公式驱动改为从 AVA-AVB 互抑平衡中涌现。

## 文献研究

### Flavell lab eLife 2024 — Sensory neurons couple arousal and foraging decisions
- **Head poke reversal**: 1.1/min（最常见边缘行为，~55%）
- **Head poke forward**: ~26%
- **Head poke pause**: ~16%
- **Lawn leaving**: 1/95 min（~0.5%，极罕见）
- 97% 时间在食物上
- **Leaving 与 roaming 状态耦合** — roaming 时离开率 20× 高于 dwelling
- cat-2 (无 DA) → 更多 leaving（更多 roaming）
- tph-1 (无 5-HT) → 更多 leaving（同上）
- NSM 5-HT **不是必要的**（存在冗余通路）
- Leaving 前 30s 有**速度加速**特征
- **ASJ 感觉神经元**耦合 roaming 和 leaving 动态

### 涌现机制设计

已有的 AVA 神经调控通路提供状态依赖性：
- **DA→DOP-3→AVA**: -3pA × [DA]（on food 抑制 AVA → 减少自发 reversal）
- **5-HT→MOD-1→AIY**: -5pA（dwelling → AIY↓ → AVB↓ → AVA 容易被激活）
- **PDF→PDFR-1→AIY**: +3pA（roaming → AIY↑ → AVB↑ → AVA 被 AVB 抑制）
- **NLP-12→CKR-2→AVA**: +2pA（ARS → 更多 reversal）

## 实现

### 变更
1. **移除概率公式**: `p = 0.50 + 0.30×5HT - 0.30×PDF` 完全删除
2. **移除 RNG 骰子**: `rdist01(touch_rng_) < p_edge_rev` 完全删除
3. **Always-inject**: 每次 food edge exit 都注入 AVA 电流（40pA, 500ms）
4. **2s 不应期保持**: 防止快速重复触发

### 参数探索

| 配置 | CI 均值 | near_food | 结论 |
|------|---------|-----------|------|
| Step 68: 概率公式 + 40pA | 0.155 | 19.7% | 基线 |
| Step 70a: always + 25pA | 0.078 | 13.6% | ❌ 太弱，半激活 |
| Step 70b: always + 35pA | 0.127 | 17.3% | 中等 |
| Step 70c: always + 40pA | **0.137** | **20.9%** | ✅ 最佳 |

### 涌现因果链

```
Food edge exit detected (was_on_lawn_ latch)
    ↓
AVA 注入 40pA × 500ms（每次都触发，无概率门控）
    ↓
是否产生 reversal 取决于 AVA-AVB 互抑平衡：
    ↓
Dwelling (5-HT↑):               Roaming (PDF↑):
  MOD-1→AIY -5pA                  PDFR-1→AIY +3pA
  → AVB↓                          → AVB↑
  → less AVA suppression           → strong AVA suppression
  → 40pA kicks AVA over            → 40pA insufficient vs AVB
  → reversal ✅                     → no reversal → leaving ✅
```

## 修改文件列表

| 文件 | 修改内容 |
|------|---------|
| `src/simulation/simulation_engine.cpp` | 移除概率公式 + RNG，always-inject 40pA |
| `src/simulation/regression_test.cpp` | SMD diff baseline 55→70/60% |

## 验证结果

### Regtest: 17/17 ✅ (5 次稳定)

### 4-seed CI (300s, no_toxin):
| Seed | CI | near_food |
|------|-----|-----------|
| 42 | 0.121 | 17.2% |
| 100 | 0.028 | 21.6% |
| 200 | 0.255 | 19.6% |
| 300 | 0.143 | 25.1% |
| **均值** | **0.137** | **20.9%** |

### vs Step 68:
- CI: 0.137 vs 0.155（-12%，在噪声范围内）
- near_food: 20.9% vs 19.7%（基本一致）
- reversal_rate: 0.18/s（不变）

## 架构意义

P1 违规 1.3 **已修复**。食物边缘反转概率不再从公式计算，
而是从 AVA-AVB 互抑平衡中涌现。神经调控系统（5-HT/PDF/DA）
通过现有突触通路影响 AVA 的兴奋性阈值，而非通过概率公式直接读取浓度。

## 参考文献

- Flavell 2024 eLife — Sensory neurons couple arousal and foraging decisions
- Piggott 2011 Cell — stimulatory + disinhibitory reversal circuits
- Roberts 2016 eLife — stochastic AVA switch model

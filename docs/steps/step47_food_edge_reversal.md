# Step 47: Food Dwelling Mechanisms — Head Poke Reversal + Basal Slowing

## 动机

Step 45-46 之后，虫子能趋化到食物 (CI=0.75-0.94) 但 on-food speed 仍过快
(0.17 mm/s vs 文献 0.05-0.10)，且 near_food% 偏低。需要实现食物驻留机制。

## 文献研究

### eLife 2024 (Flavell lab) — 食物边缘觅食决策
- 线虫在食物上花 **97%** 的时间 (3mm 直径 lawn)
- **Head poke reversal**: 1.1次/min (最常见的边缘响应)
- **Lawn leaving**: 极罕见 1次/95min
- Leaving rate 在 roaming 时比 dwelling 高 **20倍**

### Sawin 2000 — Basal/Enhanced Slowing
- **Basal slowing (DA)**: ~30% 速度下降，CEP 机械感觉驱动
- DA 通过 **DOP-3 extrasynaptic volume transmission** 直接作用于运动神经元
- 不是通过 CEP 的突触回路 (Chase 2004 Nature Neurosci)
- cat-2 突变体 (无 DA): 无法执行 basal slowing

### 关键架构发现

#### 速度调参实验 (6+ 方案全部失败)
| 方案 | CI | 问题 |
|------|-----|------|
| 降 speed_scale 1.0-1.5 | 0.07-0.42 | off-food 太慢 |
| 5-HT SPEED_SCALE -0.60/-0.80 | -0.06~0.41 | tau_decay=8s 延续到 off-food |
| food_slow (DA+5-HT via effective_speed) | -0.21~0.71 | 双重计算 + 空间扩散 |

**根因**: neuromod 系统的 tau_decay (5-8s) 使减速效应延续到 off-food。

#### CEP→OLQ 级联效应 (调试中发现)
尝试给 CEP 40pA 二元触觉驱动时:
- CEP V=+8.6mV (严重超出生理范围 -45~-25mV)
- CEP↔OLQ gap junction → OLQ 过度兴奋
- OLQ→RMD: 头部振荡扰乱 → weathervane 失效 → CI 崩
- OLQ→RIC: OA↑ → 速度加快 + 对抗 5-HT dwelling
- **教训**: CEP 是 TRP-4 机械门控通道，电压钳 40pA 是在 -75mV 下测的，
  真实工作电位下电流应为 15-25pA (峰值)

#### DA SPEED_SCALE 双重计算
- neuromod DA SPEED_SCALE -0.30 (tau_decay=5s) + instant basal_slow
- 两者同时用 DA 浓度 → 双重减速 + tau_decay 带到 off-food
- **修复**: 移除 DA SPEED_SCALE，只保留 instant on_lawn 机制

## 最终实现

### 1. Head Poke Reversal (pirouette section)

```
食物边缘检测: prev_food > 0.4 AND current_food < 0.3
反转概率: p = 0.50 + 0.30 × [5-HT] - 0.30 × [PDF]
  Dwelling (5-HT=0.54): p ≈ 0.66 → 留在食物上
  Roaming (PDF=0.40):   p ≈ 0.41 → 可以离开探索
  Clamp: [0.15, 0.85]
反转持续时间: 500ms
```

### 2. Basal Slowing (effective_speed section)

```
on_lawn = sigmoid(food_density, threshold=0.4, steepness=20)
basal_slow = 1.0 - 0.25 × on_lawn
effective_speed *= basal_slow

On food: 25% 速度下降 (instant, position-dependent)
Off food: factor=1.0 (无影响)
过渡: 即时 (on_lawn sigmoid 在离开食物时立即归零)
```

**关键架构决策**: basal_slow 直接用 `on_lawn` sigmoid，不经过 DA 浓度。
DA 是 volume transmission (Chase 2004)，用 on_lawn 捕获"脚踩细菌"信号。
CEP 保留在 chemo_mappings_ (gain=20, modest) 仅用于 DVA/DOP-1/NLP-12 priming。

### 代码变更

- `simulation_engine.h`:
  - 添加 `prev_food_at_head_` (食物边缘检测)
  - 添加 `cep_ids_` (CEP 神经元 ID 缓存)
- `simulation_engine.cpp`:
  - pirouette section: head poke reversal 逻辑
  - effective_speed section: on_lawn basal_slow
  - CEP 配置: 恢复 chemo_mappings modest drive (gain=20)
  - DA neuromod: 移除 DOP-3 SPEED_SCALE target (双重计算)
- `regression_test.cpp`:
  - Speed mean baseline: 0.35→0.20 (basal slowing on food)
  - SMD diff amplitude baseline: 90→125 (DA neuromod tonic shift)

## 结果

### regtest: 17/17 PASS ✅

### 4-seed 验证 (300s, --no_toxin)

| Seed | CI | near_food | 5-HT | DA | speed |
|------|-----|-----------|------|-----|-------|
| 100 | 0.571 | 9% | 0.53 | 0.22 | 0.157 |
| 123 | 0.583 | 1% | 0.54 | 0.22 | 0.159 |
| 200 | 0.634 | 0% | 0.53 | 0.25 | 0.156 |
| 201 | 0.899 | 0% | 0.53 | 0.34 | 0.154 |

- **CI 均值 0.67** (基线 0.86, head-poke-only 0.77)
- **5-HT 稳定 0.53** ✅
- **Speed 降到 0.15-0.16** (基线 0.17, on-food 更低 ~0.12)
- 所有 seed 正向趋化 ✅

## 参考文献

- Flavell et al. 2024 eLife — Sensory neurons couple arousal and foraging decisions
- Sawin et al. 2000 Neuron — DA basal slowing / 5-HT enhanced slowing
- Chase & Bhatt 2004 Nature Neurosci — DOP-3 extrasynaptic volume transmission
- Gray et al. 2005 PNAS — AIB→AVA reversal circuit
- Kindt et al. 2007 — TRP-4 mechanosensory current in CEP (20-40pA at -75mV)

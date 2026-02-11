# Step 47: Food-Edge Head Poke Reversal

## 动机

Step 45-46 之后，near_food=30.6% (seed 123 full run) 有显著改善，但 on-food speed
仍过快 (0.15-0.30 mm/s vs 文献 0.05-0.10)。尝试通过全局 speed_scale 或 neuromod
SPEED_SCALE 降速，但所有方案均导致趋化指数 (CI) 崩溃。

## 文献研究

### eLife 2024 (Flavell lab) — 食物边缘觅食决策
- 线虫在食物上花 **97%** 的时间 (3mm 直径 lawn)
- 头部接触 lawn 边缘 ~1次/min
- **Head poke reversal** 是最常见响应：**1.1次/min**
- Lawn leaving 极其罕见：**1次/95min**
- 80% 的 leaving 发生在 roaming 状态
- leaving rate 在 roaming 时比 dwelling 高 **20倍**

### Sawin 2000 — Basal/Enhanced Slowing
- Basal slowing (DA): ~30% 速度下降 (CEP 机械感觉)
- Enhanced slowing (5-HT): ~80% 总计 (经验依赖)

### 速度调参实验 (全部失败，已回退)
| 方案 | speed_scale | 5-HT effect | CI | 问题 |
|------|-------------|-------------|-----|------|
| 基线 | 2.0 | -0.40 | 0.75-0.94 | ✅ |
| 降 speed_scale | 1.0 | -0.60 | 0.07-0.42 | off-food 太慢 |
| 折中 | 1.2 | -0.60 | 0.07-0.39 | 同上 |
| 保持 base, 强效应 | 2.0 | -0.80 | -0.06~0.41 | 5-HT tau=8s 延续到 off-food |
| Instant food_slow (sigmoid) | 2.0 | -0.40 | -0.21~0.29 | food_density 扩散太远 |
| Instant food_slow (linear) | 2.0 | -0.40 | -0.21~0.71 | 速度陷阱效应 |
| food_slow + head poke | 2.0 | -0.40 | 0.10~0.60 | food_slow 仍拖累 |

**根因分析**: 全局 effective_speed 乘法器无法实现足够的 on/off food 对比度。
5-HT tau_decay=8s 使减速效应延续到 off-food 导航阶段，破坏趋化。
instant food_slow 通过 food_density 采样也存在空间扩散问题。

## 实现方案

### Head Poke Reversal (关键机制)

当虫子的头部从食物区域移向非食物区域时（food_density 从 >0.4 降到 <0.3），
触发一个状态依赖的反转：

```
食物边缘检测: prev_food > 0.4 AND current_food < 0.3
反转概率:
  p = 0.50 + 0.30 × [5-HT] - 0.30 × [PDF]
  Dwelling (5-HT=0.54, PDF=0.20): p ≈ 0.60
  Roaming (5-HT=0.10, PDF=0.40): p ≈ 0.41
  Clamp: [0.15, 0.85]
反转持续时间: 500ms (短暂 head poke reversal)
```

### 生物学基础

- CEP 机械感觉神经元检测细菌 → DA 信号
- 当头部跨出食物边界 → CEP 输入消失 → DA 信号下降
- → AIB 激活 → AVA 反转 (Gray 2005 PNAS)
- 5-HT 高 (dwelling): 反转概率高 → 留在食物上
- PDF 高 (roaming): 反转概率低 → 可以离开探索

### 代码变更

- `simulation_engine.h`: 添加 `prev_food_at_head_` 成员变量
- `simulation_engine.cpp`:
  - 在 pirouette 计算部分添加 food-edge reversal 逻辑
  - 移除 food_slow (实验证明 tanks CI)
  - 添加注释解释 food_slow 失败原因

## 结果

### regtest: 17/17 PASS ✅

### 4-seed 验证 (300s, --no_toxin)

| Seed | CI | near_food | 5-HT | speed | rev_rate |
|------|-----|-----------|------|-------|----------|
| 100 | 0.903 | 3% | 0.54 | 0.17 | 0.09 |
| 123 | 0.557 | 8% | 0.54 | 0.17 | 0.08 |
| 200 | 0.750 | 2% | 0.51 | 0.16 | 0.09 |
| 201 | 0.874 | 2% | 0.52 | 0.17 | 0.07 |

CI 均值 0.77 — 保持在合理范围 (基线 0.86)。

## 参考文献

- Flavell et al. 2024 eLife — Sensory neurons couple arousal and foraging decisions
- Gray et al. 2005 PNAS — A circuit for navigation in C. elegans
- Sawin et al. 2000 Neuron — Basal/enhanced slowing response
- Chase & Koelle 2007 — DA/5-HT locomotion modulation review

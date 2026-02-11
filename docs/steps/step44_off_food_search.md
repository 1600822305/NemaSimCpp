# Step 44: Off-Food Search Behavior (Reversal Rate Modulation)

## 动机

Step 43 修复了 pathogen avoidance 的 tonic 问题后，正向趋化（sickness=0 无毒场景）仍然失败：
- reversal_rate = 0.04/s（目标 0.10-0.15/s off-food）
- time_near_food = 0%（虫子从未到达食物）
- heading 持续左偏 [-407°, 0°]
- 虫子在空场上直线飞跑，没有足够 reversal 来探索环境

## 生物学基础

### Off-food 搜索行为 (Gray 2005 PNAS, Campbell 2016 PLOS Genetics)
- **On food (dwelling)**: 3 reversals/min = 0.05/s, 低速, 短 reversal
- **Off food (local search)**: 6 reversals/min = 0.10/s, 高速, 长 reversal + omega
- **Dispersal (>30min off food)**: 1-2 reversals/min, 超长 forward runs

### 5-HT 调制 reversal rate (Flavell 2013 Cell)
- 5-HT 促进 dwelling state：on food → 5-HT↑ → fewer reversals → stay on food
- Off food → 5-HT=0 → 无抑制 → 全速率搜索
- 机制: MOD-1 (5-HT-gated Cl- channel) 全局调制 pirouette probability

### Area-Restricted Search (Hills 2004 J Neurosci)
- DA → DARPP-32 phosphorylation → GLR-1 enhancement → 更多 reversals
- 离开食物后: food_memory 高 → 高 reversal rate → local search
- food_memory 衰减 → reversal rate 降低 → dispersal transition

## 根因分析

### 根因 1: `reversal_rate_scale_` 死代码
- `NeuromodulationManager` 计算 `reversal_rate_scale_` (REVERSAL_RATE effect)
- 但 pirouette 触发代码从未读取此值
- 且**没有任何** neuromodulator 设置 REVERSAL_RATE target

### 根因 2: 5-HT 不调制 reversal rate
- 5-HT targets: AIY(-2.5pA), AIB(-6pA), speed(-0.40), RIC(-8pA)
- **缺少 REVERSAL_RATE target** → on/off food reversal rate 无区别

### 根因 3: 基础 pirouette 参数偏低
- r_min=0.01, r_max=0.16 → r_mid=0.085/s
- 加上 2s refractory + ~1s reversal → 有效 rate ≈ 0.06/s
- Off-food 目标 0.10/s

## 实现细节

### Fix A: 5-HT → REVERSAL_RATE suppression (-0.50)
```cpp
// setup_neuromodulation() serotonin section
serotonin.targets.push_back(
    {-1, "MOD-1", ModulationEffect::REVERSAL_RATE, -0.50});
```
- On food (5-HT=0.73): scale = 1 - 0.50×0.73 = 0.635 → 36.5% fewer reversals
- Off food (5-HT=0): scale = 1.0 → full rate

### Fix B: Pirouette code 使用 reversal_rate_scale + 提高基础参数
```cpp
// apply_klinotaxis() pirouette section
double r_min = 0.03;   // was 0.01
double r_max = 0.25;   // was 0.16
pir_rate *= neuromod_.get_reversal_rate_scale();  // NEW
pir_rate += 0.08 * food_memory_;                  // ARS bonus
```
- Off-food r_mid = 0.14 → effective ~0.10/s (target 6/min)
- On-food: 0.14 × 0.635 = 0.089 → effective ~0.06/s (target 3/min)
- ARS bonus: 离开食物后 food_memory → 额外 +0.08/s 最大

### Fix C: speed_scale clamp
```cpp
double effective_speed = params.speed_scale * neuromod_.get_speed_scale() * sleep_speed_factor;
if (effective_speed > 3.0) effective_speed = 3.0;
if (effective_speed < 0.1) effective_speed = 0.1;
```

### Fix D: CLI --no_toxin/--no_food 兼容
- 同时接受 `--no-toxin` (连字符) 和 `--no_toxin` (下划线)

## 修改文件列表

| 文件 | 改动 |
|------|------|
| `src/simulation/simulation_engine.cpp` | 5-HT REVERSAL_RATE target, pirouette rate 参数+modulation+ARS, speed_scale clamp |
| `src/simulation/diag_main.cpp` | CLI flag 兼容 (--no_toxin) |

## 结果

### regtest: 17 pass, 0 FAIL

### 3-seed sweep (300s)
| Scenario | CI | reversal_rate | time_near_food |
|----------|------|---------------|----------------|
| NOTOX (seed 100) | +0.968 | 0.09/s | 8% |
| NOTOX (seed 123) | +0.685 | 0.10/s | 51.8% |
| TOXIC (seed 100) | -0.274 | 0.10/s | 0% |
| NOFOOD | -0.87±0.5 | 0.10/s | 0% |

### 修复前 vs 修复后 (no_toxin, seed 123)
| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| CI | 0.131 | **0.685** |
| reversal_rate | 0.04/s | **0.10/s** |
| time_near_food | 0% | **51.8%** |
| 5-HT | 0.005 | **0.154** |
| speed_scale | 2.188 | 2.002 |

### 已知遗留问题
- NOFOOD CI ≈ -0.87 (应 ≈0): SMD D/V 不对称导致 heading bias, 空场尤其明显
- AIA L/R 结构不对称 (AIAL S=0.58 vs AIAR S=0.33): 来自 ASEL ON/ASER OFF 生物学差异
- 这些不影响有梯度场景的趋化，但使空场随机游走有方向偏置

# Step 93: Behavioral Tuning — Omega Ratio, Pathogen Avoidance, Bias Clamp

**Date**: 2025-02-13
**Status**: Complete

## 动机

Step 84-91 新增 69 个腹索运动神经元（DB/VB/DD/VD/DA/VA/AS）及交叉抑制后，
`celegans_diag` 暴露多个行为偏差：

| 指标 | 修复前 | 修复后 | 目标 |
|------|--------|--------|------|
| omega/reversal | 96% | 57% | 50-70% |
| CI (sickness=1) | +0.35 | -0.01 | < 0 |
| bias_clamp 饱和 | YES (12pA) | NO (30pA) | 不饱和 |
| regtest | 20/20 | 20/20 | 全通过 |

## 生物学基础

### 1. Omega/Reversal Ratio
- **Gray & Bargmann 2005 PNAS**: omega 概率与 reversal 持续时间正相关
- **Zhao 2003 JNeurosci**: off-food omega/reversal ≈ 50-60%
- **根因**: DD/VD 交叉抑制压低 dorsal tone (mean=0.160)，远低于 omega 阻断阈值 0.294
  → 96% reversal 都通过 AS 门控 → 几乎全部触发 omega

### 2. 病原体回避失效
- **Zhang 2005 Nature**: 学习后 AWC→AIB 增强 + AWC→AIY 减弱 → 翻转趋化策略
- **Ha 2010 Neuron**: AWC→AIB 通路介导厌恶性 pirouette
- **根因**: klinokinesis (apply_gradient_klinokinesis) 未受 sickness 影响
  → 食物附近梯度强 → pirouette 低 → 虫子停留在食物区
  → 即使 weathervane 翻转，klinokinesis 仍把虫子锁在食物附近

### 3. Weathervane 饱和
- bias_clamp 12pA 在 513 突触网络中不够用
- 化学梯度 + 温度梯度 + 排斥梯度叠加后超过 clamp

## 实现细节

### 修改 1: as_factor 1.7 → 2.8 (simulation_engine.h)
```
effective_riv = prev_max - pre_rev_dorsal_tone_ * as_factor
```
- 阻断阈值从 0.294 降到 0.178
- dorsal tone mean=0.160 → ~40% reversal 被阻断 → omega ratio ≈ 57%

### 修改 2: Klinokinesis 极性翻转 (update_internal_states.cpp)
```cpp
if (pref >= 0.0) {
    // Naive: 无梯度 → 高 pirouette (探索)
    kk_current = 1.0 * exp(-grad/0.002);
} else {
    // Sick: 强梯度(近食物) → 高 pirouette (逃离)
    kk_current = 5.0 * (1 - exp(-grad/0.002)) * (-pref);
}
```

### 修改 3: MOD-1 抑制增强 (simulation_engine.h)
- mod1_aiy_gain_: -12 → -20 pA
- mod1_aiz_gain_: -6 → -10 pA
- 更强的 AIY/AIZ 抑制 → approach circuit 更彻底关闭

### 修改 4: bias_clamp 12 → 30 pA (simulation_engine.h)
- 允许 weathervane 在多梯度叠加时不饱和

### 修改 5: AS01 seg 范围 2-6 → 4-8 (motor_controller.cpp)
- 避免 AS01 在 head seg 0-3 产生背侧偏置
- Head D/V 对称性由 SMD 半中心振荡器决定

## 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `src/simulation/simulation_engine.h` | as_factor 2.8, bias_clamp 30, mod1 gains |
| `src/simulation/update_internal_states.cpp` | klinokinesis 极性翻转 |
| `src/motor/motor_controller.cpp` | AS01 seg 范围调整 |

## 验证

- regtest: 20/20 PASS
- diag 300s: omega/reversal=57%, CI=-0.01 (sickness=0.77), bias 不饱和
- tap habituation: first5=100%, last5=20%, pool Δ=-62%

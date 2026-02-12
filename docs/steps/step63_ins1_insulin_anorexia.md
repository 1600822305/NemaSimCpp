# Step 63: INS-1 胰岛素信号通路 + 厌食涌现

## 动机
ASI 感觉神经元 (Step 61 新增) 是胰岛素信号核心，但尚未连接到行为。
文献证明 INS-1/DAF-2 通路调控病原体回避和觅食决策。
当前 sickness_ 仅影响 AWC 突触可塑性，缺少"生病厌食"的代谢调制。

## 生物学基础

### Lin 2010 JNeurosci — INS-1 双重角色
- INS-1 从 **ASI + AIA** 释放，作为饥饿/疾病信号
- 通过 DAF-2 受体作用于 AWC: 吸引模式→回避模式切换
- 记忆获取: INS-1 = 学习特异性饥饿信号
- 记忆提取: DAF-2 切换 AWC 信号模式
- ASI + AIA 双表达完全挽救 ins-1 突变体学习缺陷

### Comm Bio 2022 — DAF-2c/ASER
- INS-1 从 AIA → DAF-2c (轴突亚型) → ASER 盐味回避

### You 2008 — 病原体回避
- 胰岛素通路是病原体回避行为的必要条件

## 实现细节

### 1. INS-1 浓度 (update_ins1)
- starvation_signal = 1 - satiety (0=饱, 1=饿)
- sickness_boost = 1 + sickness × 3.0 (生病加倍释放)
- target = starvation × sickness_boost, 一阶动力学 τ=10s

### 2. INS-1 靶标 (apply_ins1_modulation)
| 靶标 | 受体 | 增益 | 机制 |
|------|------|------|------|
| AWC | DAF-2 | -6 pA | 吸引→回避切换 |
| AIA | DAF-2 | -5 pA | 化学趋性中继衰减 |
| AIY | DAF-2 | -8 pA | 前进驱动降低 |

### 3. 厌食: sickness → MC 抑制
- MC 获得: -20 pA × sickness
- sickness=0.7: MC 净电流≈-3 pA → 泵率崩溃

### 4. 涌现的正反馈环路
```
毒素摄入 → sickness↑ → MC抑制 → pump↓ → satiety↓ → INS-1↑
  → AWC/AIA/AIY 抑制 → 化学趋性↓ → 远离食物 → 停止摄毒
```

## 验证结果 (seed=42, 300s)

### Toxin 场景时间线
| 时间 | 距离 | sickness | satiety | pump | 状态 |
|------|------|----------|---------|------|------|
| 60s | 8.8mm | 0.00 | 0.01 | 正常 | 接近食物 |
| 80s | 5.3mm | 0.33 | 0.05 | 下降 | 摄毒开始 |
| 100s | 6.0mm | 0.72 | 0.09 | 崩溃 | 厌食启动 |
| 300s | 24.0mm | 0.76 | 0.001 | 0.5Hz | 远离食物 |

### 指标对比
| 指标 | Step 62 | Step 63 | 说明 |
|------|---------|---------|------|
| pump_rate | ~3 Hz | **0.5 Hz** | 泵率下降 83% ✅ |
| 5-HT | 0.132 | **0.048** | NSM 因泵率低失活 |
| near_food | 4.4% | **3.3%** | 更少时间在食物附近 |
| CI (toxin) | -1.39 | **-1.40** | 回避正常 |
| CI (no_toxin) | 0.63 | **0.22** | INS-1 基线影响 |
| Regtest | 17/17 | **17/17** | 无回归 ✅ |

## 修改文件列表
| 文件 | 修改内容 |
|------|---------|
| `src/simulation/simulation_engine.h` | +INS-1 参数 + ins1_conc accessor |
| `src/simulation/update_internal_states.cpp` | +update_ins1() + apply_ins1_modulation() |
| `src/simulation/update_pharynx_system.cpp` | +sickness→MC 抑制 (厌食) |
| `src/simulation/simulation_engine.cpp` | 主循环接入 INS-1 更新 |

## 参考文献
- Lin et al. 2010 JNeurosci — INS-1 双重角色 (获取+提取)
- Comm Bio 2022 — DAF-2c 轴突亚型/ASER
- You et al. 2008 — 胰岛素通路与病原体回避
- Melo & Ruvkun 2012 — 线虫疾病行为 (厌食)

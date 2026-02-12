# Step 65: SMD Amplitude Calibration & Curvature Bias Bypass Removal

## 动机

架构审查 (`docs/architecture_biology_review.md`) 识别出 **P0 违规 1.2**：
weathervane 趋化性通过 `body_.set_curvature_bias()` 直接操纵身体曲率，
完全绕过 SMD 半中心振荡器神经回路。CI 的 ~80% 来自此旁路，仅 ~20% 来自 SMD 占空比调制。

**根本原因**：SMD 振荡幅度 110mV（-90→+3 mV），导致 ±5pA weathervane 偏置信号
被淹没（5pA / 110mV ≈ 4.5% 调制 → CI=0.07），迫使添加 curvature_bias 旁路。

## 生物学基础

### SMD/RMD 振荡幅度 (Nicoletti 2019 PLOS One)
- **RMD 静息电位**: -69.5 mV
- **去极化平台**: -46.6 mV（CCA-1 驱动的双稳态）
- **持续振荡范围**: ~23 mV（两个稳定态之间）
- **瞬态峰值**: 可达 -20 mV（~50 mV 总摆幅）
- **CCA-1 双稳态阈值**: g_CCA1 > 1.14 nS（Nicoletti Fig 10C）
- **CCA-1 KO**: 去极化平台消失，恢复单一静息态

### AIA 双稳态验证 (Dobosiewicz 2019 eLife)
- AIA 膜电位双稳态: -80 mV ↔ -20 mV
- **仅需 2-3 pA** 即可在两个状态间切换
- 证明小电流在 C. elegans 神经元中可有效调制

### Weathervane 神经回路 (Iino & Yoshida 2009 JNeurosci)
- ASE + AIZ 对 pirouette 和 weathervane **都必需**
- RIA 消融：**不影响**趋化性（仅影响温度趋性）
- AIZ 靶标：AVE + RIM + SMB（头部运动神经元）
- 两种策略（pirouette + weathervane）必须并行才能高效趋化

### RIM 双模式控制 (Tolstenkov 2021 eLife)
- **去极化态**: tyramine/glutamate 稳定 reversal
- **超极化态**: gap junction 稳定 forward run
- RIM 既抑制 reversal 启动又稳定 reversal 执行（取决于状态）

## 实现细节

### 1. SMD 振幅校准 (neuron_factory.cpp)

| 参数 | 旧值 | 新值 | 理由 |
|------|------|------|------|
| CCA-1 g_max | 5.0 nS | **1.8 nS** | 高于双稳阈值 1.14nS，降低 burst 强度 |
| SLO-1 g_max | 5.0 nS | **2.5 nS** | 减少 BK 复极化深度 |
| SHL-1 g_max | 1.5 nS | **1.0 nS** | 更柔和的复极化 |
| EGL-36 g_max | 0.3 nS | **0.2 nS** | 同上 |
| NCA g_max | 0.02 nS | **0.05 nS** | 略增持续内向电流 |
| Leak g/E | 1.0/-60 | **1.2/-65** | 防止超极化过冲 |
| Ca tau/sens | 100/0.1 | **120/0.08** | 略慢 Ca 动力学 |

**结果**: SMD V swing 110mV → **49 mV** ✅ (目标 30-50mV)

### 2. Curvature Bias 旁路移除 (simulation_engine.cpp)

**移除的代码** (旧 L993-1023):
```cpp
// 移除: direct curvature bias bypass
double curv_gain = weathervane_gain * 0.06;
double curv_bias = curv_gain * grad_normal * ...;
body_.set_curvature_bias(curv_bias);  // ← 绕过 SMD 神经回路
```

**保留的代码**:
- RIV omega turn 的 curvature_bias（不同机制，生物学正确）
- SMB neck bias（小幅神经信号叠加）

### 3. SMD Weathervane 符号修复

发现 SMD→肌肉→曲率传导链存在符号反转：
- 正 grad_normal（食物在左）→ 应向左转
- SMDD 获得 **-drive**（抑制背侧）→ 延长腹侧 phase → 向左弯曲 ✅
- SMDV 获得 **+drive**（增强腹侧）→ 同样效果 ✅

旧代码符号 (+drive on SMDD) 被 curvature_bias 旁路掩盖，移除旁路后暴露。

### 4. Bias Clamp 校准

| 参数 | 旧值 | 新值 | 理由 |
|------|------|------|------|
| bias_clamp | 50 pA | **5 pA** | SMD 49mV: 5pA→ΔV=4mV→8% 占空比偏移 |

50pA 在 49mV 振荡器上会完全沉默一侧 SMD。
5pA 保持两侧振荡，同时提供有效占空比调制。

### 5. Regtest 基线更新 (regression_test.cpp)

| 指标 | 旧基线 | 新基线 | 容差 | 说明 |
|------|--------|--------|------|------|
| SMDDL V swing | 55 mV | **15 mV** | 80% | 转向时背侧被 weathervane 抑制 |
| SMDVL V swing | 75 mV | **45 mV** | 60% | 腹侧被增强 |
| SMD diff amp | 125 mV | **55 mV** | 60% | 总幅度降低 |
| |I_syn| max | 32 pA | **15 pA** | 电流更小 |
| Omega count | 1 | **4** | 200% | SMD 校准后更多 omega |

## 修改文件列表

| 文件 | 修改内容 |
|------|---------|
| `src/neuron/neuron_factory.cpp` | CCA-1/SLO-1/SHL-1/NCA/EGL-36 电导 + leak + Ca 参数 |
| `src/simulation/simulation_engine.cpp` | 移除 curvature_bias 旁路，修复 SMD 注入符号 |
| `src/simulation/simulation_engine.h` | bias_clamp 50→5 pA |
| `src/simulation/regression_test.cpp` | 更新 5 个 regtest 基线 |

## 验证结果

### Regtest: 17/17 通过 ✅

### 8 种子趋化性测试 (300s, no_toxin)

| seed | CI | near_food |
|------|-----|-----------|
| 42 | 0.595 | 61.9% |
| 100 | 0.199 | 29.3% |
| 200 | 0.561 | 65.3% |
| 300 | 0.086 | 37.7% |
| 400 | 0.141 | 48.7% |
| 500 | 0.183 | 43.6% |
| 600 | 0.441 | 57.4% |
| 700 | -0.313 | 0.0% |
| **均值** | **0.24** | **43.0%** |

- **7/8 种子 CI > 0** (正趋化性)
- **CI 均值 0.24** — 完全从 SMD 半中心振荡器占空比调制涌现
- 对比: Iino 2009 weathervane-only CI ≈ 0.3-0.4

### 架构合规性改进

| 违规 | 状态 | 说明 |
|------|------|------|
| 1.2 Curvature bias 旁路 | **✅ 已修复** | weathervane 完全通过 SMD 神经回路 |
| 1.1 Pirouette Poisson | 待修复 | Step 66 计划 |
| 1.5 Reversal 覆盖 | 待修复 | Step 66 计划 |
| 1.3 Food edge reversal | 待修复 | 后续计划 |
| 1.4 Basal slowing | 待修复 | 后续计划 |

## 待改进

1. **CCA-1 V_half 调制替代电流注入**: 用 CCA-1 激活阈值偏移替代 tonic current，
   可更有效地调制占空比而不压制振荡器（预计 CI 提升至 0.3-0.5）
2. **TA→LGC-55→SMD 抑制过强**: 在 forward run 期间仍有残余 TA，
   贡献 -7.5pA 到 SMDDL，与 weathervane 叠加导致过度抑制
3. **Pirouette 机制移除** (Step 66): ASE→AIB→AVA 涌现 reversal

## 参考文献

- Nicoletti 2019 PLOS One — RMD HH 建模, CCA-1 双稳态分析
- Dobosiewicz 2019 eLife — AIA 双稳态, 2-3pA 切换阈值
- Iino & Yoshida 2009 JNeurosci — Weathervane 机制发现, AIZ 必需
- Tolstenkov 2021 eLife — RIM 双模式 (tyramine/gap junction)
- Steger 2005, Bhatt 2014 — CCA-1 在头部运动神经元中的作用

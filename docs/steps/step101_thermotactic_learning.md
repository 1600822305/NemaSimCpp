# Step 101: 温度趋性学习 — 食物-温度联合记忆

> 日期: 2026-02-13

---

## 动机

C. elegans 能记住与食物配对的温度（培养温度 Tc），在温度梯度上导航到 Tc 附近。这是 C. elegans 最经典的联合学习行为之一（Hedgecock & Russell 1975）。

当前代码（Step 80）已实现 AFD 的 Tc 适应机制，但温度 weathervane 仍使用固定的初始培养温度（22.5°C），导致学习到的 Tc 无法影响导航方向。此外，饥饿时的温度趋性中断机制尚未实现。

## 生物学基础

### Tc 记忆与更新
- **存储位置**: AFD 感觉末梢内的 cGMP 信号级联（Kimura 2004, Biron 2006）
- **更新速度**: ~2 小时（Hedgecock & Russell 1975）
- **机制**: DGK-3（二酰甘油激酶）调控 AFD→AIY 输出阈值的重置速率（Biron 2006）
- **食物门控**: 在食物上 → Tc→当前温度；离开食物 → Tc 停止更新（Chi 2007 J Exp Biol）

### 饥饿中断温度趋性
- **核心发现**: Hawk 2021 eLife — 饥饿不改变 AFD 或 AIY 温度响应
- **机制**: 饥饿 → 肠道 INS-1↑ → DAF-16/FOXO 在 AWC → AWC 温度响应增强
- **回路**: AWC⊣AIA（谷氨酸能抑制，Kakaria 2019）→ AIA 不能抑制转弯 → 温度趋性中断
- **进食时**: AWC 温度响应低 → AIA tonic → AFD 驱动的温度趋性正常工作
- **饥饿时**: AWC 温度响应高 → AIA 被抑制 → 更多转弯 → 温度趋性中断

## 实现

### Fix 1: Weathervane 使用学习后的 Tc

**文件**: `apply_motor_control.cpp:132`

```
double tc = cultivation_temp_;  // 旧: 固定 22.5°C
double tc = learned_tc();       // 新: 使用 ThermoTransducer 的适应值
```

这使得：
- 在食物 A 处（温度 15°C）进食 → Tc 适应到 ~15°C
- weathervane 转向 15°C 方向（而非固定的 22.5°C）
- AFD klinokinesis 已经使用内部 tc_（Step 80），现在 weathervane 同步

### Feature 2: AWC 饥饿依赖温度响应

**文件**: `apply_sensory_systems.cpp:236-259`

```
starve_factor = sigmoid(satiety < 0.3)  // 饱食→0, 饥饿→1
I_awc_temp = 8.0 × |T - Tc| × starve_factor  // cap 25pA
→ AWC 神经元 add_synaptic_current(I_awc_temp)
```

信号链：
```
饥饿 → starve_factor ≈ 1.0
  → AWC 对 |T-Tc| 敏感 → AWC 激活
  → AWC⊣AIA (已有抑制突触, Kakaria 2019)
  → AIA 被抑制 → 不能抑制转弯
  → AFD-driven 温度趋性中断
```

无需新增神经元或突触 — 利用已有 AWC→AIA 抑制连接。

## 行为预期

| 条件 | 预期行为 | 机制 |
|------|---------|------|
| 20°C 喂食 → 15-25°C 梯度 (饱) | 趋向 20°C | AFD klinokinesis + weathervane → Tc=20°C |
| 15°C 喂食 → 15-25°C 梯度 (饱) | 趋向 15°C | Tc 更新到 ~15°C (adapt_tc) |
| 20°C 喂食 → 饥饿 → 梯度 | 随机运动 | AWC 温度响应↑ → AIA 抑制 → 温度趋性中断 |
| 刚从食物离开 (高 satiety) | 保持温度趋性 | satiety > 0.3 → starve_factor ≈ 0 |

## 参数

| 参数 | 值 | 来源 |
|------|-----|------|
| `awc_temp_gain` | 8.0 pA/°C | 调参：|T-Tc|=2°C 时 16pA 足以激活 AWC |
| `starve_factor` sigmoid midpoint | satiety = 0.3 | Hawk 2021: "prolonged" food deprivation (~2hr) |
| `starve_factor` sigmoid slope | 12.0 | 快速切换，模拟延长饥饿的阈值效应 |
| `I_awc_temp` cap | 25.0 pA | 防止过驱动 |

## 修改文件

- `src/simulation/apply_motor_control.cpp`: weathervane Tc → learned_tc()
- `src/simulation/apply_sensory_systems.cpp`: AWC 饥饿依赖温度响应

## 参考文献

- Hedgecock & Russell 1975 PNAS — 温度趋性联合学习
- Biron 2006 J Neurosci — AFD 感觉末梢可塑性 + DGK-3
- Hawk 2021 eLife — 饥饿通过 AWC-AIA 功能重配置中断温度趋性
- Chi 2007 J Exp Biol — 食物和温度独立影响温度趋性
- Kodama 2006 — INS-1/DAF-2 介导饥饿-温度关联
- Kakaria 2019 eLife — AIA AND-gate, AWC⊣AIA 抑制
- Chalasani 2010 — INS-1 从 AIA 调制 AWC

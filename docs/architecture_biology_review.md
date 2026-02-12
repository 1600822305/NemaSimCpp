# C. elegans 仿真架构生物学合规审查

> 审查日期: 2026-02-12
> 审查范围: 全项目架构 (162 神经元, 63 Steps)
> 对照标准: [`docs/00_design_principles.md`](00_design_principles.md) 第二节 "禁止硬编码的内容"

---

## 评级体系

| 等级 | 含义 |
|------|------|
| 🔴 **违规** | 明确违反设计原则——行为逻辑硬编码，绕过神经回路 |
| 🟡 **灰色地带** | 生物学占位符——有物理依据但跳过了中间环节 |
| 🟢 **合规** | 因果链完整——从离子通道到行为全链条涌现 |
| ⚪ **缺失** | 生物学上存在但尚未实现的组件 |

---

## 一、🔴 严重违规：行为逻辑绕过神经回路

### 1.1 Pirouette 决策系统 — Poisson 过程替代神经回路

**位置**: [`SimulationEngine::step()`](../src/simulation/simulation_engine.cpp:600) 的 pirouette 判断逻辑

**问题**: Pirouette（转弯）的**启动时机**由 `dCdt_filtered_` → Poisson 随机过程决定，而非由 ASE→AIB→AVA 突触通路的动力学涌现。代码注释明确承认：
> *"The pirouette Poisson process is a decision-layer shortcut (bypasses AVA circuit for WHEN to reverse)"*

**违反原则**: 
- ❌ 设计原则 2.2: *"觅食行为 — 自发活动 + 感觉反馈 + 命令中间神经元噪声 → 运动轨迹涌现"*
- ❌ 设计原则 2.2: *"前进/后退切换 — ALM→AVD→AVA 突触通路激活→A 类运动神经元接管"*

**影响**: 这是项目中最严重的作弊——趋化性的核心机制（pirouette 频率的梯度调制）不是从连接组中涌现的，而是由 `simulation_engine.cpp` 中的 if-else 逻辑直接控制。AVA 的膜电位动态被完全绕过。

**生物学正确做法**: 
- ASE ON/OFF 信号 → AIA 抑制/释放 AIB → AIB 去极化 → AVA 突触激活 → AVA 膜电位超过阈值 → reversal 自然发生
- dC/dt 信息通过 ASE 感觉转导的 fast/slow 双滤波器编码在 ASE 活性中，应通过突触传导到 AVA，而非在引擎层面做 Poisson 采样

**修复优先级**: **P0** — 阻断验证（消融测试结果将不准确）

---

### 1.2 曲率偏置旁路 — 绕过 SMD 神经振荡器

**位置**: [`SimulationEngine::apply_weathervane()`](../src/simulation/simulation_engine.cpp:993-1023)

**问题**: Weathervane（风向标趋化）通过 `body_.set_curvature_bias(curv_bias)` 直接操纵身体曲率，完全绕过 SMD 半中心振荡器。代码注释：
> *"Direct curvature bias: bypass SMD oscillator bottleneck (110mV amplitude drowns ±24pA bias)"*

**违反原则**:
- ❌ 设计原则 2.2: *"转向决策 — 头部感觉采样不对称 → RIA/SMD 差异激活 → 头部弯曲"*
- ❌ 设计原则 2.3: *"❌ 错误: 直接操纵身体段曲率/角度来产生运动"*

**影响**: CI（趋化指数）的 ~80% 贡献来自这个旁路（Step 17 诊断: SMD 路径 CI=0.07, 加旁路 CI=0.76）。意味着趋化性本质上是 `simulation_engine.cpp` 计算梯度然后直接弯曲身体，不是神经回路控制的。

**根因**: SMD 半中心振荡器振幅过大（110mV peak-to-peak），几 pA 的 weathervane 偏置无法改变占空比。这说明 SMD 神经元模型参数需要校准，而不是绕过它。

**生物学正确做法**:
- 校准 SMD 神经元的 CCA-1/SHL-1 通道参数，使振荡幅度降至 ~30-50mV（生物学合理范围）
- 或改用多隔室 SMD 模型，让 weathervane 信号注入到 SMD 的不同隔室
- 确保 ±5pA 的 weathervane 偏置能产生可测量的占空比变化

**修复优先级**: **P0** — 这是 CI 的主要来源

---

### 1.3 食物边缘反转 — 概率公式替代神经回路

**位置**: [`SimulationEngine::step()`](../src/simulation/simulation_engine.cpp:600) 中的 head poke reversal 逻辑（Step 47/54）

**问题**: 当头部从食物区出来时，以概率 `p = 0.50 + 0.30×5HT - 0.30×PDF` 触发反转。这是一个行为规则，不是神经回路的涌现结果。

**违反原则**:
- ❌ 设计原则 2.2: *"前进/后退切换 — 突触通路激活→A类运动神经元接管"*
- ❌ 设计原则 7.2: *"禁止: 使用 if-else 分支实现行为切换"*

**生物学正确做法**: 
- 头部离开食物 → NSM/CEP 驱动下降 → 5-HT/DA 浓度降低 → AIY 去抑制 → AVA 相对增强 → reversal 自然发生
- 概率调制应通过 5-HT/PDF 对 AVA/AVB 的调质效应涌现

**修复优先级**: **P1**

---

### 1.4 Basal Slowing — 直接速度乘法替代 DA 神经回路

**位置**: [`SimulationEngine::step()`](../src/simulation/simulation_engine.cpp:544-552)

**问题**: 食物上减速通过 `effective_speed *= basal_slow` 直接乘法实现，绕过了多巴胺从 CEP→DOP-3→运动神经元的完整通路。代码承认：
> *"NOT through CEP's synaptic circuit... on_lawn sigmoid directly modulates speed"*

**违反原则**:
- ❌ 设计原则 2.2: *"速度调节 — 感觉输入→中间神经元→AVB/AVA 活性变化→运动波能量变化"*
- ❌ 设计原则 5.4: *"❌ 作弊: motor.output = desired_velocity"*

**根因**: CEP 高电流（40pA）通过 CEP↔OLQ gap junction 导致级联效应。这说明 gap junction 电导或 CEP 增益需要校准。

**修复优先级**: **P1**

---

### 1.5 Reversal 运动执行 — 直接设置运动状态

**位置**: [`SimulationEngine::step()`](../src/simulation/simulation_engine.cpp:607)

**问题**: `body_.set_locomotion_state(0.0, 1.0)` 强制后退，完全绕过 AVA/AVB 命令中间神经元的膜电位动态。

**违反原则**:
- ❌ 设计原则 2.2: *"前进/后退切换 — ALM→AVD→AVA 突触通路激活→A 类运动神经元接管"*
- ❌ 设计原则 5.4: *"❌ 作弊: motor.output = desired_velocity"*

**影响**: AVA/AVB 的突触输入和膜电位动力学在 reversal 期间被忽略。消融 AVA 不会真正消除后退能力（因为 pirouette 系统直接设置了运动状态）。

**修复优先级**: **P0** — 导致消融测试不准确

---

## 二、🟡 灰色地带：生物学占位符

### 2.1 疲劳/睡眠状态变量

**位置**: [`SimulationEngine::update_fatigue()`](../src/simulation/update_internal_states.cpp:104)

**问题**: `fatigue_` 是一个基于运动速度累积的标量变量，然后作为外部电流注入 RIS。真实生物学中，睡眠压力通过腺苷累积、Ca²⁺ 稳态、neuropeptide 反馈等复杂机制实现。

**评估**: 部分合理——RIS 确实是睡眠启动的关键神经元，但"疲劳度"不应从引擎层面计算并注入。

**改进方向**: 让活动→神经肽/代谢物累积→通过受体调制 RIS 兴奋性

### 2.2 饱食度效应的直接注入

**位置**: [`SimulationEngine::update_satiety()`](../src/simulation/update_internal_states.cpp:20)

**问题**: `satiety_` 对 RIC（+10pA）和 ASE/AWC（-5pA）的效应通过直接电流注入实现，而不是通过胰岛素/TGF-β 等内分泌信号通路。

**评估**: 泵驱动的饱食度计算本身是好的（Step 24），但下游效应的实现方式跳过了 DAF-2、INS-1 等分子机制。Step 63 部分修复了 INS-1，但仍然是公式计算。

### 2.3 Sickness → ADF 5-HT 直接注入

**位置**: [`SimulationEngine::step()`](../src/simulation/simulation_engine.cpp:424-436)

**问题**: `sickness_ × mod1_aiy_gain_` 直接注入 AIY/AIZ，而不是通过 ADF→5-HT 释放→MOD-1 Cl⁻ 通道的完整因果链。代码注释解释了为什么不用突触（ADF 基线释放会膨胀 off-food 5-HT），但正确做法应该是修复 ADF 的释放阈值参数，而非绕过突触通路。

### 2.4 DMP 排便定时器

**位置**: [`SimulationEngine::update_defecation()`](../src/simulation/update_internal_states.cpp:199)

**问题**: 使用 `dmp_timer_` 软件定时器模拟 45s 肠道 Ca²⁺ 振荡器。这是非神经性定时器（IP3/ITR-1），不属于神经系统仿真范畴，但直接注入 AVL/DVB 70pA 的方式过于粗暴。

### 2.5 O₂/CO₂ 从食物密度公式推导

**位置**: [`SimulationEngine::apply_touch_stimulus()`](../src/simulation/simulation_engine.cpp:1280-1400)

**问题**: O₂ = 21% - 13% × food_density, CO₂ = 0.04% + 3% × food_density。真实世界中 O₂/CO₂ 应该有独立的扩散场，受到细菌代谢和环境通风的动态影响。当前实现与食物场完全耦合，没有独立动力学。

### 2.6 NPR-1 作为常数抑制

**位置**: [`SimulationEngine::apply_touch_stimulus()`](../src/simulation/simulation_engine.cpp:1303)

**问题**: `npr1_tonic_ = -28.0` 是一个常数，但 NPR-1 受体的激活应取决于其配体（FLP-18, FLP-21 神经肽）的浓度，而这些神经肽的释放又取决于社交/O₂ 状态。

### 2.7 神经调质作为全局标量

**位置**: [`NeuromodulationManager`](../src/neuromodulation/neuromodulation.h:53)

**问题**: 每种神经调质是一个单一的 `double concentration`。真实的体积传递是空间依赖的——5-HT 从 NSM 释放后在局部浓度最高，远处浓度较低。当前实现中所有靶标神经元"看到"相同浓度，相当于假设瞬间完全混合。

---

## 三、🔴 架构级问题

### 3.1 SimulationEngine 上帝类

**位置**: [`simulation_engine.h`](../src/simulation/simulation_engine.h:1-375)

**问题**: SimulationEngine 有 **50+ 成员变量** 和 **30+ 方法**，混合了:
- 神经回路更新逻辑
- 行为状态机 (pirouette, omega, reversal, sleep, DMP, egg-laying)
- 感觉转导
- 内部状态计算 (satiety, sickness, food_memory, fatigue, INS-1)
- 身体物理接口
- 环境采样

这使得区分"什么是生物物理仿真"和"什么是人工控制逻辑"变得极其困难。每次添加新功能都必须在这个巨大的类中添加新的状态变量和方法。

**建议**: 将行为状态机（pirouette, omega 等）移除——如果神经回路正确，这些行为应该自然涌现，不需要在引擎中用状态变量跟踪。

### 3.2 因果链断裂：外部电流注入泛滥

全项目中有 **20+ 处** `set_external_current()` 和 `add_synaptic_current()` 调用不是来自突触或感觉转导，而是来自引擎层面的状态变量计算。这意味着信息**绕过了连接组**到达了神经元。

按设计原则检验 4（无"上帝视角"）:
> *"❌ 作弊: neuron.receive(global_food_position)"*

当前代码中:
- `satiety_` → RIC: 神经元收到了一个来自"全局饱食度"的电流
- `sickness_` → AIY/AIZ: 神经元收到了一个来自"全局疾病状态"的电流
- `food_memory_` → AVA: 神经元收到了一个来自"全局食物记忆"的电流
- `fatigue_` → RIS: 神经元收到了一个来自"全局疲劳度"的电流
- `ins1_conc_` → AWC/AIA/AIY: 神经元收到了来自公式计算的胰岛素信号

这些都是"上帝视角"信息——真实神经元只能通过突触输入获知这些状态。

### 3.3 身体模型缺乏力学闭环

**位置**: [`BodyModel::update_positions()`](../src/body/body_model.cpp:78-155)

**问题**:
1. **速度公式**: `forward_speed = v_max × muscle_work` 是一个经验公式，不是力学方程。真实线虫速度 = ΣF_推进 / 阻力系数，推进力来自曲率波与基底的各向异性摩擦交互。
2. **运动学而非动力学**: 身体段的位置是从头部角度推导的（θ_i = θ_{i-1} - κ_i × ds），没有牛顿力学（F=ma）。肌肉产生力→身体形变→与环境交互产生推进力的完整链条被简化为 `curvature → speed`。
3. **没有质量/惯性**: 虽然低雷诺数下惯性确实可忽略，但完整的阻力力理论（RFT）需要计算每个体段的切向/法向阻力分量。

**技术债务 TD-04** 已记录此问题但尚未解决。

---

## 四、🟢 架构优点（符合生物学的部分）

### 4.1 离子通道多样性 ✅
14 种已知 C. elegans 离子通道，参数来自 Nicoletti 2019。Boltzmann 稳态 + 指数松弛框架正确。每种通道有独立的 `IonChannel` 子类。

### 4.2 HH 型分级电位模型 ✅
C_m·dV/dt = -(I_leak + ΣI_ion) + I_syn + I_ext。正确实现了 C. elegans 的分级电位（非脉冲）信号编码。

### 4.3 连接组忠实性 ✅
基于 Cook 2019 / Emmons 2024 EM 数据。突触权重 ∝ EM 切面数。Step 42 进行了校准。

### 4.4 SMD 半中心振荡器 ✅
CCA-1 T-type Ca²⁺ burst + SLO-1 BK 适应 + 背腹交叉抑制 → 自主头部振荡。这是正确的涌现机制。

### 4.5 RIA 多隔室模型 ✅
Step 28: 3 隔室（soma + nrV + nrD）实现亚细胞钙信号，符合 Hendricks 2012 实验数据。

### 4.6 RIV-Driven Omega Turn ✅
Step 31: 从 TA 门控涌现，替代了之前的硬编码。CCA-1 burst → omega。正确。

### 4.7 Tsodyks-Markram 突触可塑性 ✅
所有化学突触带 STD/STF，参数按回路分类。Tap 习惯化从 STP 涌现。

### 4.8 咽部泵食系统 ✅
MC/M3 驱动的咽部 AP → 真实进食 → satiety。替代了占位符。

### 4.9 本体感觉波传播 ✅
Step 29: B 类顺序感知前一单元领地（Wen 2012），D/V 交替接力产生 S 波。

---

## 五、⚪ 关键缺失组件

| 组件 | 生物学重要性 | 对行为影响 |
|------|------------|----------|
| **RMG Hub 神经元** | 社交/独居行为核心 | NPR-1 效应无法正确建模 |
| **AIB→AVA 突触的正确权重** | Pirouette 频率调制 | 是修复 1.1 的前提 |
| **完整 302 神经元** | 140 个缺失 | 中间神经元网络不完整 |
| **突触外整流 gap junction** | 部分 gap junction 是电压依赖的 | 影响 AVA-AVE 等耦合 |
| **神经肽 250+ 种** | 完整无线连接组 | 只实现了 NLP-12/PDF/FLP-11/INS-1 |
| **肌肉作为计算节点** | Emmons 2024 发现 | 缺少肌肉→神经元反馈 |
| **完整 RFT 身体物理** | 速度/波形精确性 | TD-04 |

---

## 六、修复路线图建议

### Phase 1: 消除红线违规 (P0)

1. **修复 SMD 振幅**: 校准 CCA-1/SHL-1 参数使振幅降至 30-50mV，让 weathervane ±5pA 偏置能改变占空比。移除 `curvature_bias_` 旁路。
2. **移除 Pirouette Poisson 过程**: 让 ASE→AIA⊣AIB→AVA 的突触通路自然产生 reversal。调整 AIB→AVA 突触权重使 reversal 频率匹配文献。
3. **移除 `set_locomotion_state(0,1)` 强制后退**: 让 AVA 膜电位动态自然控制 A 类运动神经元。
4. **消融测试验证**: AVA 消融 → 不能后退, ASE 消融 → CI 丧失。

### Phase 2: 替换灰色地带 (P1)

5. **Basal slowing**: 校准 CEP 增益和 CEP↔OLQ gap junction 电导，让 DA→DOP-3 通过神经调质层正确实现减速。
6. **Food edge reversal**: 让头部离开食物 → NSM 活性下降 → 5-HT 浓度降低的动态过程自然驱动 reversal。
7. **内部状态去中心化**: 将 satiety/sickness/fatigue 的效应通过神经肽/调质通路实现，而非直接电流注入。

### Phase 3: 架构重构

8. **SimulationEngine 瘦身**: 移除行为状态机（pirouette/omega/reversal），这些应从回路涌现。
9. **空间化神经调质**: 给每种调质添加空间扩散模型，而非全局标量。
10. **力学身体模型**: 实现完整 RFT 或升级到 SPH。

---

## 七、总结

| 类别 | 数量 | 占比 |
|------|------|------|
| 🔴 严重违规 | 5 | 行为的核心控制逻辑 |
| 🟡 灰色地带 | 7 | 物理依据有但跳过中间环节 |
| 🟢 合规 | 9 | 因果链完整的涌现机制 |
| ⚪ 缺失 | 7+ | 已知但未实现的生物学组件 |

**核心矛盾**: 项目的设计原则（`00_design_principles.md`）写得极好——明确禁止行为脚本和 if-else 切换。但实际实现中，趋化性（CI 的主要贡献）和运动方向切换（reversal）这两个最核心的行为都依赖于旁路/硬编码，而非从连接组涌现。

**根因分析**: 最可能的原因是 SMD 半中心振荡器的振幅过大（110mV），导致突触级别的调制信号被淹没。修复 SMD 振幅参数可能是解决多个下游问题的关键（修复 weathervane 旁路、使 RIA→SMD 调制有效、使 pirouette 的 AIB→AVA 通路能正确控制运动）。

> **一句话**: 项目的"骨骼"（连接组+离子通道+HH方程）是正确的，但"肌肉"（行为的实际产生）很大程度上仍由引擎层面的控制逻辑驱动，而非从神经回路动力学中涌现。
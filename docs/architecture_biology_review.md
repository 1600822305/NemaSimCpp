# C. elegans 仿真项目 — 架构生物学审查报告

> 审查日期: 2026-02-12
> 审查范围: 全部源码 + 68 步开发进度
> 审查标准: `docs/00_design_principles.md` 中定义的 P0/P1 规则

---

## 〇、总体评价

本项目在 68 步迭代中从零构建了一个 162 神经元的秀丽隐杆线虫仿真系统，包含 14 种离子通道、分级突触、多隔室神经元、6 种神经调质、咽部 CPG、睡眠系统等。Step 65-68 显示了系统性的"去硬编码"清理工作（移除 Pirouette Poisson、curvature_bias 旁路、basal slowing 直接乘法），表明项目方向正确。

但仍存在 **6 个 P0 级违规**（行为绕过神经回路）和 **12 个 P1 级违规**（生物学可疑的快捷方式），以及若干架构层面的关注点。

---

## 一、P0 违规 — 行为绕过神经回路（必须修复）

### P0-1: Weathervane 使用"上帝视角"梯度计算

**位置**: [`apply_weathervane()`](src/simulation/simulation_engine.cpp:901)

**问题**: 代码直接调用 `environment_.chemical_field().gradient(head_pos)` 计算数学梯度，然后将差异电流注入 SMD 神经元。

```
❌ 环境梯度 → [数学运算] → SMD 注入
✅ 头部摆动采样 → ASE 浓度时间序列 → AIY/AIZ → SMB/SMD 突触传递
```

线虫**没有梯度传感器**。Weathervane 转向（Iino & Yoshida 2009）的真实机制是：头部左右摆动 → 感觉神经元交替采样不同位置 → 时间序列编码空间梯度 → 下游回路提取方向信息。

**严重性**: 高。这是趋化性的核心机制之一，直接使用全局梯度信息违反了"无上帝视角"原则（设计原则 §4.4）。虽然 Step 65 移除了 curvature_bias 旁路改为 SMD 注入，但信息来源仍然是全局梯度。

**修复方向**: 让 ASE/AWC 仅通过 `sample_pos`（已受头部曲率影响）采样浓度，下游的 RIA 多隔室 Ca²⁺ 差异（Step 28 已实现）+ SMD 占空比调制应自然涌现出 weathervane 行为。移除 `apply_weathervane()` 中的直接 SMD 注入。

---

### P0-2: 温度 Weathervane 同样使用全局梯度

**位置**: [`apply_weathervane()`](src/simulation/simulation_engine.cpp:963-974)

**问题**: `environment_.temperature_gradient(head_pos)` + `temp_sign = (temp_at_head > tc) ? -1.0 : 1.0` → 直接计算转向方向。

```
❌ 全局温度梯度 + sign(T-Tc) → SMD 注入
✅ AFD 对温度变化的响应 → AIY/AIZ → RIA → SMD 突触调制
```

线虫的热趋性机制是 AFD 感觉神经元通过 dT/dt 响应结合培养温度记忆来编码方向（Mori 1995, Clark 2006），不是通过直接计算梯度法向分量。

---

### P0-3: RIV Omega 转弯通过 curvature_bias 直接操控身体

**位置**: [`apply_riv_omega()`](src/simulation/simulation_engine.cpp:1620-1631)

**问题**: `body_.set_curvature_bias(bias)` 直接设置身体曲率偏置，绕过了 RIV→肌肉 的物理通路。代码注释（[motor_controller.cpp:91-95](src/motor/motor_controller.cpp:91)）明确承认 RIV 没有映射到运动控制器。

```
❌ RIV 释放率 → [直接] → body.curvature_bias
✅ RIV 释放率 → RIV→ventral head muscle → 肌肉差异激活 → 曲率
```

RIV 是 GABA 能运动神经元，其生物学功能是投射到**腹侧头部肌肉**（Gray 2005 PNAS），抑制对侧同时激活同侧。应该像 SMD 一样通过 motor_controller 映射到肌肉段。

**修复方向**: 在 `MotorController` 中添加 RIV→ventral seg 0-4 映射，增大 muscle_gain 或为 omega 设置独立的增益参数。

---

### P0-4: Food Edge Reversal 使用"上帝视角"食物检测

**位置**: [`simulation_engine.cpp:1529-1560`](src/simulation/simulation_engine.cpp:1529)

**问题**:
1. `food_at_head = environment_.sample_food_density(...)` — 直接检测食物密度
2. `was_on_lawn_` latch 检测器 — 行为级逻辑
3. `p_edge_rev = 0.50 + 0.30 * sht - 0.30 * pdf` — 概率公式（if-else 的连续版本）
4. 40pA 直接注入 AVA

```
❌ 食物密度 → [阈值比较] → [概率公式] → AVA 注入
✅ CEP/ADE 机械感觉 → DA 释放 → DOP-3→AVA + DA 信号突然下降 → DA-dependent 回路
```

真实机制是：线虫离开食物 → CEP 不再检测到细菌纹理 → DA 释放骤降 → DOP-1/DOP-3 对 AVA/AVB 的调制改变 → 产生 reversal。

---

### P0-5: DMP 通过 speed_factor 直接操控速度

**位置**: [`update_defecation()`](src/simulation/update_internal_states.cpp:248-270)

**问题**: `dmp_speed_factor_ = 0.6/0.7/0.5` 直接乘以 effective_speed。排便期间的运动减速应该从 AVL/DVB 对运动神经元的 GABA 抑制中涌现。

```
❌ DMP 阶段 → speed_factor = 0.6
✅ AVL/DVB GABA 释放 → 突触抑制 B/A-class MN → 肌肉工作减少 → 速度自然下降
```

AVL↔DD05 的 gap junction 已存在于连接组中，应该通过这个通路传递抑制。

---

### P0-6: FLP-11 睡眠抑制通过直接电流注入而非神经调质框架

**位置**: [`apply_sleep_effects()`](src/simulation/update_internal_states.cpp:142-186)

**问题**: FLP-11 是神经肽，但其效应通过直接 `add_synaptic_current(-15/-12/-20/-30 pA)` 实现，绕过了项目已有的 NeuromodulationManager 框架。

```
❌ RIS V → sigmoid → 直接注入 AVA/AVB/MC/SMD/DA 等
✅ RIS → FLP-11 释放 → NeuromodulationManager → 受体介导效应
```

项目已有 6 种神经调质的完整框架（5-HT/DA/OA/TA/NLP-12/PDF），FLP-11 应作为第 7 种加入，保持一致性。

---

## 二、P1 违规 — 生物学可疑的快捷方式（应改进）

### P1-1: Satiety 直接调制化学感觉增益（双重作用）

**位置**: [`apply_sensory_input()`](src/simulation/simulation_engine.cpp:663-664)

```cpp
double chemo_sat_gain = 1.0 - 0.85 * sat_switch;  // 直接乘到感觉电流上
```

饱食度(satiety_)是一个内部体液状态，感觉神经元无法直接"知道"它。真实机制是通过 INS-1/DAF-2 信号通路调制 AWC/AIA/AIY（Step 63 已实现）。当前存在**双重调制**：既有直接增益抑制，又有 INS-1 通路抑制。

**修复**: 移除直接 `chemo_sat_gain` 乘法，仅保留 INS-1 → DAF-2 通路。

---

### P1-2: Sickness 直接抑制化学感觉（双重作用）

**位置**: [`apply_sensory_input()`](src/simulation/simulation_engine.cpp:669)

```cpp
double sick_suppression = 1.0 - 0.85 * sickness_;
```

与 P1-1 类似，sickness_ 是内部状态，不应直接乘到感觉电流上。应通过 ADF 5-HT + INS-1 + AWC 突触翻转等已实现的神经通路传递。

---

### P1-3: ADF 由 sickness_ 内部变量直接驱动

**位置**: [`apply_sensory_input()`](src/simulation/simulation_engine.cpp:715)

```cpp
double I_adf = 0.5 + 30.0 * sickness_;
```

ADF 是化学感觉神经元，应该通过化学检测来响应病原体信号。直接从 `sickness_` 注入电流相当于给 ADF "上帝视角"访问体内疾病状态。

真实机制：ADF 通过 TPH-1 上调（需要数小时的转录调控）来增加 5-HT 释放能力（Zhang 2005 Nature）。这是一个基因表达层面的变化，时间尺度远慢于当前实现的即时响应。

---

### P1-4: RIC 由 satiety_ 直接驱动

**位置**: [`update_satiety()`](src/simulation/update_internal_states.cpp:32-38)

```cpp
double ric_satiety = 10.0 * satiety_;
neurons_[rid]->add_synaptic_current(ric_baseline + ric_satiety);
```

RIC 是中间神经元/运动神经元（章鱼胺能），不是感觉神经元。它无法直接感知饱食度。应通过 NSM 5-HT → RIC 的突触/调质通路传递进食状态信息。

---

### P1-5: food_memory_ (DARPP-32) → AVA 直接注入

**位置**: [`update_food_memory()`](src/simulation/update_internal_states.cpp:75-78)

```cpp
double ars_current = 1.5 * food_memory_;
neurons_[nid("AVAL")]->add_synaptic_current(ars_current);
```

DARPP-32 磷酸化在生物学上调制的是 GLR-1 受体的突触后增益（Hills 2004），不是给 AVA 添加持续电流。应实现为：food_memory_ → AVA 上的 GLR-1 突触增益增强 → ASE→AIB→AVA 通路的响应更强。

---

### P1-6: gradient klinokinesis 使用全局梯度信息

**位置**: [`apply_gradient_klinokinesis()`](src/simulation/update_internal_states.cpp:88-99)

```cpp
Vector2d grad = environment_.chemical_field().gradient(head_pos);
double no_signal_factor = fast_exp(-grad_mag / 0.002);
```

AVA 不可能"知道"化学梯度强度。无梯度→高 reversal 的行为应从感觉输入的统计特性中涌现（信号方差低 → 随机 AVA 激活更频繁）。

---

### P1-7: AWB 由 sickness_ × repellent 驱动

**位置**: [`apply_sensory_input()`](src/simulation/simulation_engine.cpp:733)

```cpp
double I_awb = 2.0 + awb_pathogen_gain_ * sickness_ * repellent;
```

AWB 是嗅觉感觉神经元，它检测排斥性化学物质（如 serrawettin）。乘以 `sickness_` 给了 AWB 访问内部疾病状态的能力，这违反了"上帝视角"原则。

真实机制：学习后 AWB 的敏感度变化是通过**突触权重改变**实现的（Ha 2010 Neuron），而不是通过改变感觉输入增益。

---

### P1-8: INS-1 作为"虚拟调质"无神经元来源

**位置**: [`simulation_engine.h:282-289`](src/simulation/simulation_engine.h:282)

```cpp
double ins1_conc_ = 0.0;  // 直接从 satiety/sickness 计算
```

INS-1 应由 ASI/AIA 神经元释放（Lin 2010），但当前实现直接从 satiety_ 和 sickness_ 状态变量计算浓度，没有经过任何神经元。应将 INS-1 加入 NeuromodulationManager 框架，以 ASI 为源神经元。

---

### P1-9: head_tonic_ 固定 3pA 常量

**位置**: [`apply_head_tonic()`](src/simulation/simulation_engine.cpp:849-870)

```cpp
double tonic = head_tonic_;  // = 3.0 pA
```

所有 SMD/RMD 头部运动神经元接收固定 3pA 驱动。这应来自上游中间神经元（RIA/RIB）的突触输入——这些突触已存在于连接组中。移除此常量后，头部振荡的启动应完全由连接组突触驱动。

---

### P1-10: 饱食度直接抑制 ASE/AWC 突触电流

**位置**: [`update_satiety()`](src/simulation/update_internal_states.cpp:41-51)

```cpp
double suppress = -5.0 * (satiety_ - 0.3) / 0.7;
neurons_[nid]->add_synaptic_current(suppress);
```

与 P1-1 构成第三重饱食度调制。化学感觉抑制应仅通过 INS-1/DAF-2 通路。

---

### P1-11: 产卵系统中 VC 电流直接注入

**位置**: [`simulation_engine.cpp:1501-1506`](src/simulation/simulation_engine.cpp:1501)

```cpp
neurons_[id]->set_external_current(15.0);  // "5-HT potentiation"
```

注释称这是"5-HT potentiation"，但实现为直接电流注入。应通过 HSN→5-HT→SER-1/SER-7→VC 的神经调质通路实现。

---

### P1-12: ADF sickness → MOD-1 ⊣ AIY/AIZ 通过直接注入

**位置**: [`simulation_engine.cpp:417-436`](src/simulation/simulation_engine.cpp:417)

```cpp
double I_mod1 = mod1_aiy_gain_ * sickness_;
neurons_[aiy_id]->add_synaptic_current(I_mod1);
```

注释解释了不用突触的原因（ADF 基线释放干扰），但更好的方案是在 ADF 5-HT 释放加入学习依赖的门控：只有 sickness > 阈值时 ADF 才是有效的 5-HT 源。

---

## 三、架构层面关注点

### A-1: SimulationEngine 仍是"上帝类"

尽管 Step 50 做了拆分，`SimulationEngine` 仍然管理着：
- 感觉转导（5 种模态 × 多种映射结构）
- 运动状态检测（reversal/omega）
- 6+ 种内部状态（satiety/sickness/food_memory/fatigue/egg_pressure/ins1）
- 3 种学习系统（盐学习/病原体学习/STP 习惯化）
- 所有直接注入（~20 处 `add_synaptic_current` 调用）

**建议**: 将感觉转导、内部状态、学习系统分离为独立类，减少 SimulationEngine 职责。

### A-2: 信号路由混乱 — 突触 vs 直接注入

当前系统中，信号通过三种方式传递：
1. **连接组突触** (`connectome_.compute_synaptic_currents`) — 正确方式
2. **直接电流注入** (`add_synaptic_current`) — 约 20 处旁路
3. **神经调质框架** (`NeuromodulationManager`) — 6 种调质

问题是类型 2 和类型 1/3 共存，导致因果链断裂，难以追踪信号流。

**建议**: 尽量将类型 2 转化为类型 1（连接组突触）或类型 3（神经调质靶标）。

### A-3: 2D 身体模型中背腹/左右混淆

2D 弹性杆模型中，"dorsal/ventral" 映射到 2D 平面的曲率方向。但真实线虫在二维平面上爬行时，dorsal 和 ventral **分别朝上和朝下**（贴地面），左右摆动实际上对应身体的左右侧肌肉交替收缩（**不是**背腹）。

Step 65 发现的 SMD→肌肉→曲率符号反转问题可能部分源于此。这个映射假设需要明确文档化。

### A-4: 162/302 神经元覆盖率

当前 162 个神经元覆盖了 53.6% 的体细胞神经元。关键缺失：
- **RMG**: hub-and-spoke 社交行为枢纽（NPR-1 通路核心）
- **AVG/PVQ**: 长程腹索中间神经元
- **FLP/PHA/PHB**: 后体感觉神经元
- **SAA/SIA/SIB**: 头部中间神经元
- **完整的 VB/VA/DB/DA 各 11-13 个**: 当前仅 5-7 个

缺失神经元的功能可能被错误地归因到现有神经元上（如 RIV 的 omega 功能部分代偿了缺失的 SAA/SIA 头部回路）。

### A-5: 感觉转导模型中的物种特异性

CEP/ADE/PDE（多巴胺神经元）被实现为化学感觉转导器（TONIC 类型，采样 food_density），但它们实际上是**机械感觉神经元**——检测细菌的物理纹理而非化学信号。用 `ChemoTransducer` + `sample_food_density()` 模拟机械感觉在概念上是混淆的。

---

## 四、做得好的方面

| 方面 | 评价 |
|------|------|
| **离子通道生物物理** | 14 种通道，Boltzmann 动力学，来源 Nicoletti 2019 ✅ |
| **分级突触** | 正确反映 C. elegans 特性（非脉冲） ✅ |
| **连接组数据** | Cook 2019 + Emmons 2024 校准 ✅ |
| **STP per-circuit 调参** | CPG/touch/sensory 各有合适的时间常数 ✅ |
| **神经调质框架** | 6 种调质、受体特异性效应、合理的时间尺度 ✅ |
| **多隔室 RIA** | 亚细胞 Ca²⁺ 编码，匹配 Hendricks 2012 ✅ |
| **消融验证** | AVA=0 reversals、ASE CI↓、RIM rev↑ 匹配文献 ✅✅ |
| **系统性去硬编码** | Step 65-68 移除了 4 个旁路/直接乘法 ✅ |
| **设计原则文档** | 明确的反作弊框架 + 占位符标注规范 ✅ |
| **回归测试** | 17 指标自动检测 + 电流溯源 ✅ |

---

## 五、优先修复建议

### 优先级排序

| 优先级 | ID | 描述 | 难度 | 影响 |
|--------|-----|------|------|------|
| **P0 紧急** | P0-1 | Weathervane 去全局梯度 | 高 | 趋化性核心 |
| **P0 紧急** | P0-3 | RIV→肌肉映射替代 curvature_bias | 中 | omega 转弯 |
| **P0 重要** | P0-4 | Food edge reversal 去"上帝视角" | 中 | 觅食行为 |
| **P0 重要** | P0-6 | FLP-11 加入 NeuromodulationManager | 低 | 一致性 |
| **P0 中** | P0-5 | DMP speed_factor → 突触抑制 | 低 | 小行为 |
| **P0 中** | P0-2 | 温度 weathervane 去全局梯度 | 高 | 热趋性 |
| **P1 推荐** | P1-1/2/10 | 移除三重饱食度化学感觉调制 | 低 | 代码清洁 |
| **P1 推荐** | P1-5 | food_memory→GLR-1 增益替代 AVA 注入 | 中 | ARS 机制 |
| **P1 建议** | P1-8 | INS-1 加入 NeuromodulationManager | 中 | 一致性 |
| **P1 建议** | P1-9 | 移除 head_tonic_ 常量 | 低 | 纯涌现 |

### P0-1 修复路线图（最重要）

Weathervane 去全局梯度的具体步骤：

1. **确认 RIA Ca²⁺ klinotaxis 已工作** — Step 28 的 `apply_smb_neck_bias()` 已经从 RIA nrV/nrD 钙差异提取方向信息，不使用全局梯度
2. **验证 SMD 占空比调制** — Step 65 校准了 SMD 振幅到 49mV，±5pA 偏置可产生 8% 占空比变化
3. **移除 `apply_weathervane()` 中的 SMD 直接注入** — 化学/温度/排斥物三个通道全部移除
4. **增强 RIA Ca²⁺ → curvature offset 增益** — 补偿 SMD 直接注入移除后的转向能力
5. **验证**: CI 应保持 >0.2（纯涌现范围），8-seed 测试

预期困难：移除直接注入后 CI 可能下降到 0.1 以下，需要调参 RIA Ca²⁺ 信号增益和 SMB→SMD/muscle 通路强度。

---

## 六、结论

项目展现了扎实的生物物理仿真工程能力和系统性的去硬编码进步。Step 65-68 的"P0 违规修复"系列证明了团队对涌现性原则的重视。

**核心矛盾**：项目在追求"行为涌现"（设计原则）和"行为表现"（CI 指标）之间存在张力。大多数 P0 违规都是为了维持趋化性能（CI>0.3）而引入的旁路。建议接受短期 CI 下降，优先修复 P0-1 和 P0-3，然后通过连接组突触权重校准恢复性能。

**一句话总结**：离子通道和突触层面是真正的生物物理仿真，但行为层面仍有约 20 处"旁路注入"将全局信息直接注入神经元，这些是下一阶段需要系统性清理的主要目标。
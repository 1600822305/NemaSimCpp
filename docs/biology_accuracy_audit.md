# C. elegans 仿真项目 — 生物学准确性审计报告

> 审计日期: 2026-02-13
> 审计范围: 全部源代码 vs. 已知线虫生物学文献
> 严重性分级: 🔴 严重（违反已知生物学事实）| 🟡 中等（简化过度）| 🟢 轻微（可改进）| ⚪ 设计权衡（已知折中）

---

## 一、神经元模型层 (Neuron Dynamics)

### 🔴 B-1: 谷氨酸(Glutamate)反转电位统一设为兴奋性 (-10 mV)

**文件**: [`chemical_synapse.h`](src/connectome/chemical_synapse.h:93) `is_excitatory()` 及 [`default_reversal()`](src/connectome/chemical_synapse.h:100)

**问题**: 代码将 `GLUTAMATE` 统一视为兴奋性递质 (E_syn = -10 mV)。但在 C. elegans 中，谷氨酸既可以是**兴奋性**的（通过 GLR-1/GLR-2 离子型受体, E_rev ≈ 0 mV），也可以是**抑制性**的（通过 GLC-1/AVR-15 谷氨酸门控氯通道, E_rev ≈ -70 mV）。

**生物学事实**:
- 兴奋性 Glu 受体: GLR-1, GLR-2 (AMPA/kainate 类) → E_rev ≈ 0 mV
- 抑制性 Glu 受体: AVR-15, GLC-1, GLC-2 (GluCl) → E_rev ≈ -70 mV (等同 GABA)
- 关键通路: M3→pharynx (AVR-15, 抑制性), AIB→RIM (可能是抑制性GluCl)
- REF: Dent 2006 WormBook, Beg & Bhatt 2001

**影响**: 所有谷氨酸能突触都被当作兴奋性处理，但 ~30% 的 Glu 突触可能应该是抑制性的。这会导致某些回路的极性错误。

**建议**: 在 `SynapseInfo` 中加入受体类型字段，根据突触后神经元表达的受体决定 E_syn。至少应区分 GLR (兴奋) vs GluCl (抑制)。

---

### 🟡 B-2: 所有神经元使用同一组离子通道参数模板

**文件**: [`neuron_factory.cpp`](src/neuron/neuron_factory.cpp:6)

**问题**: `NeuronFactory` 只有 ~6 种模板（default, motor, sensory, inter, motor_b_class, motor_head），但 C. elegans 有 **118 种神经元类型**，每种的离子通道表达谱（transcriptome）都不同。例如:
- AWA 表达 CCA-1 并能产生钙依赖动作电位 (Liu 2018 Cell)，但代码中 AWA 用的是 `create_sensory()` 模板，**没有 CCA-1**
- AVA/AVB 命令中间神经元的通道表达与一般中间神经元不同
- ASE 左右不对称性不仅在转导层面，离子通道表达也不同

**生物学事实**:
- CeNGEN 项目 (Taylor 2021 Cell) 提供了单细胞转录组数据，列出了每种神经元表达的通道基因
- 同一类型不同个体(L/R)的通道表达可以不同

**影响**: 中等。粗粒度的分类能捕捉大致特性，但特定神经元的独特电生理特性被忽略。

**建议**: 至少为关键神经元 (AWA, RMD, AVA, AVB, AIY, RIA) 创建专用通道配置，使用 CeNGEN/WormBase 数据。

---

### 🟡 B-3: 钙动力学模型过度简化

**文件**: [`calcium_dynamics.h`](src/neuron/calcium_dynamics.h:12)

**问题**: 钙动力学使用简单的一阶线性衰减模型:
```
dCa/dt = -alpha * I_Ca - (Ca - baseline) / tau
```
但真实线虫钙动力学涉及:
1. **多种钙源**: 电压门控 Ca²⁺ 通道 (EGL-19, UNC-2, CCA-1)、IP3 受体 (ITR-1)、SERCA 泵、线粒体摄取
2. **非线性缓冲**: calmodulin, parvalbumin 等钙结合蛋白的非线性饱和效应
3. **钙诱导钙释放 (CICR)**: 内质网 Ca²⁺ 存储通过 IP3R/RyR 释放
4. **隔室化钙信号**: 突触末端局部钙 vs 全细胞钙

**影响**: 对依赖精确钙动力学的行为（如咽泵的精确时序、AWA 的全或无钙动作电位）建模不够精确。

---

### 🟡 B-4: 钙电流识别方法不可靠

**文件**: [`single_compartment.cpp`](src/neuron/single_compartment.cpp:41)

**问题**: 代码通过 `ch->get_reversal_potential() > 40.0` 来判断一个通道是否携带钙电流。这是一个**临时 hack**:
- NCA (NALCN) 的 E_rev = 30 mV，不会被识别为钙通道（正确）
- 但如果未来添加 E_Na = 50 mV 的钠通道，会被误识别为钙通道

**建议**: 为 `IonChannel` 基类添加 `ion_type()` 虚函数 (Ca, K, Na, Cl, mixed)，而不是基于反转电位猜测。

---

### 🟢 B-5: 欧拉积分精度

**文件**: [`single_compartment.cpp`](src/neuron/single_compartment.cpp:52)

**问题**: 膜方程使用简单的前向欧拉法 (`V += dV * dt`)，dt = 0.5 ms。对于快速通道（CCA-1 tau_m = 3 ms），这可能导致数值不稳定。

**建议**: 对快速通道考虑使用指数欧拉法或 Rush-Larsen 方法（已在 `relax()` 函数中用于门控变量，但膜方程本身没有）。

---

### 🟡 B-6: SHK-1 (延迟整流 K⁺) 和 EGL-2 (EAG) 通道缺失

**文件**: [`ion_channel.h`](src/neuron/ion_channel.h:1)

**问题**: 蓝图中列出了 14 种通道，但实际只实现了 12 种。缺失:
- **SHK-1** (Shaker Kv, 延迟整流): 广泛表达的主要钾电流，与 SHL-1 不同
- **EGL-2** (EAG-type): 在感觉和中间神经元中表达，参与兴奋性调控
- **KCNL** (SK, 小电导钙激活 K⁺): 参与动作电位后超极化

**影响**: 这些通道对特定神经元的电生理特性（如发放频率适应、后超极化）有显著影响。

---

## 二、突触与连接组层 (Connectome & Synapses)

### 🔴 B-7: 间隙连接假设完全对称（欧姆耦合）

**文件**: [`gap_junction.h`](src/connectome/gap_junction.h:16)

**问题**: 代码将间隙连接实现为纯欧姆双向耦合 `I = g × (V_a - V_b)`。但最新研究表明:
- C. elegans 的许多间隙连接由**异构体组成** (innexin heteromers)，可以是**电整流的**（单向通过性更强）
- INX-7/INX-14 异聚体在 ASE 回路中形成整流间隙连接
- REF: Phelan & Starich 2001, Liu 2013 Current Biology

**生物学事实**:
- C. elegans 有 25 种 innexin 基因（而非哺乳动物的 connexin）
- 不同 innexin 组合形成具有不同电学特性的间隙连接
- 一些间隙连接具有电压依赖性电导、整流特性

**影响**: 某些需要信号定向传播的回路（如 AVA→AVB 之间的间隙连接）的功能可能被错误模拟。

**建议**: 至少为关键回路中的已知整流间隙连接添加电压依赖性电导模型。

---

### 🟡 B-8: 突触权重标定缺乏系统验证

**文件**: [`connectome.h`](src/connectome/connectome.h:60)

**问题**: 
- `synapse_weight_scale_ = 0.3 nS/section`
- `gap_conductance_scale_ = 0.05 nS/section`

这些数值是估计值，没有通过系统性拟合实验数据验证。BAAIWorm (2024) 通过 GPU 梯度优化拟合全脑钙成像数据来标定权重，取得了更好的效果。

**影响**: 突触权重的绝对值可能不准确，需要通过行为/神经活动对标来间接验证。

---

### 🟡 B-9: 突触传递缺少突触延迟

**文件**: [`chemical_synapse.h`](src/connectome/chemical_synapse.h:22)

**问题**: 突触电流在同一时间步内计算并应用，没有突触延迟（synaptic delay）。真实化学突触的延迟约 1-5 ms（包括轴突传导、递质释放、受体结合）。

**生物学事实**:
- C. elegans 神经元较小，轴突传导延迟可能很短 (<1 ms)
- 但 Ca²⁺ 依赖的递质释放有固有延迟 (~0.5-2 ms)
- 分级突触的延迟可能更慢（持续性释放而非脉冲式）

**影响**: 对于快速回路（如触觉反射 <500 ms 总延迟）影响较小；但可能影响需要精确时序的回路。

---

### 🔴 B-10: 硬编码连接组 vs EM 数据对标不充分

**文件**: [`connectome_builder.cpp`](src/connectome/connectome_builder.cpp:41)

**问题**: 连接组通过手写 C++ 代码构建 (`syn()`, `gj()`, `inh()` 函数调用)，而非从 Cook 2019 / WormWiring.org 的原始数据文件加载。这带来几个问题:
1. 难以验证连接的完整性（是否遗漏了连接？）
2. 难以与原始 EM 数据对比校验
3. `inh()` 函数将递质强制设为 GABA，但预突触神经元可能不释放 GABA（可能是 GluCl 介导的抑制）
4. EM 切面数(num_sections)是手动输入的，可能与实际数据不一致

**建议**: 
1. 从 Cook 2019 数据文件（CSV/Excel）程序化生成连接组
2. 添加连接计数验证（与 Cook 2019 Table S1 对比）
3. 区分 GABA 抑制和 GluCl 抑制

---

## 三、身体模型与运动力学 (Body Physics)

### 🔴 B-11: 运动速度不通过物理力学计算

**文件**: [`body_model.cpp`](src/body/body_model.cpp:117)

**问题**: 前进速度通过 `muscle_work × v_max` 直接计算:
```cpp
double forward_speed = v_max * muscle_work;  // v_max = 0.6 mm/s
```
这**完全绕过了物理力学**。真实线虫运动涉及:
1. 肌肉力 → 身体弯曲 → 与基底摩擦产生推进力
2. **阻力力各向异性** (C_N/C_T ≈ 10): 法向阻力 >> 切向阻力
3. 推进力 = Σ(局部曲率变化率 × 法向阻力 - 切向阻力) 
4. REF: Gray & Lissmann 1964 (阻力力理论), Fang-Yen 2010, Boyle 2012

**影响**: 严重。当前身体模型没有真正的"推进力"概念:
- 速度与肌肉活动线性相关，而非与曲率波的传播产生的推进力相关
- 无法正确模拟不同基底（琼脂 vs 液体）上的运动差异
- 无法模拟静水压骨骼的效果
- 这是文档中标注的 **TD-04** 技术债务

**建议**: 实现阻力力理论 (Resistive Force Theory, RFT):
```
F_propulsive = ∫ (C_N - C_T) × κ × dκ/dt × ds
```

---

### 🔴 B-12: 身体段跟随机制过度简化 (无惯性/阻力)

**文件**: [`body_model.cpp`](src/body/body_model.cpp:173)

**问题**: 身体段位置通过**几何跟随**计算:
```cpp
segments_[i].angle = segments_[i-1].angle - segments_[i].curvature * segment_length_;
segments_[i].position = segments_[i-1].position - dir * segment_length_;
```
这是纯运动学（kinematic），不是动力学（dynamic）。身体被当作刚性链，没有:
- 弹性变形
- 粘弹性阻尼
- 内部静水压
- 环境流体阻力

**影响**: 身体不会展示真实的粘弹性行为（如停止后的回弹、被动弯曲的衰减）。

---

### 🟡 B-13: 肌肉系统只是占位符

**文件**: [`muscle_system.h`](src/body/muscle_system.h:9)

**问题**: `MuscleSystem` 类完全为空。肌肉激活直接以 `[0, 1]` 标量传递给 `BodySegment`，没有:
- 肌肉收缩力学模型 (Hill model: 力-速度-长度关系)
- 肌肉兴奋-收缩耦合 (ECC: 电信号 → Ca²⁺ → 横桥循环)
- 95 个独立肌肉细胞的建模（当前用 48 段×2 = 96 个标量近似）
- 肌肉间的间隙连接耦合 (BWM 通过 UNC-9 innexin 电耦合)

**生物学事实**:
- C. elegans 体壁肌肉排列为 4 列（2 背 + 2 腹），每列 ~24 个菱形细胞
- 肌肉细胞通过间隙连接彼此电耦合，形成功能合胞体
- 肌肉收缩受 ACh (兴奋, UNC-29/UNC-38 nAChR) 和 GABA (抑制, UNC-49) 调控
- Emmons 2024: 肌肉不仅是效应器，**也是信息整合节点**
- REF: Liu 2006, Richmond & Bhatt 1999

---

### 🟡 B-14: 48 个身体段 vs 实际肌肉解剖

**文件**: [`types.h`](src/core/types.h:139) `NUM_BODY_SEGMENTS = 48`

**问题**: 48 段的选择大致对应于每侧 ~24 个肌肉细胞（背+腹 ≈ 48），但实际肌肉排列并非简单的一段一肌肉:
- 实际是 95 个肌肉细胞排列为 4 列
- 每个肌肉细胞被 2-3 个运动神经元支配
- 运动神经元的肌肉臂（muscle arms）长度不同，一个 MN 可能支配 2-4 个肌肉细胞

**影响**: 中等。48 段足以产生合理的蠕动波，但精确的肌肉-运动神经元映射被简化了。

---

## 四、感觉转导层 (Sensory Transduction)

### 🟡 B-15: weathervane 机制绕过了完整的神经回路

**文件**: [`apply_motor_control.cpp`](src/simulation/apply_motor_control.cpp:63)

**问题**: weathervane（风标转向）机制通过直接计算环境梯度法向分量并注入 SMD 差异电流来实现。虽然文档标注这是一个已知的简化，但它**绕过了完整的感觉→中间→运动神经元回路**:

真实回路: 浓度梯度 → 头部摆动 → ASE/AWC 感觉 → AIY/AIZ 中间 → SMB/SMD 运动
代码简化: 浓度梯度 → 直接计算 → SMD 差异电流

**生物学事实**:
- Iino & Yoshida 2009 提出 weathervane 是通过头部摆动的**相位锁定**实现的
- ASE/AWC 在头部左右摆动时采样不同浓度 → 产生与摆动同步的信号
- 这个信号通过 AIY/AIZ 传递到 SMD/SMB，调制摆动幅度的不对称性
- 直接注入绕过了 AIY/AIZ 的信号处理（包括多感觉整合）

**影响**: weathervane 效果是正确的方向，但缺少了 AIY/AIZ 层面的信号处理（如与温度、食物记忆的整合）。

---

### 🟡 B-16: AFD 温度传感器使用 |T - Tc| 而非方向性信号

**文件**: [`sensory_transducer.h`](src/environment/sensory_transducer.h:144)

**问题**: `ThermoTransducer` 计算的是 `deviation = |T - Tc|`，然后用双滤波器的 OFF 响应。这意味着 AFD 只编码"距离 Tc 有多远"，不编码方向。但实验表明:

**生物学事实**:
- AFD 对**升温**和**降温**有不对称响应 (Clark 2006, Ramot 2008)
- AFD 主要被**高于 Tc 的升温**激活 (Biron 2008)
- 低于 Tc 时，其他神经元 (AWC, ASI) 可能参与负向趋温性
- 取绝对值会丢失方向信息，导致 Tc 上下的行为对称化

**建议**: 考虑将 AFD 响应建模为方向性的: 高于 Tc 时升温激活，低于 Tc 时降温激活（而非取绝对值）。

---

### 🟢 B-17: 化学感觉采样只在头部一个点

**文件**: [`apply_sensory_systems.cpp`](src/simulation/apply_sensory_systems.cpp:21)

**问题**: 所有化学感觉神经元都在同一个采样点 (`sample_pos`, 基于头部弯曲位移) 采样。但:
- C. elegans 有两个独立的感觉开口: amphid (头部) 和 phasmid (尾部)
- 尾部化学感觉 (PHA/PHB) 已在 Step 81 中实现，但采样点可能需要独立计算
- 头部左右两侧的 amphid 是分开的，L/R 不对称采样可能对 weathervane 重要

---

## 五、神经调质系统 (Neuromodulation)

### 🟡 B-18: 神经调质浓度是全局标量，而非空间分布

**文件**: [`neuromodulation.h`](src/neuromodulation/neuromodulation.h:53)

**问题**: 每种神经调质只有一个全局 `concentration` 值 (0-1 标量)。真实的体积传递是**空间分布**的:
- 释放源附近浓度高，远处低
- 扩散-降解动力学创建空间梯度
- 相邻但不同区域的目标神经元可能暴露于不同浓度

**生物学事实**:
- 5-HT 主要在头部释放 (NSM, ADF) → 头部浓度 > 尾部
- DA 在前部 (CEP, ADE) 和后部 (PDE) 都有释放源
- 神经肽的扩散范围有限 (~10-50 μm)
- REF: Ripoll-Sánchez 2023 Neuron — neuropeptidergic "wireless" connectome 是空间结构化的

**影响**: 对于小体积的 C. elegans (~1mm 长)，全局标量可能是合理的一级近似。但对于需要局部调制的行为可能不够。

---

### 🟡 B-19: 神经肽系统严重简化

**文件**: [`neuromodulation.h`](src/neuromodulation/neuromodulation.h:47)

**问题**: 项目实现了 7 种神经调质 (5-HT, DA, OA, TA, NLP-12, PDF, FLP-11)，但 C. elegans 有:
- **250+ 种神经肽** (FLP, NLP, INS 家族)
- **150+ 种神经肽受体** (NPR, FRPR, etc.)
- 神经肽信号形成的网络密度**远超突触连接**

关键缺失的神经肽回路:
- **INS-1/DAF-2/DAF-16**: 只有 ins1_conc 标量，没有完整的胰岛素/PI3K 级联
- **FLP 神经肽家族**: 31 个 FLP 基因，只实现了 FLP-11 和 FLP-20
- **NLP 神经肽家族**: 47 个 NLP 基因，只实现了 NLP-12
- **PDF-1/PDF-2**: 实现了 PDF-1 (roaming)，但 PDF-2 (RIM autocrine) 简化
- REF: Li & Kim 2008 (C. elegans 神经肽综述)

---

## 六、咽部系统 (Pharyngeal System)

### 🟡 B-20: 咽泵只实现了 4 相状态机，而非 20 神经元回路

**文件**: [`pharyngeal_pump.h`](src/pharynx/pharyngeal_pump.h:25)

**问题**: 咽泵用 `Phase::RESTING → EXCITATION → PLATEAU → REPOLARIZATION` 状态机实现，由 MC 和 M3 的释放率驱动。但真实咽部有:
- 20 个独立神经元 (MC, M1, M2, M3, M4, M5, I1-I6, MI, NSM, etc.)
- 独立的咽部 CPG（不依赖体神经系统的自主节律）
- 咽部肌肉本身有电生理特性（CCA-1, EGL-19, EXP-2, AVR-15 等通道）
- 通过 RIP 神经元与体神经系统的双向通信

**影响**: 咽泵行为（泵速 2-4 Hz）可以用状态机近似产生，但:
- 无法正确模拟 M4 驱动的蠕动（isthmus peristalsis）的精确时序
- 无法模拟 I1/I2/MC 之间的复杂反馈
- 5-HT 对咽泵的调制是通过直接对 MC 的效应实现的，而非通过内部回路

---

## 七、身体运动学 (Locomotion Kinematics)

### 🟡 B-21: 反向运动时波传播方向未正确反转

**文件**: [`body_model.cpp`](src/body/body_model.cpp:84)

**问题**: 本体感觉波传播始终是从前向后 (`anterior_curv = segments_[i-1].curvature`)。但在反向运动时:
- 真实线虫的蠕动波从后向前传播（尾→头）
- A-class MN (DA/VA) 驱动反向波，其本体感觉耦合方向应该是**后→前**
- REF: Kawano 2011 — 前进时 B-class 前→后波，后退时 A-class 后→前波

**当前实现**: 无论前进还是后退，身体曲率都是从 segment[i-1] 传递到 segment[i]，方向固定。

**建议**: 根据 `forward_drive_` vs `reverse_drive_` 的平衡，切换本体感觉耦合方向。

---

### 🟡 B-22: omega turn 通过 curvature_bias 而非肌肉力实现

**文件**: [`body_model.h`](src/body/body_model.h:38) `set_curvature_bias()`

**问题**: omega 转弯通过 `curvature_bias_` 直接加到头部曲率上，而非通过 RIV 运动神经元 → 头部腹侧肌肉 → 深度腹侧弯曲来实现。这意味着 omega 弯曲的力学不通过肌肉系统。

代码注释 ([motor_controller.cpp:160](src/motor/motor_controller.cpp:160)) 解释了原因: "muscle_gain (0.3) too weak for omega curvature"。

**影响**: 这是一个违反"所有运动必须通过肌肉"原则的技术债务。

---

## 八、环境模型 (Environment)

### 🟢 B-23: 温度场是简单线性梯度

**文件**: [`environment.h`](src/environment/environment.h:70)

**问题**: 温度场为简单的线性梯度 `T(x,y) = T_center + grad_x × dx + grad_y × dy`。实验中:
- 温度梯度可能非线性（特别是在加热/冷却源附近）
- 辐射热源会产生径向梯度
- 琼脂板上的温度场受板厚、边界条件影响

**影响**: 对于标准热梯度板实验 (Hedgecock & Russell 1975)，线性近似合理。

---

### 🟢 B-24: 化学场扩散模型简化

**文件**: [`chemical_field.h`](src/environment/chemical_field.h)

**问题**: 化学场使用 Gaussian 模型而非实时扩散方程求解。对于静态实验（点源扩散达到稳态）这是合理的，但无法模拟:
- 时变扩散（突然移除食物后的浓度衰减）
- 多源竞争
- 风场对扩散的影响

---

## 九、整体架构层面

### ⚪ B-25: 只模拟雌雄同体，缺少雄虫

**问题**: 项目只模拟 302 神经元的雌雄同体。雄虫有额外 83 个神经元 (总 385 个)、尾部感觉射线等结构。这是一个**有意的设计决策**（见 behavior_coverage_analysis.md），不是 bug。

---

### 🟡 B-26: 多隔室模型只用于 RIA

**文件**: [`neuron_factory.h`](src/neuron/neuron_factory.h:32) `create_ria_multi()`

**问题**: `MultiCompartmentNeuron` 只用于 RIA 的亚细胞钙信号。但其他神经元也需要:
- **RMD**: 树突形态影响振荡频率 (Nicoletti 2019)
- **AWA**: 钙动作电位需要轴突末端的高 Ca²⁺ 通道密度
- **AIY**: 轴突上有多个突触输入区域
- **VNC 运动神经元**: 长轴突沿体壁延伸，不同位置接收不同输入

---

### 🟡 B-27: 缺少发育过程中的突触重塑

**问题**: 连接组是固定的成虫连接组 (Cook 2019)。但:
- Witvliet 2021 展示了从 L1 到成虫的连接组变化
- DD 运动神经元在 L1→L2 发育中极性反转 (Hallam & Jin 1998)
- 这与 Dauer/发育行为的模拟相关

---

## 十、总结

### 按严重性统计

| 严重性 | 数量 | 编号 |
|--------|------|------|
| 🔴 严重 | 4 | B-1, B-7, B-10, B-11 |
| 🟡 中等 | 16 | B-2,3,4,6,8,9,13,14,15,16,18,19,20,21,22,26 |
| 🟢 轻微 | 4 | B-5, B-17, B-23, B-24 |
| ⚪ 设计权衡 | 1 | B-25 |

### 优先级建议

**P0 (应优先修复)**:
1. **B-1**: 谷氨酸兴奋/抑制区分 — 影响多条关键回路极性
2. **B-11**: 阻力力理论 (RFT) 运动模型 — 当前速度计算完全非物理
3. **B-10**: 连接组从原始数据加载 — 确保拓扑正确性

**P1 (次优先)**:
4. **B-7**: 间隙连接整流性 — 影响命令神经元回路
5. **B-2**: 关键神经元专用通道配置 — 影响 AWA/RMD 等特殊行为
6. **B-21**: 反向波传播方向 — 影响后退运动的物理正确性
7. **B-13**: 肌肉力学模型 — 当前完全空白

**P2 (长期改进)**:
8. B-3, B-15, B-16, B-18, B-19, B-20, B-22, B-26, B-27

---

### 项目优势（与其他线虫仿真对比）

值得强调的是，本项目在以下方面**做得很好**:
1. ✅ 302/302 神经元全部模拟（优于大多数项目只模拟 10-50 个）
2. ✅ HH 型分级电位模型（非简单的 LIF 或 integrate-and-fire）
3. ✅ 12 种离子通道（接近 Nicoletti 2019 的 14 种）
4. ✅ 突触短时程可塑性 (STP, Tsodyks-Markram)
5. ✅ 7 种神经调质的体积传递
6. ✅ 本体感觉波传播 (proprioceptive wave)
7. ✅ 行为从神经回路涌现（7 个硬编码全部移除）
8. ✅ 消融验证与文献一致
9. ✅ 多隔室神经元模型 (RIA)
10. ✅ 极高的行为覆盖率 (56/61 = 92%)
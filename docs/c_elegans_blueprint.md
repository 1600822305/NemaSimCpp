# C. elegans 302 神经元工程复刻 — 总体架构蓝图

## 1. 项目概述

本项目旨在从工程角度完整复刻秀丽隐杆线虫（C. elegans）的神经系统与行为。
C. elegans 拥有精确的 302 个神经元、约 7000 个化学突触和约 600 个电突触（间隙连接），
是目前唯一拥有完整连接组（connectome）数据的多细胞生物。

**目标**：通过工程仿真，使虚拟线虫在模拟环境中展现出与真实线虫一致的行为模式。

**技术栈**：
- **核心计算**：C++17/20（神经元仿真、物理引擎、性能关键路径）
- **数据处理/可视化**：Python（连接组数据解析、行为分析、可视化）
- **构建系统**：CMake
- **并行计算**：OpenMP / CUDA（可选）

---

## 2. 分层架构总览

```
┌─────────────────────────────────────────────────────┐
│             第8层：行为涌现与评估层                    │
│         (Behavior Emergence & Evaluation)            │
├─────────────────────────────────────────────────────┤
│             第7层：运动控制层                          │
│            (Motor Control Layer)                     │
├─────────────────────────────────────────────────────┤
│             第6层：神经调质与胞外信号层                 │
│     (Neuromodulation & Extrasynaptic Signaling)      │
├──────────────────────┤  "无线连接组"  ├──────────────┤
│             第5层：连接组与突触层                      │
│         (Connectome & Synaptic Layer)                │
├──────────────────────┤  "有线连接组"  ├──────────────┤
│             第4层：神经元计算层                        │
│          (Neuron Dynamics Layer)                     │
├─────────────────────────────────────────────────────┤
│             第3层：感知转导层                          │
│        (Sensory Transduction Layer)                  │
├─────────────────────────────────────────────────────┤
│             第2层：躯体物理层                          │
│           (Body Physics Layer)                       │
├─────────────────────────────────────────────────────┤
│             第1层：环境仿真层                          │
│         (Environment Simulation Layer)               │
└─────────────────────────────────────────────────────┘
```

> **架构要点**：第5层（有线连接组）和第6层（无线连接组）共同构成完整的
> 神经通信系统。研究表明（Ripoll-Sánchez et al., 2023），神经肽的胞外
> 旁分泌信号（"wireless connectome"）形成的网络密度远超突触连接，
> 对行为的调控至关重要。

---

## 3. 各层详细设计

### 3.1 第1层：环境仿真层（Environment Simulation Layer）

**职责**：模拟线虫生存的物理世界。

**核心组件**：

| 组件 | 说明 | 实现语言 |
|------|------|---------|
| `Environment` | 2D/3D 仿真空间管理 | C++ |
| `ChemicalField` | 化学物质浓度梯度场（扩散方程求解） | C++ |
| `TemperatureField` | 温度梯度场 | C++ |
| `SubstrateModel` | 基底模型（琼脂表面摩擦、液体粘性） | C++ |
| `FoodSource` | 食物源（大肠杆菌分布）建模 | C++ |

**关键算法**：
- 化学扩散：2D 扩散方程离散化求解（有限差分法）
- 空间索引：网格划分用于高效浓度查询
- 时间步进：固定步长或自适应步长

**数据结构**：
```cpp
class Environment {
    double time_step_;          // 仿真时间步长 (ms)
    double total_time_;         // 当前仿真时间
    Vector2d arena_size_;       // 仿真区域尺寸
    ChemicalField chem_field_;  // 化学场
    TemperatureField temp_field_; // 温度场
    SubstrateModel substrate_;  // 基底模型

    void step();                // 推进一个时间步
    double sample_chemical(Vector2d pos, ChemType type) const;
    double sample_temperature(Vector2d pos) const;
};
```

---

### 3.2 第2层：躯体物理层（Body Physics Layer）

**职责**：模拟线虫的物理身体、肌肉系统和运动力学。

**核心组件**：

| 组件 | 说明 | 实现语言 |
|------|------|---------|
| `BodyModel` | 身体几何形态（弹性杆模型 or SPH 粒子模型） | C++ |
| `MuscleSystem` | 95 个体壁肌肉细胞的收缩模型 | C++ |
| `HydrostaticSkeleton` | 静水压骨骼（内部加压液体 + 弹性外壳） | C++ |
| `Locomotion` | 蠕动运动学计算（低雷诺数流体） | C++ |
| `PharynxModel` | 咽部系统（独立的 20 神经元 + 肌肉泵） | C++ |
| `CollisionDetector` | 身体与环境的碰撞检测 | C++ |

**身体建模方法**：

两种可选方案（可根据精度需求切换）：

**方案 A：弹性杆模型**（轻量级，推荐 MVP 阶段）
- 将线虫身体离散为 N 个弹性段（segments），每段有位置、角度、曲率
- 每段连接背侧和腹侧肌肉
- 肌肉收缩产生弯矩 → 驱动身体变形 → 与环境交互产生推进力

**方案 B：SPH 粒子模型**（高精度，参考 Sibernetic/BAAIWorm）
- 基于 PCI-SPH（预测-校正不可压缩光滑粒子流体力学）
- 身体由弹性粒子组成，环境为流体粒子
- 可精确模拟身体-流体交互、静水压骨骼变形
- 参考实现：OpenWorm/Sibernetic（C++/OpenCL）

**数据结构**（方案 A）：
```cpp
struct BodySegment {
    Vector2d position;       // 段中心位置
    double angle;            // 段朝向角度
    double curvature;        // 局部曲率
    double dorsal_activation;  // 背侧肌肉激活度 [0,1]
    double ventral_activation; // 腹侧肌肉激活度 [0,1]
};

class BodyModel {
    static constexpr int NUM_SEGMENTS = 48;  // 身体离散段数
    std::array<BodySegment, NUM_SEGMENTS> segments_;
    double body_length_;     // 体长 (~1mm)
    double body_radius_;     // 体半径 (~40μm)
    double stiffness_;       // 身体刚度
    double damping_;         // 阻尼系数
    double internal_pressure_; // 静水压骨骼内部压力

    void update_physics(double dt);
    Vector2d get_head_position() const;
    Vector2d get_tail_position() const;

    // 本体感觉输出：提供给 DVA/PVD 等本体感觉神经元
    double get_local_curvature(int segment_idx) const;
    double get_local_stretch(int segment_idx) const;
};
```

**关键物理参数**：
- 体长：~1 mm（成体可达 ~1.5 mm）
- 体壁肌肉：95 个细胞（背侧 48 + 腹侧 47），排列为 4 条纵行
- 运动频率：~0.5-2 Hz（爬行），~1.7 Hz（游泳）
- 低雷诺数环境（Re ~ 10⁻²）
- 静水压骨骼：弹性不透水外壳 + 内部加压体液

**⚠️ 关键发现（Emmons 2024）**：
> 体壁肌肉不仅是运动效应器，**也是重要的信息汇聚节点和感觉反馈源**。
> 连接组分析表明，肌肉接收来自多种感觉和中间神经元的大量突触输入，
> 并参与反馈回路。仿真中必须将肌肉视为闭环系统的一部分。

**咽部系统**：
- 独立的 20 个咽部神经元，拥有自己的 CPG（中枢模式发生器）
- 控制咽部肌肉的节律性泵送（进食）
- 与体神经系统通过少量突触相连（RIP 神经元为桥梁）
- **可在 Phase 2 之后再实现**

---

### 3.3 第3层：感知转导层（Sensory Transduction Layer）

**职责**：将环境中的物理/化学信号转化为神经元可接受的输入电流。

**核心组件**：

| 组件 | 说明 | 实现语言 |
|------|------|---------|
| `ChemoSensor` | 化学感觉转导（AWA, AWC, ASE, ASH 等） | C++ |
| `MechanoSensor` | 机械感觉转导（ALM, AVM, PLM, PVD 等） | C++ |
| `ThermoSensor` | 温度感觉转导（AFD, AWC 等） | C++ |
| `NociSensor` | 伤害感觉/多模态（ASH 等） | C++ |
| `SensorManager` | 感觉神经元统一管理 | C++ |

**感觉神经元分类**（共 ~39 个感觉神经元）：

| 类型 | 神经元 | 感知模态 |
|------|--------|---------|
| 化学感觉-吸引 | AWA, AWC, ASE(L/R) | 吸引性化学物质 |
| 化学感觉-回避 | ASH, ADL, AWB | 有害化学物质 |
| 机械触觉-前 | ALM(L/R), AVM | 前部轻触 |
| 机械触觉-后 | PLM(L/R), PVM | 后部轻触 |
| 温度感觉 | AFD(L/R), AWC | 温度梯度 |
| 本体感觉 | DVA, PVD | 身体姿态 |
| 伤害感觉 | ASH, FLP, OLQ | 有害刺激 |

**转导模型**：
```cpp
class SensoryNeuron {
    std::string name_;           // 神经元名称 (e.g. "ASEL")
    SensoryType modality_;       // 感知模态
    double adaptation_state_;    // 适应状态
    double adaptation_tau_;      // 适应时间常数
    double gain_;                // 增益
    double baseline_;            // 基线电流

    // 将物理刺激强度转化为输入电流
    double transduce(double stimulus_intensity, double dt);
};
```

**关键特性**：
- 化学感觉需要实现**浓度时间导数**检测（线虫通过比较不同时刻的浓度来判断梯度方向）
- 温度感觉需要实现**培养温度记忆**（AFD 记忆曾经生长的温度）
- 机械感觉需要实现**快速适应**（轻触后迅速衰减）
- 感觉神经元存在**左右不对称性**（如 ASEL 感知 Na⁺ 上升，ASER 感知 Na⁺ 下降）

---

### 3.4 第4层：神经元计算层（Neuron Dynamics Layer）

**职责**：模拟单个神经元的电生理行为。

**核心组件**：

| 组件 | 说明 | 实现语言 |
|------|------|---------|
| `Neuron` | 神经元基类（单隔室/多隔室） | C++ |
| `SingleCompartmentNeuron` | 单隔室神经元模型（默认） | C++ |
| `MultiCompartmentNeuron` | 多隔室神经元模型（含树突形态） | C++ |
| `IonChannel` | 离子通道模型（14+ 种通道） | C++ |
| `CalciumDynamics` | 胞内钙动力学 | C++ |
| `NeuronFactory` | 根据连接组数据和形态数据创建神经元实例 | C++ |

**C. elegans 神经元的特殊性**：

> **关键区别**：C. elegans 的大部分神经元**不产生经典的钠依赖动作电位**，
> 而是通过**分级电位（graded potential）**进行信号传导。
> 这与哺乳动物神经元有根本区别，不能简单套用 LIF 脉冲模型。
>
> **⚠️ 重要更新**（Liu et al., 2018, *Cell*）：
> AWA 嗅觉神经元已被证实能产生**钙介导的全或无动作电位（calcium-mediated
> all-or-none action potentials）**。RMD 运动神经元表现出**双稳态和持续振荡**
> （Nicoletti et al., 2019）。因此不能一概而论地排除动作电位。
>
> **⚠️ 隔室效应**（Hendricks et al., 2012）：
> RIA 中间神经元的亚细胞钙信号编码头部运动方向。部分神经元需要
> **多隔室模型**才能捕捉空间分隔的信号处理。BAAIWorm（2024）采用
> 具有真实形态的多隔室模型，取得了最佳仿真效果。

**神经元模型层级**（按复杂度递增）：

| 层级 | 模型 | 适用场景 | 复杂度 |
|------|------|---------|--------|
| L1 | 简化分级电位（RC 电路） | 快速原型、大规模扫描 | 低 |
| L2 | HH 型分级电位 + 离子通道 | 默认推荐，大部分神经元 | 中 |
| L3 | 多隔室 HH + 钙动力学 | RIA、AWA 等需要空间分辨的神经元 | 高 |
| L4 | 完整生物物理模型 | 关键神经元的精确仿真 | 极高 |

**推荐神经元模型**（L2，默认）：
```cpp
// 膜电位方程（HH 型分级电位）
// C_m * dV/dt = -g_leak * (V - E_leak) - Σ g_ion(V,Ca) * (V - E_ion) + I_syn + I_ext
class Neuron {
public:
    virtual ~Neuron() = default;
    virtual void step(double dt) = 0;
    virtual double get_membrane_potential() const = 0;
    virtual double get_transmitter_release_rate() const = 0;
};

class SingleCompartmentNeuron : public Neuron {
    double V_;            // 膜电位 (mV)
    double C_m_;          // 膜电容 (pF), 典型值 ~1-3 pF
    double g_leak_;       // 漏电导 (nS)
    double E_leak_;       // 漏电位 (mV), 典型值 ~ -60 to -40 mV
    double I_ext_;        // 外部输入电流 (pA)
    double I_syn_;        // 突触电流 (pA)

    std::vector<std::unique_ptr<IonChannel>> channels_;
    CalciumDynamics calcium_;

    void step(double dt) override;
    double get_membrane_potential() const override { return V_; }
    double get_transmitter_release_rate() const override;
};
```

**多隔室模型**（L3，用于关键神经元）：
```cpp
class MultiCompartmentNeuron : public Neuron {
    struct Compartment {
        double V;                // 该隔室膜电位
        double C_m;              // 该隔室膜电容
        double area;             // 膜面积
        double axial_resistance; // 轴向电阻 (与相邻隔室间)
        std::vector<std::unique_ptr<IonChannel>> channels;
        CalciumDynamics calcium;
    };

    std::vector<Compartment> compartments_;  // 形态学隔室
    // 隔室间耦合：I_axial = (V_i - V_j) / R_axial

    void step(double dt) override;
};
```

**钙动力学模块**：
```cpp
class CalciumDynamics {
    double Ca_;              // 胞内钙浓度 [Ca²⁺]_i (μM)
    double Ca_baseline_;     // 基线钙浓度 (~0.05-0.1 μM)
    double Ca_tau_;           // 钙衰减时间常数 (ms)
    double Ca_buffer_ratio_; // 钙缓冲比

    void update(double I_Ca, double dt);
    double get_concentration() const { return Ca_; }
};
```

**已知的 C. elegans 离子通道**（Nicoletti et al., 2019）：

| 通道基因 | 离子选择性 | 类型 | 功能 |
|---------|-----------|------|------|
| EGL-19 | Ca²⁺ | L-type CaV | 持续性钙电流，广泛表达 |
| UNC-2 | Ca²⁺ | N/P/Q-type CaV | 突触传递相关钙流入 |
| CCA-1 | Ca²⁺ | T-type CaV | 低阈值钙电流，RMD 振荡关键 |
| SHL-1 | K⁺ | Shaker-like KV | 快速失活钾电流（A-type） |
| KVS-1 | K⁺ | KV | 缓慢失活钾电流 |
| KQT-3 | K⁺ | KCNQ-type | M 电流（慢钾电流） |
| SHK-1 | K⁺ | Shaker KV | 延迟整流钾电流 |
| EGL-2 | K⁺ | EAG-type | 电压门控钾电流 |
| EGL-36 | K⁺ | Shaw-type | 高阈值钾电流 |
| IRK | K⁺ | Kir | 内向整流钾电流 |
| SLO-1 | K⁺ | BK (Ca²⁺-activated) | 大电导钙激活钾通道 |
| SLO-2 | K⁺ | Ca²⁺/Na⁺-activated | 钙/钠激活钾通道 |
| KCNL | K⁺ | SK (Ca²⁺-activated) | 小电导钙激活钾通道 |
| NCA | Na⁺ | NALCN | 漏钠通道，维持兴奋性 |

> **实现策略**：先为每种通道基因实现独立的 `IonChannel` 子类，
> 然后根据 WormBase 中的基因表达数据，为每个神经元配置其表达的通道组合。

**神经元分类**（302 个体细胞系神经元 + 20 个咽部神经元）：

| 类别 | 数量 | 说明 |
|------|------|------|
| 感觉神经元 | ~39 | 接收环境刺激（几乎所有都是多功能的） |
| 中间神经元 | ~81 | 信息整合与处理（57% 功能尚不完全清楚） |
| 运动神经元 | ~113 | 控制肌肉收缩 |
| 咽部神经元 | 20 | 独立的咽部神经系统 |

> **注意**：几乎所有 C. elegans 神经元都是**多功能**的——同时具有
> 感觉、整合和运动输出特性。上述分类基于主要功能。

---

### 3.5 第5层：连接组与突触层（Connectome & Synaptic Layer）

**职责**：管理 302 个神经元之间的连接拓扑与突触传导。

**核心组件**：

| 组件 | 说明 | 实现语言 |
|------|------|---------|
| `Connectome` | 连接组拓扑管理 | C++ |
| `ChemicalSynapse` | 化学突触模型 | C++ |
| `GapJunction` | 电突触（间隙连接）模型 | C++ |
| `ConnectomeLoader` | 连接组数据加载器（解析 CSV/JSON） | C++/Python |

**连接组数据来源**：
- Cook et al. 2019（全动物连接组，两性完整重建）
- Emmons 2024（最新综合分析：473 节点含肌肉、6,951 条边）
- Witvliet et al. 2021（发育连接组：从 L1 幼虫到成虫的变化）
- WormWiring.org 数据库
- OpenWorm 项目 c302 框架的 NeuroML 数据

**⚠️ 连接组模块化结构**（Emmons 2024）：
> 谱方法社区检测将连接组划分为 **10 个功能模块（communities）**：
> - 模块 1：纵向体壁肌肉 + 腹索运动神经元
> - 模块 2-4：感觉→中间→运动的垂直信息流通道
> - 模块 5：咽部系统（独立）
> - 模块 6-8：产卵/排便等小肌肉群
> - 模块 9-10：腹索中间神经元网络（**新发现的整合中枢**）
>
> 特别是模块 9 中的腹索中间神经元（AVG, AVH, AVJ, AVK, DVA, DVC, PVN, PVP 等）
> 通过大量间隙连接相互连接，可能构成一个**此前未被识别的信息整合中枢**。

**化学突触模型**：
```cpp
class ChemicalSynapse {
    int pre_neuron_id_;          // 突触前神经元
    int post_neuron_id_;         // 突触后神经元
    double weight_;              // 突触权重（连接强度）
    NeurotransmitterType nt_;    // 神经递质类型
    bool is_excitatory_;         // 兴奋性 or 抑制性
    double reversal_potential_;  // 突触反转电位

    // 分级突触传递：递质释放率 = f(V_pre)
    double release_rate_;        // 当前递质释放速率
    double tau_rise_;            // 突触电流上升时间常数
    double tau_decay_;           // 突触电流衰减时间常数

    double compute_current(double V_pre, double V_post, double dt);
};
```

**电突触（间隙连接）模型**：
```cpp
class GapJunction {
    int neuron_a_id_;
    int neuron_b_id_;
    double conductance_;     // 间隙连接电导 (nS)

    // 双向欧姆电流：I = g * (V_a - V_b)
    double compute_current_a_to_b(double V_a, double V_b) const;
};
```

**神经递质系统**：

| 神经递质 | 类型 | 使用该递质的神经元数 |
|---------|------|-------------------|
| 乙酰胆碱 (ACh) | 兴奋性 | ~120 |
| GABA | 抑制性 | ~26 |
| 谷氨酸 (Glutamate) | 兴奋性/抑制性 | ~38 |
| 多巴胺 (DA) | 调节性 | 8 |
| 5-羟色胺 (5-HT) | 调节性 | ~8 |
| 酪胺 (Tyramine) | 调节性 | ~4 |
| 章鱼胺 (Octopamine) | 调节性 | ~2 |
| 神经肽 | 调节性 | 广泛 |

**突触可塑性**（可选扩展）：
- 短时程突触可塑性（STD/STF）
- 神经调质对突触权重的调控

**突触权重标定方法**：
> 连接组数据提供的是突触的 EM 切面计数（连接的解剖强度），不直接等于生理权重。
> - **初始方案**：突触权重 ∝ EM 切面数（即突触面积代理指标）
> - **拟合方案**（参考 BAAIWorm）：通过梯度优化算法拟合实验钙成像数据
>   或膜电位相关矩阵，调整突触权重直至神经活动模式匹配实验观测
> - **支持工具**：可基于 GPU 并行的参数优化（BAAIWorm 使用多 GPU 训练）

---

### 3.6 第6层：神经调质与胞外信号层（Neuromodulation & Extrasynaptic Signaling）

**职责**：模拟突触外的"无线"神经通信——神经肽、单胺类调质的旁分泌信号。

> **为什么需要这一层？**
> Ripoll-Sánchez et al. (2023, *Neuron*) 首次完整绘制了 C. elegans 的
> **神经肽连接组（neuropeptidergic connectome）**。研究发现：
> - C. elegans 表达 **250+ 种神经肽**和 **150+ 种神经肽受体**
> - 神经肽信号形成的网络**密度远超突触连接**
> - 许多行为（如觅食策略切换、社交/独居偏好、运动速度调节）
>   依赖于神经肽/单胺调制，**无法仅通过突触连接解释**

**核心组件**：

| 组件 | 说明 | 实现语言 |
|------|------|---------|
| `Neuromodulator` | 神经调质分子模型（释放、扩散、降解） | C++ |
| `NeuropeptideSystem` | 神经肽信号管理（配体-受体匹配） | C++ |
| `MonoamineSystem` | 单胺类调质（多巴胺、5-HT 等） | C++ |
| `ExtrasynapticNetwork` | 胞外信号网络拓扑 | C++ |
| `ModulationEffector` | 调质对突触权重/神经元兴奋性的效应 | C++ |

**单胺类神经调质**：

| 调质 | 产生神经元 | 主要功能 |
|------|-----------|---------|
| 多巴胺 (DA) | CEP, ADE, PDE（共 8 个） | 食物检测、运动减速 |
| 5-羟色胺 (5-HT) | NSM, ADF, HSN 等（~8 个） | 进食状态、产卵、运动状态 |
| 酪胺 (Tyramine) | RIM, RIC 等（~4 个） | 回避行为、运动抑制 |
| 章鱼胺 (Octopamine) | RIC 等（~2 个） | 饥饿响应 |

**神经肽信号建模**：
```cpp
class NeuropeptideSignal {
    std::string peptide_name_;      // 神经肽名称 (e.g. "FLP-1")
    std::vector<int> source_neurons_; // 释放该肽的神经元
    std::vector<int> target_neurons_; // 表达受体的神经元

    double release_rate_;           // 释放速率（与源神经元活动相关）
    double diffusion_range_;        // 有效扩散范围
    double degradation_rate_;       // 降解速率
    double effective_concentration_; // 当前有效浓度

    // 调节效应：改变目标神经元的兴奋性或突触权重
    enum class ModulationType { EXCITABILITY, SYNAPTIC_GAIN, ION_CHANNEL };
    ModulationType effect_type_;
    double effect_magnitude_;
};

class NeuromodulationManager {
    std::vector<NeuropeptideSignal> neuropeptides_;
    MonoamineSystem monoamines_;

    // 每步更新所有调质浓度并施加调制效应
    void update(const std::vector<Neuron*>& neurons, double dt);

    // 查询某神经元当前受到的调制状态
    ModulationState get_modulation(int neuron_id) const;
};
```

**实现优先级**：
- **Phase 1**：忽略此层（纯突触模型）
- **Phase 2**：实现多巴胺和 5-HT 对运动状态的调制
- **Phase 3**：实现完整的神经肽网络

---

### 3.7 第7层：运动控制层（Motor Control Layer）

**职责**：将运动神经元的输出转化为肌肉激活，驱动身体运动。

**核心组件**：

| 组件 | 说明 | 实现语言 |
|------|------|---------|
| `MotorNeuronMapper` | 运动神经元到肌肉段的映射 | C++ |
| `CPGModel` | 中枢模式发生器（可选） | C++ |
| `MotorController` | 运动控制总调度 | C++ |

**运动神经元分类**：

| 类别 | 神经元 | 功能 |
|------|--------|------|
| A 类运动神经元 | DA, VA | 后退运动 |
| B 类运动神经元 | DB, VB | 前进运动 |
| D 类运动神经元 | DD, VD | 交叉抑制（背腹协调） |
| AS 运动神经元 | AS | 背侧肌肉控制 |
| 头部运动神经元 | RMD, RME, SMD, SMB | 头部摆动/转向 |

**神经元→肌肉映射**：
```cpp
class MotorNeuronMapper {
    // 每个运动神经元映射到若干肌肉段
    struct MotorMapping {
        int motor_neuron_id;
        int muscle_segment_start;   // 起始肌肉段
        int muscle_segment_end;     // 结束肌肉段
        bool is_dorsal;             // 背侧 or 腹侧
        double coupling_strength;    // 耦合强度
    };
    std::vector<MotorMapping> mappings_;

    // 将运动神经元输出 → 肌肉激活度
    void compute_muscle_activations(
        const std::vector<double>& motor_outputs,
        BodyModel& body
    );
};
```

**运动模式**：
- **前进**：B 类运动神经元激活，正弦波从头部传向尾部
- **后退**：A 类运动神经元激活，正弦波从尾部传向头部
- **转向（Ω-turn）**：深度弯曲，头部几乎触碰尾部
- **急转（pirouette）**：后退 + 大角度转向组合
- **头部探索**：头部小幅度摆动，采样化学梯度

**⚠️ 本体感觉反馈的关键作用**（Izquierdo & Beer 2018; Boyle et al.）：
> 运动波的传播**不完全依赖 CPG**，而是高度依赖**短程后向本体感觉反馈**：
> - 头部运动神经元 SMD/RMD 足以驱动头颈部的背腹波动
> - 波沿身体向后传播主要依赖 B 类运动神经元的**拉伸受体反馈**
> - 即：前方身体段的弯曲 → 拉伸信号 → 激活后方运动神经元 → 波传播
> - 这意味着身体物理层（第2层）与运动控制层之间存在**紧密耦合的闭环**
>
> 实现时必须确保 body → proprioception → motor neuron → muscle → body
> 的闭环反馈延迟正确。

---

### 3.8 第8层：行为涌现与评估层（Behavior Emergence & Evaluation）

**职责**：监测、记录和评估虚拟线虫的涌现行为，与真实行为对照验证。

**核心组件**：

| 组件 | 说明 | 实现语言 |
|------|------|---------|
| `BehaviorDetector` | 行为模式自动识别 | C++/Python |
| `TrajectoryRecorder` | 运动轨迹记录 | C++ |
| `BehaviorMetrics` | 行为量化指标计算 | Python |
| `Visualizer` | 实时可视化渲染 | Python |

**需验证的核心行为**：

| 行为 | 描述 | 验证指标 |
|------|------|---------|
| 趋化性 (Chemotaxis) | 向食物源移动 | 趋化指数 CI |
| 趋温性 (Thermotaxis) | 沿等温线运动/趋向培养温度 | 温度偏好曲线 |
| 轻触回避 (Touch response) | 前触后退，后触前进 | 反应延迟、后退距离 |
| 鼻触回避 (Nose touch) | 头部碰触后后退+转向 | 后退概率 |
| 习惯化 (Habituation) | 重复刺激后反应减弱 | 衰减曲线 |
| 前进/后退切换 | 自发性运动方向切换 | 切换频率 |
| 觅食策略 | 有食/无食区域的运动模式差异 | 转向频率、速度 |
| Ω-turn | 大角度转弯 | 转弯角度分布 |

---

## 4. 项目目录结构

```
agi4/
├── CMakeLists.txt                       # 顶层 CMake 配置
├── docs/
│   └── c_elegans_blueprint.md           # 本文档
├── data/
│   ├── connectome/                      # 连接组数据
│   │   ├── neurons.csv                  # 302+20 个神经元属性
│   │   ├── chemical_synapses.csv        # 化学突触列表 (Cook 2019 / Emmons 2024)
│   │   ├── gap_junctions.csv            # 电突触列表
│   │   ├── neurotransmitters.csv        # 神经递质分配 (Pereira 2015)
│   │   └── communities.csv             # 10 个功能模块划分 (Emmons 2024)
│   ├── neuron_morphology/               # 神经元形态数据（多隔室模型用）
│   │   └── *.swc                        # SWC 格式的神经元形态文件
│   ├── ion_channels/                    # 离子通道参数
│   │   ├── channel_expression.csv       # 每个神经元的通道表达谱 (WormBase)
│   │   └── channel_kinetics.json        # 14 种通道的动力学参数
│   ├── neuropeptides/                   # 神经肽数据 (Ripoll-Sánchez 2023)
│   │   ├── peptide_receptor_pairs.csv   # 配体-受体配对
│   │   └── expression_map.csv           # 神经元表达图谱
│   ├── muscle_map/                      # 运动神经元-肌肉映射
│   └── behavior_reference/              # 真实行为参考数据 (Yemini 2013)
├── src/
│   ├── core/                            # 核心基础设施
│   │   ├── types.h                      # 基础类型定义 (Vector2d 等)
│   │   ├── math_utils.h                 # 数学工具 (ODE 积分器等)
│   │   ├── config.h                     # 全局配置 (JSON/YAML 解析)
│   │   └── logger.h                     # 日志系统
│   ├── environment/                     # 第1层：环境仿真
│   │   ├── environment.h/.cpp
│   │   ├── chemical_field.h/.cpp
│   │   ├── temperature_field.h/.cpp
│   │   └── substrate_model.h/.cpp
│   ├── body/                            # 第2层：躯体物理
│   │   ├── body_model.h/.cpp
│   │   ├── muscle_system.h/.cpp
│   │   ├── hydrostatic_skeleton.h/.cpp
│   │   ├── pharynx_model.h/.cpp
│   │   ├── locomotion.h/.cpp
│   │   └── collision.h/.cpp
│   ├── sensory/                         # 第3层：感知转导
│   │   ├── sensory_neuron.h/.cpp
│   │   ├── chemo_sensor.h/.cpp
│   │   ├── mechano_sensor.h/.cpp
│   │   ├── thermo_sensor.h/.cpp
│   │   ├── proprioceptor.h/.cpp         # 本体感觉（拉伸受体）
│   │   └── sensor_manager.h/.cpp
│   ├── neuron/                          # 第4层：神经元计算
│   │   ├── neuron.h/.cpp                # 基类（虚接口）
│   │   ├── single_compartment.h/.cpp    # 单隔室模型 (L1/L2)
│   │   ├── multi_compartment.h/.cpp     # 多隔室模型 (L3/L4)
│   │   ├── ion_channel.h/.cpp           # 离子通道基类
│   │   ├── channels/                    # 14+ 种具体通道实现
│   │   │   ├── egl19.h/.cpp             # L-type Ca²⁺
│   │   │   ├── unc2.h/.cpp              # N/P/Q-type Ca²⁺
│   │   │   ├── cca1.h/.cpp              # T-type Ca²⁺
│   │   │   ├── shl1.h/.cpp              # Shaker-like K⁺
│   │   │   ├── slo1.h/.cpp              # BK K⁺
│   │   │   └── ...                      # 其余通道
│   │   ├── calcium_dynamics.h/.cpp
│   │   └── neuron_factory.h/.cpp
│   ├── connectome/                      # 第5层：连接组与突触（有线）
│   │   ├── connectome.h/.cpp
│   │   ├── chemical_synapse.h/.cpp
│   │   ├── gap_junction.h/.cpp
│   │   └── connectome_loader.h/.cpp
│   ├── neuromodulation/                 # 第6层：神经调质（无线）
│   │   ├── neuromodulator.h/.cpp
│   │   ├── neuropeptide_system.h/.cpp
│   │   ├── monoamine_system.h/.cpp
│   │   └── modulation_manager.h/.cpp
│   ├── motor/                           # 第7层：运动控制
│   │   ├── motor_neuron_mapper.h/.cpp
│   │   ├── motor_controller.h/.cpp
│   │   └── proprioceptive_feedback.h/.cpp
│   ├── behavior/                        # 第8层：行为涌现
│   │   ├── behavior_detector.h/.cpp
│   │   └── trajectory_recorder.h/.cpp
│   └── simulation/                      # 仿真主循环
│       ├── simulation_engine.h/.cpp     # 仿真引擎（总调度）
│       ├── parameter_optimizer.h/.cpp   # 参数优化器
│       └── main.cpp                     # 入口
├── python/
│   ├── analysis/                        # 行为分析脚本
│   │   ├── behavior_metrics.py
│   │   └── trajectory_analysis.py
│   ├── visualization/                   # 可视化
│   │   ├── realtime_viewer.py           # 实时 2D/3D 可视化
│   │   ├── neural_activity_viewer.py    # 神经活动热图
│   │   └── plot_results.py
│   ├── data_tools/                      # 数据处理工具
│   │   ├── parse_connectome.py          # 解析 Cook/Emmons 数据
│   │   ├── parse_neuropeptides.py       # 解析 Ripoll-Sánchez 数据
│   │   ├── parse_morphology.py          # 解析 SWC 形态文件
│   │   └── validate_data.py
│   └── optimization/                    # 参数优化
│       ├── fit_synaptic_weights.py      # 突触权重拟合
│       └── fit_ion_channels.py          # 离子通道参数拟合
└── tests/
    ├── test_neuron.cpp
    ├── test_ion_channels.cpp
    ├── test_synapse.cpp
    ├── test_body.cpp
    └── test_chemotaxis.cpp
```

---

## 5. 仿真主循环

```cpp
class SimulationEngine {
    Environment environment_;
    BodyModel body_;
    SensorManager sensors_;
    std::vector<std::unique_ptr<Neuron>> neurons_;  // 302 个体神经元 (+20 咽部)
    Connectome connectome_;
    NeuromodulationManager neuromodulation_;          // 第6层：无线信号
    MotorController motor_controller_;
    BehaviorDetector behavior_detector_;
    TrajectoryRecorder recorder_;

    double dt_;             // 时间步长 (推荐 0.1-0.5 ms)
    double current_time_;

public:
    void initialize(const Config& config);

    void step() {
        // 1. 环境更新（化学扩散、温度变化等）
        environment_.step(dt_);

        // 2. 感知采样：环境刺激 → 感觉神经元输入电流
        //    包括化学、机械、温度、本体感觉（身体曲率/拉伸）
        sensors_.sample_environment(environment_, body_);

        // 3. 神经调质更新（第6层）：更新旁分泌信号浓度，
        //    调制突触权重和神经元兴奋性（慢时间尺度，可降频执行）
        neuromodulation_.update(neurons_, dt_);

        // 4. 突触电流计算（第5层）：
        //    a. 化学突触：分级递质释放 → 突触后电流
        //    b. 电突触（间隙连接）：欧姆耦合电流
        connectome_.compute_synaptic_currents(neurons_);

        // 5. 神经元膜电位更新（第4层）：
        //    整合漏电流、离子通道电流、突触电流、外部电流
        for (auto& neuron : neurons_) {
            neuron->step(dt_);
        }

        // 6. 运动输出（第7层）：运动神经元 → 肌肉激活度
        motor_controller_.update(neurons_, body_);

        // 7. 身体物理更新（第2层）：
        //    肌肉力 → 身体变形 → 与环境交互 → 位移
        body_.update_physics(dt_);

        // 8. 行为检测与记录（第8层）
        behavior_detector_.analyze(body_, current_time_);
        recorder_.record(body_, neurons_, current_time_);

        current_time_ += dt_;
    }
};
```

**时间步长注意事项**：
- 神经元动力学需要 ~0.1 ms 步长才能稳定求解（取决于最快的离子通道时间常数）
- 身体物理可以用更大步长（~1 ms），可通过子步进解耦
- 神经调质更新可以每 10-100 ms 执行一次（慢时间尺度）
- BAAIWorm 的经验：脑模型和身体模型可以以不同频率耦合

---

## 6. 关键技术挑战与决策点

### 6.1 神经元模型选择
- **分级电位 vs 动作电位**：大部分神经元使用分级电位，但 AWA（Liu 2018）
  已被证实产生钙介导动作电位，RMD 表现出双稳态振荡（Nicoletti 2019）。
- **单隔室 vs 多隔室**：BAAIWorm（2024）证明多隔室模型效果显著优于单隔室。
  但多隔室计算成本更高。建议 MVP 使用单隔室，关键神经元逐步升级。
- **策略**：默认 L2（HH 分级），对 AWA/RMD/RIA 等使用 L3（多隔室+钙动力学）。

### 6.2 突触权重标定
- 连接组数据提供的是 **EM 切面数**（解剖强度代理），不等于生理权重。
- **初始方案**：权重 ∝ EM 切面数 × 兴奋/抑制符号
- **拟合方案**（BAAIWorm 方法）：
  - 目标数据：全脑钙成像的神经元活动相关矩阵（Kato et al. 2015）
  - 优化方法：基于梯度的参数优化（Adam/SGD），支持多 GPU 并行
  - 损失函数：膜电位相关矩阵或钙信号轨迹的 MSE
- **注意**：突触的兴奋/抑制性质由神经递质类型决定，
  但 ~30% 神经元的递质类型尚未完全确认。

### 6.3 身体-神经闭环耦合
- 身体物理仿真与神经仿真需要紧密耦合（不能分别独立运行）。
- **关键闭环**：
  1. 化学感觉闭环：头部位置 → 化学采样 → 感觉神经元 → 转向决策 → 头部位置
  2. 本体感觉闭环：身体曲率 → 拉伸受体 → B 类运动神经元 → 肌肉 → 身体曲率
  3. 触觉闭环：碰撞 → 机械感觉神经元 → 后退/前进切换 → 运动
- BAAIWorm 采用 CNN 中间层耦合脑模型和身体模型（可参考但非必须）。

### 6.4 参数不确定性
- **离子通道参数**：仅 AWC^ON 和 RMD 有较完整的电生理数据（Nicoletti 2019），
  其余神经元的通道组成和密度大多基于 WormBase 基因表达推断。
- **膜电容**：典型值 ~1-3 pF，但具体值因神经元而异。
- **策略**：
  1. 使用 Nicoletti 2019 的 14 通道参数作为基础
  2. 根据基因表达数据（WormBase）为每个神经元配置通道组合
  3. 通过行为拟合优化未知参数
  4. 可使用 BAAIWorm 提供的参数调优工具

### 6.5 计算性能
- **单隔室模型**：302 个神经元 + ~7000 突触，单核 CPU 即可实时。
- **多隔室模型**：每个神经元 10-100 个隔室，总计 ~10000 个方程，
  仍可单核运行但可能低于实时。
- **SPH 身体物理**：计算量较大，需要 GPU（OpenCL/CUDA）。
- **参数优化**：大规模参数搜索需要 GPU 并行（BAAIWorm 使用 NVIDIA 3090）。
- **推荐**：MVP 阶段用弹性杆 + 单隔室，全部单核 CPU 可运行。

### 6.6 多信息整合的复杂性（新发现）
- Emmons 2024 揭示：C. elegans 的信息整合**不是集中式的**，
  而是**分布式地发生在网络的所有层级**——包括感觉神经元、中间神经元和肌肉。
- 大多数中间神经元的入度和出度相近，说明它们既聚合信息也分散信息。
- 这意味着不能简单地将网络理解为"感觉→处理→运动"的线性流水线。

### 6.7 行为验证的标准化
- OpenWorm 开发了**运动验证引擎**，基于 Yemini et al. 2013 的 C. elegans
  行为数据库（包含运动速度、转向频率、Ω-turn 频率等定量指标）。
- 但该数据库仅包含**细菌层上自由爬行**的数据，缺乏其他刺激条件下的数据。
- 需要补充趋化性、触觉回避等特定行为的实验数据作为验证基准。

---

## 7. 数据依赖

| 数据 | 来源 | 格式 | 获取方式 |
|------|------|------|---------|
| 连接组（神经元列表） | Cook 2019 / Emmons 2024 | CSV | WormWiring.org 补充数据 |
| 化学突触连接 | Cook et al. 2019 (S1 File) | CSV | Nature 补充材料 |
| 电突触连接 | Cook et al. 2019 (S1 File) | CSV | Nature 补充材料 |
| 连接组模块划分 | Emmons 2024 (S1-S9 Files) | CSV | PLOS Biology 补充材料 |
| 神经递质分配 | Pereira et al. 2015 | CSV | 论文补充材料 |
| 神经肽连接组 | Ripoll-Sánchez et al. 2023 | CSV | Neuron 补充材料 |
| 离子通道表达谱 | WormBase (gene expression) | 数据库查询 | wormbase.org |
| 离子通道动力学 | Nicoletti et al. 2019 (S1) | 方程/参数 | PLOS ONE 补充材料 |
| 神经元形态 | OpenWorm/CElegansNeuroML | SWC/NeuroML | GitHub 仓库 |
| 运动神经元-肌肉映射 | WormAtlas | CSV | wormatlas.org |
| 感觉神经元模态 | WormAtlas / 文献综合 | CSV | wormatlas.org |
| 电生理参数 | Goodman, Mellem, Liu 等 | 配置文件 | 各论文 |
| 行为参考数据 | Yemini et al. 2013 | HDF5/CSV | C. elegans 行为数据库 |
| 全脑钙成像数据 | Kato et al. 2015 | 矩阵 | 用于参数拟合 |
| BAAIWorm 参考实现 | Jessie940611/BAAIWorm | C++/Python | GitHub 仓库 |

---

## 8. 开发路线图

### Phase 1：基础骨架（MVP）— 约 4-6 周
- [ ] 搭建项目结构与 CMake 构建系统
- [ ] 实现核心类型和数学工具（Vector2d, ODE 积分器）
- [ ] 实现单隔室 HH 型分级电位神经元模型（L2）
- [ ] 实现 3-4 种基础离子通道（EGL-19, SHL-1, KQT-3, 漏通道）
- [ ] 加载连接组数据（302 神经元 + 化学突触 + 电突触）
- [ ] 实现分级化学突触和间隙连接
- [ ] 简化身体模型（2D 弹性杆，48 段）
- [ ] 实现本体感觉反馈（身体曲率 → 拉伸受体信号）
- [ ] 实现基本前进/后退运动（B/A 类运动神经元 → 肌肉）
- [ ] **验证目标**：虚拟线虫能在空旷环境中蠕动前进

### Phase 2：感觉-运动回路 — 约 4-6 周
- [ ] 实现化学感觉转导（AWA, AWC, ASE 浓度导数检测）
- [ ] 实现环境化学梯度场
- [ ] 实现机械触觉（ALM/PLM 前后触觉）
- [ ] 实现趋化性行为（pirouette 策略 + weathervane 策略）
- [ ] 实现轻触回避反射（前触后退 + 转向）
- [ ] 实现头部运动神经元（SMD/RMD）驱动的头部摆动
- [ ] **验证目标**：趋化指数 CI > 0.5；触觉后正确后退

### Phase 3：完整行为验证 — 约 6-8 周
- [ ] 实现温度感觉（AFD）和趋温性
- [ ] 扩展离子通道库（全部 14 种）
- [ ] 对关键神经元升级为多隔室模型（AWA, RMD, RIA）
- [ ] 实现习惯化（重复触觉刺激衰减）
- [ ] 实现 Ω-turn 和 pirouette 运动模式
- [ ] 实现觅食策略切换（有食区高转向 → 无食区低转向）
- [ ] 实现多巴胺/5-HT 对运动状态的基本调制（第6层 MVP）
- [ ] 与 Yemini 2013 行为数据库定量对比
- [ ] **验证目标**：运动特征（速度、转向频率等）落在实验分布范围内

### Phase 4：优化与高保真扩展 — 持续迭代
- [ ] 基于钙成像数据的突触权重系统优化
- [ ] 神经肽网络实现（第6层完整版）
- [ ] 咽部神经系统实现
- [ ] SPH 身体物理引擎（替换弹性杆，可选）
- [ ] 实时 3D 可视化（参考 BAAIWorm/neuronXcore）
- [ ] 发育连接组支持（Witvliet 2021，不同龄期）
- [ ] 突触可塑性与学习
- [ ] 社交行为（多虫仿真）

---

## 9. 与现有项目的对比

| 特性 | OpenWorm | BAAIWorm (2024) | 本项目 |
|------|----------|-----------------|--------|
| 神经元模型 | 多级 (c302) | 多隔室 HH | 多级可切换 (L1-L4) |
| 身体物理 | SPH (Sibernetic) | 3D 物理引擎 | 弹性杆(MVP) → SPH |
| 连接组 | Varshney 2011 | Cook 2019 | Emmons 2024 (最新) |
| 神经调质 | 无 | 无 | 有（第6层） |
| 闭环仿真 | 部分 | 完整闭环 | 完整闭环 |
| 核心语言 | Python/Java | C++/Python | C++/Python |
| 参数优化 | 手动 | GPU 梯度优化 | 梯度优化 |
| 行为验证 | 运动引擎 | 轨迹对比 | Yemini 数据库 |

**本项目的差异化**：
1. 采用最新 Emmons 2024 连接组分析（含肌肉、10 模块结构）
2. 包含神经肽"无线连接组"层（Ripoll-Sánchez 2023）
3. 神经元模型可在 L1-L4 之间灵活切换
4. 全 C++ 核心，性能优先

---

## 10. 参考文献

### 连接组与神经解剖
1. White, J.G. et al. (1986). "The structure of the nervous system of C. elegans." *Phil. Trans. R. Soc. Lond. B* 314:1-340.
2. Cook, S.J. et al. (2019). "Whole-animal connectomes of both C. elegans sexes." *Nature* 571:63-71.
3. Emmons, S.W. (2024). "Comprehensive analysis of the C. elegans connectome reveals novel circuits." *PLOS Biology* 22(12):e3002939.
4. Varshney, L.R. et al. (2011). "Structural properties of the C. elegans neuronal network." *PLOS Comput. Biol.*
5. Witvliet, D. et al. (2021). "Connectomes across development reveal principles of brain maturation." *Nature* 596:257-261.

### 神经元电生理与离子通道
6. Nicoletti, M. et al. (2019). "Biophysical modeling of C. elegans neurons: Single ion currents and whole-cell dynamics of AWC^ON and RMD." *PLOS ONE* 14(7):e0218738.
7. Liu, Q. et al. (2018). "C. elegans AWA olfactory neurons fire calcium-mediated all-or-none action potentials." *Cell* 175(1):57-70.
8. Hendricks, M. et al. (2012). "Compartmentalized calcium dynamics in a C. elegans interneuron encode head movement."

### 神经调质与神经肽
9. Ripoll-Sánchez, L. et al. (2023). "The neuropeptidergic connectome of C. elegans." *Neuron* 111(22):3570-3589.
10. Pereira, L. et al. (2015). "A cellular and regulatory map of the cholinergic nervous system of C. elegans." *eLife* 4:e12432.

### 计算模型与仿真
11. Gleeson, P. et al. (2018). "c302: a multiscale framework for modelling the nervous system of C. elegans." *Phil. Trans. R. Soc. B* 373:20170379.
12. Izquierdo, E.J. & Beer, R.D. (2018). "From head to tail: a neuromechanical model of forward locomotion in C. elegans." *Phil. Trans. R. Soc. B* 373:20170374.
13. BAAIWorm (2024). "An integrative data-driven model simulating C. elegans brain, body and environment." *Nature Computational Science* 4:967-977.
14. Boyle, J.H. et al. (2012). "Gait modulation in C. elegans: an integrated neuromechanical model." *Frontiers Comput. Neurosci.*

### 行为数据与验证
15. Yemini, E. et al. (2013). "A database of C. elegans behavioral phenotypes." *Nature Methods* 10:877-879.
16. Kato, S. et al. (2015). "Global brain dynamics embed the motor command sequence of C. elegans." *Cell* 163(3):656-669.

### 开源项目与数据库
17. OpenWorm project: http://openworm.org
18. WormAtlas: http://wormatlas.org
19. WormWiring: http://wormwiring.org
20. WormBase: http://wormbase.org
21. BAAIWorm GitHub: https://github.com/Jessie940611/BAAIWorm
22. OpenWorm/Sibernetic: https://github.com/openworm/sibernetic
23. OpenWorm/c302: https://github.com/openworm/c302

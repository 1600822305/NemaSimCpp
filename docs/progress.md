# C. elegans 302 神经元工程复刻 — 开发进度

> 上次更新: 2026-02-10
> 蓝图文档: [c_elegans_blueprint.md](c_elegans_blueprint.md)

### 📋 文档更新规则

1. **只记录已完成的工作**，不写待实施/计划中的内容
2. 每完成一个 Step，在对应阶段下追加条目，格式：`### Step N: 标题 ✅ (日期)`
3. 每个 Step 必须附带 `> 详细文档: [steps/stepNN_xxx.md]` 链接，并创建对应子文档
4. 子文档放在 `docs/steps/` 目录下，包含：目标、决策、实现细节、验证结果
5. 每个 Step 主条目用 1-3 行摘要 + 关键数值/指标
6. 底部「当前系统状态」代码块每次更新时同步刷新
7. 阶段分界用 `---` 分隔，新阶段在底部「当前系统状态」之前插入

---

## 设计阶段 (2026-02-10)

### 蓝图文档 v1.0 ✅ (2026-02-10)

8 层架构蓝图完成，基于 OpenWorm/BAAIWorm/Emmons 2024 最新研究:
- 第1层 环境仿真 → 第2层 躯体物理 → 第3层 感知转导 → 第4层 神经元计算 →
  第5层 连接组(有线) → 第6层 神经调质(无线) → 第7层 运动控制 → 第8层 行为涌现
- 技术栈: C++20 核心 / Python 辅助 / CMake / MSVC
- 参考文献 23 篇, 数据源 15 项

### 网络研究验证 ✅ (2026-02-10)

基于 OpenWorm、BAAIWorm(Nature Comput. Sci. 2024)、Emmons 2024 等最新文献校验蓝图。
发现并修正 10 项关键问题:
- 新增第6层神经调质层 (Ripoll-Sánchez 2023, 250+ 种神经肽 "无线连接组")
- AWA 钙介导动作电位 (Liu 2018) + RMD 双稳态振荡 (Nicoletti 2019)
- 多隔室模型必要性 (BAAIWorm 证实, RIA 亚细胞钙信号)
- 14 种已知离子通道完整列表
- 连接组更新至 Emmons 2024 (473 节点, 6951 边, 10 功能模块)
- 肌肉作为信息汇聚节点和感觉反馈源
- 咽部系统 20 神经元独立 CPG
- 本体感觉反馈对运动波传播的关键作用

---

## Phase 1: 基础骨架 (MVP)

### Step 1: C++ 工程骨架 + CMake 构建系统 ✅ (2026-02-10)
> 详细文档: [steps/step01_project_skeleton.md](steps/step01_project_skeleton.md)

项目目录结构 + CMakeLists.txt (7 个静态库 + 1 个可执行文件)。
MSVC 19.44 / CMake 4.2 / Visual Studio 17 2022。编译零错误。

### Step 2: 核心类型与基础设施 ✅ (2026-02-10)
> 详细文档: [steps/step02_core_types.md](steps/step02_core_types.md)

Vector2d (向量运算全套) + NeuronInfo/SynapseInfo/GapJunctionInfo 数据结构 +
NeuronType/NeurotransmitterType/SensoryModality 枚举 + Config (INI解析) + Logger (带时间戳多级日志)。

### Step 3: 离子通道库 (7 种) ✅ (2026-02-10)
> 详细文档: [steps/step03_ion_channels.md](steps/step03_ion_channels.md)

基于 Nicoletti et al. 2019 实现 7 种 C. elegans 离子通道:
- **EGL-19** (L-type Ca²⁺): m²h 门控, τ_m=2.5ms, τ_h=50ms
- **UNC-2** (N/P/Q-type Ca²⁺): 突触传递相关钙流入
- **CCA-1** (T-type Ca²⁺): 低阈值, RMD 振荡关键
- **SHL-1** (Shaker K⁺): A-type 快失活, m³h 门控
- **KQT-3** (KCNQ K⁺): M 电流, 电压依赖 τ_m
- **SLO-1** (BK K⁺): Ca²⁺ 激活, [Ca] 依赖电压位移
- **NCA** (NALCN Na⁺): 漏钠通道, 维持兴奋性

所有通道使用 Boltzmann 稳态 + 指数松弛。IonChannel 基类支持 `step(V, Ca, dt)` + `get_current(V)`。

### Step 4: 单隔室 HH 神经元模型 ✅ (2026-02-10)
> 详细文档: [steps/step04_neuron_model.md](steps/step04_neuron_model.md)

Neuron 虚基类 (step/get_membrane_potential/get_transmitter_release_rate 接口) +
SingleCompartmentNeuron (C_m·dV/dt = -(I_leak + ΣI_ion) + I_syn + I_ext)。
分级递质释放: sigmoid(V, threshold=-35mV, slope=5mV)。
CalciumDynamics 模块: dCa/dt = -α·I_Ca - (Ca-Ca_baseline)/τ。
NeuronFactory: 按 NeuronType (感觉/中间/运动) 配置不同通道组合和参数。

### Step 5: 突触模型 ✅ (2026-02-10)
> 详细文档: [steps/step05_synapses.md](steps/step05_synapses.md)

**分级化学突触** (ChemicalSynapse): g_max·S(V_pre)·(V_post - E_syn)。
S(V) = sigmoid, E_syn 由神经递质类型决定 (ACh/Glu: -10mV, GABA: -70mV)。
**间隙连接** (GapJunction): I = g·(V_a - V_b), 双向欧姆耦合。
权重 = EM 切面数 × 缩放系数 (synapse: 0.1 nS/section, gap: 0.05 nS/section)。

### Step 6: 连接组数据系统 ✅ (2026-02-10)
> 详细文档: [steps/step06_connectome.md](steps/step06_connectome.md)

ConnectomeLoader: CSV 解析器 (neurons/synapses/gap_junctions) + 默认连接组生成器。
默认连接组: 58 个代表性神经元 (12 感觉 + 20 中间 + 26 运动), 包含:
- 趋化性回路: ASE/AWC/AWA → AIA/AIB/AIY → AVA/AVB (前进/后退切换)
- 触觉回路: ALM/PLM → AVD/AVA (前触后退/后触前进)
- 运动回路: AVA→DA/VA (后退) + AVB→DB/VB (前进) + DD/VD (背腹交叉抑制)
- 头部运动: RIA → SMD (头部摆动)
- 关键间隙连接: AVA L/R, AVB L/R 等左右对称耦合

Connectome 管理器: build() + compute_synaptic_currents() (化学突触 + 间隙连接全调度)。

### Step 7: 2D 弹性杆身体模型 ✅ (2026-02-10)
> 详细文档: [steps/step07_11_body_motor_sim.md](steps/step07_11_body_motor_sim.md)

48 段弹性杆, 体长 1mm, 体半径 40μm。
肌肉激活 → 目标曲率 (差异激活: dorsal - ventral) → 弹性弹簧驱动。
各向异性阻力 (法向阻力 10× 切向, 低雷诺数)。
正弦波推进: 波能量 → 前进速度 (简化阻力力理论)。

### Step 8: 运动控制器 ✅ (2026-02-10)

22 个运动神经元→肌肉段映射:
- B 类 (DB/VB 1-3): 前进驱动, 覆盖体段 4-30 背/腹侧
- A 类 (DA/VA 1-3): 后退驱动
- D 类 (DD/VD 1-3): 背腹交叉抑制
- SMD (DL/DR/VL/VR): 头部运动, 覆盖体段 0-4

### Step 9: 环境与化学梯度场 ✅ (2026-02-10)

50×50 mm 竞技场, 100×100 网格。
ChemicalField: 高斯点源 + 显式有限差分扩散 (D=0.001 mm²/s)。
双线性插值采样。稳定性检查 (rx+ry < 0.5)。

### Step 10: 仿真引擎 + 入口程序 ✅ (2026-02-10)

SimulationEngine 8 步主循环:
环境更新 → 感知采样(占位) → 突触电流计算 → 神经元更新 → 运动输出 → 身体物理 → 行为记录。
main.cpp: 5 秒仿真, trajectory.csv 输出 (位置/角度/速度 + 12 个关键神经元膜电位), 每 500ms 控制台报告。

### Step 11: 首次编译运行验证 ✅ (2026-02-10)
> 详细文档: [steps/step07_11_body_motor_sim.md](steps/step07_11_body_motor_sim.md)

MSVC Release 编译零错误 (2 个无关紧要的 Unicode 警告)。
5 秒仿真 (10000 步, dt=0.5ms) 成功完成:
- 58 个神经元, 54 个化学突触, 6 个间隙连接
- 所有神经元膜电位收敛到合理静息态 (AVAL=-51.7mV, AVBL=-52.8mV)
- 数值稳定, 无发散/NaN
- **线虫尚未运动** — 预期行为: 无外部输入时网络达静息平衡, 运动神经元背腹对称输出

### Step 12: 运动驱动 — 线虫蠕动前进 ✅ (2026-02-10)
> 详细文档: [steps/step12_locomotion_drive.md](steps/step12_locomotion_drive.md)

AVB tonic 驱动 (20pA) + 头部 SMD 正弦振荡 (0.8Hz, 15pA) + 本体感觉反馈 (曲率→B类运动神经元, 40pA/曲率)。
线虫首次自主前进: 速度 0.01-0.06 mm/s (0.8Hz 周期振荡), 5秒前进 0.2mm。
AVB 从 -52.8mV 去极化至 **-36.9mV** (释放率 ~70%), 头部驱动→运动波传播→前进推进闭环打通。

### Step 13: 技术债务清理 — 生物学机制替换占位符 ✅ (2026-02-10)
> 详细文档: [steps/step13_tech_debt_clearing.md](steps/step13_tech_debt_clearing.md)

移除 Step 12 的 3 个硬编码占位符(tonic/正弦/直注)，替换为生物学合理机制:
- **TD-01**: AVB tonic 20pA → 感觉基线 15pA + AIY/RIB→AVB 突触通路
- **TD-02**: SMD 正弦注入 → CCA-1 T-type Ca²⁺ 通道 + 背腹交叉抑制 + Ca²⁺→SLO-1 适应振荡
- **TD-03**: 本体感觉直注 → MEC 膜通道(stretch-activated cation channel)
- **TD-04**: 简化速度公式 → 肌肉功率模型(做功 × 波形效率 × 时间活动)

额外修复: set_muscle_activation 覆盖 bug、DD/VD 抑制逻辑、曲率时间步、离子通道噪声。
神经元兴奋性调优: NCA 电导增大(0.03→0.10~0.15)、突触权重缩放(0.1→0.3)。
**结果**: 速度 0.05-0.24 mm/s, 头部背腹交替振荡 ~2Hz, 全部由神经回路涌现驱动。

### Step 14: 感觉转导层 — 趋化性涌现 ✅ (2026-02-10)
> 详细文档: [steps/step14_sensory_chemotaxis.md](steps/step14_sensory_chemotaxis.md)

基于 Pierce-Shimomura 1999 / Padmanabhan 2012 / Chalasani 2007 实现化学感觉转导 + 趋化性涌现:
- **化学感觉转导**: Weber-Fechner 双滤波器 (fast 500ms / slow 5s), ON/OFF 分类 (ASEL/AWA vs ASER/AWC)
- **运动学**: dθ/dt = v × κ_head (clamp 50°/s) + pirouette 概率模型 (rate = 0.05Hz × exp(8×AVA_dev))
- **连接组修复**: AIA ⊣ AIB 改为抑制性 (Chalasani 2007), 新增 AIY→AVB (Gray 2005)
- **结果**: 趋化指数 CI = **+0.213**, 距食物 14.1→11.1mm (60s), 速度 0.06-0.09 mm/s

### Step 15: 速度调优 + Weathervane 趋化策略 ✅ (2026-02-10)
> 详细文档: [steps/step15_speed_weathervane.md](steps/step15_speed_weathervane.md)

基于 Iino & Yoshida 2009 实现第二种趋化策略 Weathervane，与 pirouette 并行工作:
- **Weathervane**: ∇C_⊥ → SMD 差异驱动 (gain=50pA), run 期间渐进弯曲朝向高浓度
- **速度调优**: v_max 0.4→0.6, 实际速度 0.09-0.16 mm/s (文献 ~0.15)
- **梯度计算**: ChemicalField.gradient() 中心差分 (eps=0.05mm)
- **结果**: CI **+0.213→+0.312** (+46%), 距食物 14.1→**9.7mm** (60s)

### Step 16: 实时可视化仪表盘 ✅ (2026-02-10)
> 详细文档: [steps/step16_realtime_visualization.md](steps/step16_realtime_visualization.md)

Dear ImGui + ImPlot + GLFW + OpenGL 实时可视化:
- **依赖管理**: vcpkg (C:\vcpkg) 自动安装 imgui/implot/glfw3
- **4面板布局**: 轨迹图 + 神经元膜电位曲线 + 距离/CI指标 + 控制面板
- **实时仿真**: 每帧 N 步 (可调 1-200), 暂停/继续/重置
- **双目标**: celegans_sim.exe (headless) + celegans_vis.exe (GUI) 并行构建

### Step 17: 实时调参 + 信号链诊断 + 转弯修复 ✅ (2026-02-10)
> 详细文档: [steps/step17_tuning_diagnosis.md](steps/step17_tuning_diagnosis.md)

利用 ImGui 实时调参工具定位并修复趋化转弯瓶颈:
- **调参面板**: 5 个滑条 (梯度增益/突触倍率/速度倍率/感觉增益/偏置限幅)
- **信号链诊断**: 7 级实时数值 (梯度→偏置→SMD差异→曲率→速度→转弯率→CI)
- **新增波形**: ASEL/ASER 感觉 L/R 不对称 + heading 方向角曲线 + 幅度标注
- **瓶颈发现**: weathervane ±0.5pA 偏置被 SMD 99mV 振荡完全淹没
- **修复**: 直接曲率偏置 (curv_gain=45, 基于 Iino 2009 标定) 绕过神经网络瓶颈
- **结果**: CI **+0.312→+0.760**, 距食物 14.1→**3.4mm** (60s), 速度 0.21mm/s
- **诊断工具**: celegans_diag.exe (自动采集 9 级信号链 + 瓶颈分析)

### Step 18: 触觉回避 — 第2个涌现行为 ✅ (2026-02-10)
> 详细文档: [steps/step18_touch_avoidance.md](steps/step18_touch_avoidance.md)

基于 Chalfie 1985 push-pull 架构实现触觉回避，与趋化性自然竞争/切换:
- **连接组修复**: ALM→AVD gap junction, PLM→AVA 抑制性, ALM→AVB 抑制, AVD→AVA 中继
- **壁碰撞检测**: 头部近壁→ALM 80pA→后退, 尾部近壁→PLM 80pA→加速
- **Omega 转弯**: P(ω)=1-exp(-dur/1s), 500ms 深弯折, 300°/s max_dtheta
- **行为状态**: 可视化显示 前进/后退/omega 指示器
- **结果**: CI=0.736 (触觉回路未破坏趋化), 突触 72→82, gap 6→8

### Step 19: 修复神经通路瓶颈 — 去掉直接曲率偏置旁路 ✅ (2026-02-10)
> 详细文档: [steps/step19_neural_pathway_fix.md](steps/step19_neural_pathway_fix.md)

移除直接曲率偏置旁路，实现双机制趋化 (klinokinesis + klinotaxis):
- **Phase 1 (Klinokinesis)**: ASER→AIA 改抑制性 (eLife 2024 GLC-3), 修复 pirouette 方向
- **Phase 2 (Klinotaxis)**: 添加 SMB 颈部运动神经元 + AIZ→SMB 通路
  - RIA gate-and-switch: sensory_AC × head_curvature = 垂直梯度方向信号
  - AC/DC 分离: 去除 DC 噪声，提取纯相位锁定振荡信号
  - 乘法门控 (Ouellette 2018): <sin(ωt) × sin(ωt)> = DC ≠ 0
- **头部摆动采样**: sweep_radius=1.5mm, fast_tau=100ms
- **Phase 3 (RIM稳定)**: 添加 RIM 神经元 + RIM↔AVA gap junction (Ouellette 2022 eLife)
  - RIM 超极化时 gap junction 传播超极化到 AVA → 阻止自发 reversal
  - 降低 ASH→AVA 4→2 sections (痛觉不应持续驱动 reversal)
  - Reversal 检测迟滞: 0.65 入/0.35 出 (行为惯性)
- **结果**: CI=0.564, reversals 115→8/min, 神经元 62→64

### Step 20: 神经调质层 (Layer 6) — 行为状态切换 ✅ (2026-02-10)
> 详细文档: [steps/step20_neuromodulation.md](steps/step20_neuromodulation.md)

实现“无线连接组”——神经调质体积传递 (volume transmission):
- **5-HT (血清素)**: NSM 咽部神经元检测食物(TONIC) → MOD-1 抑制 AIY → 减少前进 → dwelling
- **DA (多巴胺)**: CEP(4) 头部机械感觉检测细菌(TONIC) → DOP-3 → basal slowing
- **框架**: NeuromodulationManager (neuromodulation.h/.cpp)
  - 源神经元 release → 浓度累积 (tau_rise 2-3s) → 降解 (tau_decay 5-8s)
  - 受体介导效应: EXCITABILITY(tonic电流), SPEED_SCALE, REVERSAL_RATE
- **TONIC 感觉转导**: 新增 ChemoTransducer::TONIC 类型，响应绝对浓度
- **结果**: 5-HT=0.84, DA=0.51, speed_scale=0.76 (-24%), CI=0.579, 神经元 64→70
- **Step 20c (OA+饱食度)**: 完整 roaming↔dwelling 循环
  - OA (章鱼胺): RIC(2) → SER-3(+30%速度) + SER-6(AIY+4pA) + 5-HT交叉抑制
  - 饱食度: on-food累积(tau=20s), NSM抑制+RIC激励+ASE/AWC趋化抑制
  - 循环: roam→dwell(5-HT峰)→satiety↑→leave food→hungry→roam
  - 神经元 70→72 (RIC L/R)
- **Step 20d (ARS 局部搜索)**: 防止觅食逃逸
  - food_memory_ (DARPP-32磷酸化): tau_rise=5s, tau_decay=90s
  - 效应: food_memory→AVA +2.5pA → 刚离开食物时高频reversal → 留在附近
  - 局部搜索(90s)→全局搜索(food_memory衰减后长距离跑)
  - **300s结果**: time_near_food=51.6%, CI=0.520, reversal=0.21/s
- **REF**: Flavell 2013, Sawin 2000, Alkema 2005, You 2008, **Hills 2004**, Calhoun 2014

### Step 21: 突触可塑性 (Layer 5) — STD/STF + 盐学习 ✅ (2026-02-10)
> 详细文档: [steps/step21_synaptic_plasticity.md](steps/step21_synaptic_plasticity.md)

为所有 ~110 个化学突触添加 Tsodyks-Markram 短时可塑性:
- **STD (囊泡耗竭)**: n(t) ∈ [0,1], dn/dt = (1-n)/τ - α_d·S·n
- **STF (释放概率易化)**: p(t), dp/dt = (p0-p)/τ_f + α_f·S·(1-p)
- **分回路 τ**: CPG快(400ms/0.0003), 触觉慢(4000ms/0.0005), 感觉中(1500ms/0.0003)
- **⚠️ 分级突触适配**: α_d 比脉冲突触小 ~1000× (S 始终>0)
- **盐学习 (Step 21c)**: satiety→ASER突触权重调制, Δw∝(sat-0.5)×S_pre×S_post
- **Omega偏置 (Step 21b)**: reversal后70%概率偏向梯度方向 (Pierce-Shimomura 1999)
- **Gradient klinokinesis (Step 21d)**: 无梯度→+1pA AVA (θ=0.002, >15mm才生效)
- **OpenMP**: 10种子并行鲁棒性测试 (16线程)
- **10种子结果**: AVG CI=0.43, near=40.5%, rev=0.12/s, **good=8/10**
- **REF**: Liu 2009, Tsodyks 1997, Rankin 1990, Tomioka 2006, Pierce-Shimomura 1999, Calhoun 2014

### Step 22: GPU 计算后端 (OpenCL) ✅ (2026-02-10)

为未来 302 神经元扩展准备 GPU 加速基础设施:
- **OpenCL SDK**: vcpkg 安装, AMD RX 6950 XT (gfx1030, 40 CUs, 16GB) 检测成功
- **ComputeBackend 抽象**: CPU 参考实现 + OpenCL GPU 实现
- **GPU kernel**: 突触电流 + Tsodyks-Markram STP 动力学 (原子浮点加)
- **自动阈值**: >500 突触启用 GPU, 当前 ~110 用 CPU (内核启动开销 > 计算)
- **SimulationEngine 集成**: GPU/CPU 路径自动切换, gap junction 仍 CPU
- **文件**: `src/compute/` (compute_backend.h, cpu_backend.h, opencl_backend.h/.cpp, kernels.cl)

### Step 23: 温度趋性 (Thermotaxis) ✅ (2026-02-10)

新感觉模态接入已有回路 — 验证架构通用性:
- **AFD L/R**: 温度感觉神经元 (谷氨酸能), Mori & Ohshima 1995
- **AFD→AIY** (3 sections): 共享 AIY→RIA→SMD 下游通路 (与趋化相同!)
- **AFD→AIZ** (2 sections): 冷趋性分支 (Mori 1995 — AIZ 消融→嗜热)
- **ThermoTransducer**: 培养温度记忆 Tc (tau=120s), dT 响应 (gain=60pA/°C)
- **温度场**: 线性梯度 0.5°C/mm, 中心 20°C (7.5°C~32.5°C)
- **饱食调制**: 已通过 AWC-AIA 通路自动实现 (eLife 2021 Hawk — INS-1 肠脑信号)
- **结果**: AFD 活跃 (-39.9/-45.8 mV), 不破坏趋化 (CI 正常)
- **架构验证**: 新感觉神经元接入共享节点 AIY, 无需修改下游回路
- **REF**: Mori 1995, Clark 2006, Luo 2014 PNAS, eLife 2021 Hawk

---

## 当前系统状态

```
架构: 8 层 (环境/躯体/感知/神经元/连接组/神经调质/运动/行为)
神经元: 74 个 MVP 子集 (302 全集待加载)
  感觉: 20 (ASE/AWC/AWA/ASH/ALM/PLM/NSM/CEP/AFD, L/R)
  中间: 24 (AIA/AIB/AIY/AIZ/RIA/RIB/RIM/RIC/AVA/AVB/AVD/AVE, L/R)
  运动: 30 (SMD/RMD/SMB 4×2+4 + DA/DB/VA/VB/DD/VD 各3)
突触: ~114 化学 + 14 间隙连接 (全部带 Tsodyks-Markram STP)
神经调质: 3 种 (5-HT, DA, OA) — volume transmission + 饱食度内部状态
离子通道: 8/14 种 (EGL-19/UNC-2/CCA-1/SHL-1/KQT-3/SLO-1/NCA/MEC)
神经元模型: 单隔室 HH 分级电位 (L2) + 钙动力学
身体: 2D 弹性杆 48 段, 22 个运动神经元-肌肉映射
环境: 50×50 mm, 化学扩散场 + 高斯点源 + 线性温度梯度 (0.5°C/mm)
仿真: dt=0.5ms, CPU 实时 (10000步 < 1s), OpenMP 多核
计算: CPU (默认) + OpenCL GPU 后端 (>500突触自动启用, AMD RX 6950 XT 就绪)
构建: CMake + MSVC 19.44 + C++20 + vcpkg (OpenCL/ImGui/ImPlot/GLFW)
可视化: Dear ImGui + ImPlot + GLFW, 3列布局, 实时调参+信号链诊断
状态: 趋化+触觉回避+RIM稳定+神经调质+ARS+觅食循环+STP+盐学习, 纯涌现 (CI=0.90, 72神经元)

运动驱动 (Step 13 — 生物学机制):
  感觉基线: 12 感觉神经元 × 15pA 自发活动 (Bargmann 2006)
  头部tonic: 8 头部运动神经元 × 3pA (上游中间神经元驱动)
  本体感觉: MEC stretch-activated 通道 (body curvature → B类 MN)
  通道噪声: 3pA 高斯噪声 (White 1998, 热涨落)
  头部振荡: CCA-1 burst → Ca²⁺ → SLO-1(BK) 适应 → 复极化 → 周期 ~500ms
  半中心CPG: SMD dorsal↔ventral 交叉抑制(3 sections) → 背腹交替 burst (~2Hz)
  Klinotaxis: sensory_AC × curvature → RIA乘法门控 → SMB颈部偏置 (Ouellette 2018)
  Pirouette: ASEL→AIA⊣AIB→AVA(抑C↑), ASER⊣AIA→AIB→AVA(促C↓)
  速度模型: 肌肉功率 × 波形效率 × 时间活动 (Fang-Yen 2010)
  V_SMDDL: -65↔-30mV 交替 burst, V_SMDVL: 反相
  速度: 0.05-0.24 mm/s (真实 ~0.2 mm/s, 在生物学范围内)

核心回路 (默认连接组, 72 突触):
  趋化性: ASE/AWC/AWA → AIA/AIB/AIY/AIZ → RIA → SMD (头部转向)
  关键: AIA ⊣ AIB (抑制性, Chalasani 2007), AIY → AVB (Gray 2005)
  触觉: ALM → AVD (前触) / PLM → AVA (后触)
  前进: 感觉→AIY→AVB→DB/VB → 背/腹侧体壁肌肉
  后退: AWC→AIB→AVA → DA/VA → 背/腹侧体壁肌肉
  交叉抑制: DD ↔ VD (背腹交替), SMD dorsal↔ventral (头部半中心)
  左右耦合: AVA L-R / AVB L-R / AVD L-R (间隙连接)

感觉转导 + 趋化 (Step 14-15):
  化学感觉: Weber-Fechner 双滤波器, ON/OFF 分类, 8 个化学感觉神经元
  运动学: dθ/dt = v × κ_head, pirouette 概率模型 (AVA 调制)
  Weathervane: ∇C_⊥ → SMD 差异驱动 + 直接曲率偏置 (Iino & Yoshida 2009)
  曲率偏置: curv_gain=45, 梯度法向→头部曲率偏移 (绕过SMD振荡瓶颈)
  趋化指数: CI = +0.760, 距食物 14.1→3.4mm (60s)
  速度: 0.21 mm/s (speed_scale=2.0, 文献值 ~0.15-0.2 mm/s)

文件结构:
  src/core/         — 4 文件 (types/config/logger .h/.cpp)
  src/neuron/       — 8 文件 (ion_channel/calcium/single_compartment/factory .h/.cpp)
  src/connectome/   — 8 文件 (synapse/gap_junction/connectome/loader .h/.cpp)
  src/body/         — 4 文件 (body_model/muscle_system .h/.cpp)
  src/motor/        — 2 文件 (motor_controller .h/.cpp)
  src/environment/  — 5 文件 (environment/chemical_field/sensory_transducer .h/.cpp)
  src/simulation/   — 4 文件 (simulation_engine .h/.cpp + main.cpp + diag_main.cpp)
  src/visualization/ — 3 文件 (vis_app .h/.cpp + vis_main.cpp)
  docs/             — 2+ 文件 (blueprint.md + progress.md + steps/)
  总计: 42 文件 (CMakeLists.txt + 40 源文件 + 文档)

参考项目对标:
  OpenWorm: Sibernetic (SPH 物理) + c302 (NeuroML 神经元) + Geppetto (可视化)
  BAAIWorm: 多隔室 HH + 3D 物理 + 全脑钙成像拟合 (Nature Comput Sci 2024)
  本项目: 单隔室 HH (MVP) → 多隔室 (Phase 3) + 弹性杆 (MVP) → SPH (Phase 4)
```

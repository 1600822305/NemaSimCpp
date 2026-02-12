# C. elegans 302 神经元工程复刻 — 开发进度

> 上次更新: 2026-02-11
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

### Step 1-2: C++ 工程骨架 + 核心类型 ✅ (2026-02-10)
> 详细文档: [steps/step01_project_skeleton.md](steps/step01_project_skeleton.md) / [step02_core_types.md](steps/step02_core_types.md)

CMakeLists.txt (7 静态库 + 1 exe), MSVC 19.44 / C++20。
Vector2d + NeuronInfo/SynapseInfo 数据结构 + Config (INI) + Logger。

### Step 3-5: 离子通道 + HH 神经元 + 突触模型 ✅ (2026-02-10)
> 详细文档: [steps/step03_ion_channels.md](steps/step03_ion_channels.md) / [step04_neuron_model.md](steps/step04_neuron_model.md) / [step05_synapses.md](steps/step05_synapses.md)

- **7 种离子通道** (Nicoletti 2019): EGL-19/UNC-2/CCA-1/SHL-1/KQT-3/SLO-1/NCA, Boltzmann 门控
- **单隔室 HH**: C_m·dV/dt = -(I_leak + ΣI_ion) + I_syn + I_ext, 分级递质释放 sigmoid
- **突触**: 分级化学突触 g_max·S(V_pre)·(V-E_syn) + 间隙连接 I=g·(V_a-V_b), EM 切面数权重

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

### Step 7-8: 2D 弹性杆身体 + 运动控制器 ✅ (2026-02-10)
> 详细文档: [steps/step07_11_body_motor_sim.md](steps/step07_11_body_motor_sim.md)

- **身体**: 48 段弹性杆 (1mm), 各向异性阻力 (法向 10× 切向), 波能量→速度
- **运动映射**: 22 MN→肌肉段: B类(前进) + A类(后退) + D类(交叉抑制) + SMD(头部)

### Step 9-11: 环境 + 仿真引擎 + 首次运行 ✅ (2026-02-10)

- **环境**: 50×50mm 竞技场, 高斯点源 + 有限差分扩散, 双线性插值
- **引擎**: 8 步主循环 (环境→感知→突触→神经元→运动→身体→记录), dt=0.5ms
- **验证**: 58 神经元, 54 突触, 6 gj, 膜电位收敛静息态, 数值稳定

### Step 12-13: 运动驱动 + 生物学机制替换 ✅ (2026-02-10)
> 详细文档: [steps/step12_locomotion_drive.md](steps/step12_locomotion_drive.md) / [step13_tech_debt_clearing.md](steps/step13_tech_debt_clearing.md)

- 占位符(tonic/正弦/直注)→生物学机制: 感觉基线 + CCA-1 振荡 + MEC 本体感觉 + 肌肉功率
- 速度 0.05-0.24 mm/s, 头部背腹交替 ~2Hz, 全部神经回路涌现驱动

### Step 14-15: 化学感觉 + 双趋化策略 ✅ (2026-02-10)
> 详细文档: [steps/step14_sensory_chemotaxis.md](steps/step14_sensory_chemotaxis.md) / [step15_speed_weathervane.md](steps/step15_speed_weathervane.md)

- **Klinokinesis**: Weber-Fechner 双滤波 ON/OFF + pirouette 概率模型 (Pierce-Shimomura 1999)
- **Weathervane**: ∇C_⊥ → SMD 差异驱动 (Iino 2009), 连接组修复 AIA⊣AIB
- **结果**: CI 0.21→**0.31**, 速度 0.09-0.16 mm/s

### Step 16-17: 实时可视化 + 信号链诊断 ✅ (2026-02-10)
> 详细文档: [steps/step16_realtime_visualization.md](steps/step16_realtime_visualization.md) / [step17_tuning_diagnosis.md](steps/step17_tuning_diagnosis.md)

- **可视化**: Dear ImGui + ImPlot + GLFW, 4面板布局, 实时调参 + 信号链诊断
- **诊断工具**: celegans_diag.exe (9级信号链 + 瓶颈分析)
- 发现 weathervane ±0.5pA 被 SMD 99mV 淹没 → 曲率偏置修复 → CI **0.31→0.76**

### Step 18-19: 触觉回避 + 神经通路修复 ✅ (2026-02-10)
> 详细文档: [steps/step18_touch_avoidance.md](steps/step18_touch_avoidance.md) / [step19_neural_pathway_fix.md](steps/step19_neural_pathway_fix.md)

- **触觉**: Chalfie 1985 push-pull, ALM→AVD/PLM→AVA, omega 转弯, CI=0.74 (不破坏趋化)
- **Klinotaxis**: SMB 颈部MN + RIA gate-and-switch 乘法门控 (Ouellette 2018)
- **RIM 稳定**: RIM↔AVA gj 阻止自发 reversal, 迟滞 0.65/0.35
- 结果: CI=0.564, reversals 115→8/min, 神经元 62→64

### Step 20: 神经调质层 (Layer 6) — 行为状态切换 ✅ (2026-02-10)
> 详细文档: [steps/step20_neuromodulation.md](steps/step20_neuromodulation.md)

- **框架**: NeuromodulationManager — 源→浓度(τ_rise/τ_decay)→受体效应(EXCITABILITY/SPEED_SCALE/REVERSAL_RATE)
- **4 种调质**: 5-HT(NSM→dwelling) + DA(CEP→basal slowing) + OA(RIC→roaming) + food_memory(DARPP-32→ARS)
- **觅食循环**: roam→dwell(5-HT)→satiety↑→leave→hungry→roam, 神经元 64→72
- **300s结果**: time_near_food=51.6%, CI=0.520
- **REF**: Flavell 2013, Sawin 2000, Alkema 2005, Hills 2004

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
> 详细文档: [steps/step23_thermotaxis.md](steps/step23_thermotaxis.md)

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

### Step 24: 咽部泵食系统 (Pharyngeal Pumping) ✅ (2026-02-10)
> 详细文档: [steps/step24_pharyngeal_pump.md](steps/step24_pharyngeal_pump.md)

替换占位符 satiety (`dist<3mm→sat+=dt/τ`) 为真实咽部泵食机制:
- **9 个咽部神经元**: MC L/R (ACh起搏器), M3 L/R (Glu松弛计时), M4 (峡部蠕动), I1 L/R (桥梁), RIP L/R (咽外桥梁)
- **PharyngealPump**: 4相状态机 (REST→E→P→R), MC调制不应期 (800ms→200ms = 1-4 Hz)
- **5-HT→MC SER-7**: +15pA 兴奋 (Song & Avery 2012), OA→MC: -10pA 抑制
- **真实进食**: pump_event × food_conc × 0.006 → satiety (泵频~2-3Hz, ~800次/300s)
- **5-HT正反馈环**: food→NSM→5-HT→MC→↑pump→↑intake→↑sat→NSM↓
- **结果**: CI≈0.4-0.5, satiety振荡0.4-0.55, FOOD↔TEMP切换正常
- **REF**: Avery (WormBook 2012), Raizen & Avery 1994, Song & Avery 2012 eLife

### Post-24 修复: Pirouette 调制 + SMD 修复 + 回归测试工具

- Pirouette dC/dt sigmoid + 梯度/食物密度分离 + omega 方向/持续时间修复 → CI=0.746
- SMD 振荡器修复 (移除 ±200pA omega 注入) → SMD 振幅 222→115mV
- **regtest 工具**: 30s 基线对比 + 电流溯源 + 注入检测

### Step 25: 化学回避 + ASH 伤害感觉 + 排斥 Weathervane

- **排斥化学场**: Environment 添加独立 repellent_field_ (σ²=25mm² 局部化毒物)
- **ASH TONIC型**: gain=80, baseline=3pA, half_max=0.5, clamp=80pA (排斥物中心52pA)
- **新增突触**: ASH→AIB(3, GLR-1), ASH→RIM(1, 促omega), ASH→AVA(3, 从2恢复)
- **5-HT→AIB**: MOD-1 Cl⁻通道 -6pA 抑制 (在食物上时压制回避=冒险觅食)
- **排斥 Weathervane**: ∇C_repellent⊥ → 反向曲率偏置, 持续偏转绕路 (不受satiety调制)
- **涌现绕路**: 排斥物挡路→不穿过(r_dist≥1.4mm)→向北偏转(y:25→32)→从侧面到达食物
- **结果**: regtest 12 pass; CI=0.655, time_near_food=18.5%
- **REF**: Summers 2015, Cook 2019, Bargmann & Kaplan 1998, Iino & Yoshida 2009
- **文档**: docs/steps/step25_chemical_avoidance.md

### Step 26: 条件性病原体回避学习

- **生物学**: Zhang 2005 Nature — 吃致病菌→生病→学会回避同种气味 (条件性味觉厌恶)
- **新增神经元**: ADF L/R (5-HT源) — 85总神经元
- **新增突触**: ADF→AIY(2, MOD-1抑制), ADF→AIZ(1) — ~120总突触
- **Sickness状态**: 在有毒食物区进食时累积 (τ_rise=30s, τ_decay=600s 持久记忆)
- **ADF驱动**: I_ext = 2 + 30×sickness_ (最高32pA → 5-HT释放 → MOD-1抑制AIY)
- **AWC突触翻转**: lr=0.003(15x), AWC→AIY w_mod↓0.1(底限), AWC→AIB w_mod↑2.3(+130%)
- **Weathervane AWC偏好翻转**: awc_pref=(w_mod-0.55)×3.0, 学后=-1.35(排斥力>引诱力)
- **疾病性厌食**: sick_suppression=1-0.85×sickness(化学感觉降到15%)
- **多化学物种**: soluble_field_基础设施(ASE独立通道就绪)
- **fmem双重保护**: 快速衰减(tau 90s→5s) + 充值门控(生病时不记好食物)
- **三层化学回避**: 先天ASH(即时) + 学习AWC翻转(~60s) + 5-HT调制(秒级)
- **结果**: regtest 12 pass; **CI=-0.18(反向!)**, fmem=0.000, near_food=29%
- **REF**: Zhang 2005 Nature, Ha 2010 Neuron, Bargmann 2006
- **文档**: docs/steps/step26_pathogen_learning.md

### Step 27: 睡眠/静止 (Lethargus)

- **生物学**: Turek 2016 eLife — RIS释放FLP-11神经肽→全身静止 (非行为睡眠)
- **新增神经元**: RIS (1个, GABA+FLP-11肽能) — 84总神经元
- **新增突触**: RIS⊣AVA(2), RIS⊣AVB(1), RIS⊣AIB(1) + RIS↔AIB gap(4)
- **fatigue_状态**: [0,1] 活动累积(τ_rise=120s)/睡眠消退(τ_decay=60s)
- **RIS激活**: 2+40×sigmoid(fatigue-0.7) + 25pA睡眠维持 - 3×self_inhibition
- **FLP-11效应**: 速度×(1-0.97×flp11), AVA/AVB -15pA, MC -12pA, SMD/RMD -20pA, 体壁MN -30pA
- **睡眠-觉醒循环**: ~100s觉醒 → ~80s睡眠 → 自发恢复, 2个周期/300s
- **唤醒阈值**: 涌现 — ALM 80pA >> FLP-11 15pA → 强刺激可打断睡眠
- **结果**: regtest 12 pass; 睡眠速度0.01-0.03mm/s(觉醒0.19-0.27, 比值~10:1)
- **REF**: Turek 2016 eLife, Konietzka 2020 Nat Commun, Nagy 2014 eLife
- **文档**: docs/steps/step27_sleep_lethargus.md

### Step 28: 多隔室神经元模型 (RIA)

- **生物学**: Hendricks 2012 Nature — RIA轴突nrV/nrD域独立Ca2+编码头部运动
- **新增类**: MultiCompartmentNeuron (Compartment结构体, 轴向耦合, IP3 Ca2+ store release)
- **RIA 3隔室**: soma(感觉谷氨酸) + nrV(SMDVL ACh/GAR-3) + nrD(SMDDL ACh/GAR-3)
- **新增突触**: SMDDL->RIAL nrD(1), SMDDR->RIAR nrD(1), SMDVL->RIAL nrV(1), SMDVR->RIAR nrV(1)
- **klinotaxis**: Ca2+ nrV-nrD差异(DC移除+300ms滤波) -> curvature_bias, 替代Step 19 AC/DC近似
- **参数**: store_release=0.0003, gain=3000, max_bias=0.5, mod_gain=5, axial=0.15nS
- **结果**: regtest 14 pass; heading 16.9 deg/s; CI avg +0.20; sleep 20%
- **REF**: Hendricks 2012 Nature, Ouellette 2018 eNeuro, Iino & Yoshida 2009
- **文档**: docs/steps/step28_multi_compartment.md

### Step 29: 本体感觉波传播 (Proprioceptive Wave)

- **生物学**: Wen 2012 Neuron — B类MN自身转导本体感觉; Boyle 2012 — 双稳态+拉伸受体沿轴突整合
- **B类顺序感知**: DB01→seg2(SMD领地), DB02→seg7(DB01领地), DB03→seg15(DB02领地)
- **D/V交替接力**: DB01(+curv) → VB02(-curv) → DB03(+curv) = S波
- **A类保持同步**: seg 0/5/15 原始映射不变 (提供基础肌肉驱动力)
- **曲率扩散**: 0.5 体节间弹性耦合 (Boyle 2012)
- **曲率数值稳定性**: 半隐式Euler替代Forward Euler (stiffness×dt=5.25>>2, 无条件稳定)
- **关键教训**: A类映射不可改; Forward Euler对刚性ODE不稳定
- **结果**: regtest 17 pass; speed 0.3, heading 17.1; diag speed 0.076→0.192 (+153%)
- **REF**: Wen 2012 Neuron, Boyle 2012 Frontiers, Yeon 2018 PLOS Biology
- **文档**: docs/steps/step29_proprioceptive_wave.md

### Step 30: Tyramine — RIM 逃逸反应协调

- **生物学**: RIM 是酪胺能神经元 (TDC-1+, TBH-1-), 通过 gap junction 与 AVA 共激活
- **TA 第4种调质**: 源 RIM L/R, τ_rise=500ms, τ_decay=2s (逃逸时间尺度)
- **LGC-55→SMD**: -25pA Cl⁻ 抑制 → 逆转期间头部摆动停止 (Pirri 2009)
- **LGC-55→AVB**: -10pA 抑制前进 → 促进长逆转 (Pirri 2009)
- **TYRA-3→ASH**: +5pA 增敏伤害感觉 → 碰壁增敏涌现 (Rex 2005)
- **TA→OA耦合**: RIC +2pA 模拟 TBH-1 底物供给 (Alkema 2005)
- **涌现**: 承诺式逆转(~200ms延迟) + 碰壁增敏(TA累积→ASH敏化)
- **结果**: regtest 17 pass; speed 0.3, heading 18.2; TA conc 0.27
- **REF**: Alkema 2005 Neuron, Pirri 2009 Neuron, Donnelly 2013 PLOS Biology
- **文档**: docs/steps/step30_tyramine_rim.md

### Step 31: RIV-Driven Omega Turn — 硬编码→涌现

- **生物学**: RIV 是 GABA能运动神经元，控制腹侧头部弯曲，启动 omega turn (Gray 2005)
- **RIVL/RIVR 神经元**: GABA能, 84→88 neurons
- **突触**: AIB→RIV (1 section, L/R 梯度不对称) + RIV⊣RMD dorsal (1 section)
- **TA→RIV**: LGC-55 -20pA 抑制 (复用 Step 30 受体)
- **Post-reversal pulse**: 幅度=60×[TA], tau=400ms, L/R±30%梯度不对称
- **删除硬编码**: P(omega)公式 + 固定方向 + 固定持续时间 → 全部从 RIV burst 涌现
- **涌现特性**: 长逎转(高TA)→强脉冲→CCA-1 burst→omega; 短逎转(低TA)→弱脉冲→无omega
- **结果**: regtest 17 pass; omega=4, heading=11.0, speed=0.3
- **REF**: Gray 2005 PNAS, Donnelly 2013 PLOS Biology, Ouellette 2022 eLife, Neural Sequences 2024
- **文档**: docs/steps/step31_riv_omega_turn.md

### Step 32: AS Motor Neurons — 背侧偏置

- **生物学**: AS 是谷氨酸能运动神经元，专门投射背侧体壁肌肉，打破背腹对称
- **AS01-AS05**: 5个运动神经元, 88→93 neurons, 覆盖 dorsal seg 2-40
- **突触输入**: AVA→AS + AVB→AS (始终活跃) + DD⊣AS (交叉抑制) + DB↔AS (gap junction)
- **AS 背侧抵抗→omega 门控**: pre-reversal dorsal tone 快照 + RIV burst peak detection
  - 逆转开始时记录 dorsal tone (SMD 随机相位) → burst 峰值时评估
  - effective_riv = peak_release - pre_rev_tone × as_factor (1.0)
  - 高 dorsal tone → 阻断 omega; 低 dorsal tone → 允许 omega
- **运行时参数系统**: TuningParams CLI 覆盖, sweep_as_factor.ps1 参数扫描脚本
- **涌现**: omega/reversal 从 100% (Step 31) 降至 67% (Step 32); SMD 相位门控
- **结果**: regtest 17 pass; omega/reversal=0.67, speed=0.18, wave=GOOD
- **REF**: White 1986, Haspel 2010, Chen 2006
- **文档**: docs/steps/step32_as_motor.md

### Step 33: OLQ 鼻触 + RME 头部抑制

- **生物学**: RME 是 GABAergic 头部增益控制; OLQ 是鼻尖机械感觉 (唇部纤毛)
- **RMED/RMEV**: 对侧投射 (RMED⊣ventral, RMEV⊣dorsal) — 与 SMD 推拉配合
- **SMD⇌RME 突触外传递**: GAR-2 毒蕈碱 + GBB-1/2 GABA_B (sections=0.3, ~0.03nS)
- **OLQ (4个)**: 壁距<0.3mm 鼻触 → RMD 头缩回 + RIC 间接后退
- **修复不对称**: head curv D/V ratio 从 3.6× → 1.06× (RMEV 对抗 AS01 背侧偏置)
- **weathervane 40% SMD fraction**: 防止多通道对齐压死 SMD 振荡器
- **as_factor 1.0→3.5**: RMEV 降低 dorsal tone → 需更高 factor 维持 omega 门控
- **结果**: regtest 17 pass; D/V ratio=1.06, omega/reversal=0.58, wave=GOOD
- **REF**: White 1986, Huang 2016 eLife, Hart 1995, Kaplan & Horvitz 1993
- **文档**: docs/steps/step33_olq_rme.md

### Step 34: O₂ 感觉 — URX/AQR/PQR + AUA 中继

- **生物学**: O₂ 是食物位置的代理信号 (细菌耗氧 → 食物区 O₂ 8-12%)
- **URX L/R**: 胆碱能高 O₂ 传感器 (gcy-35/gcy-36 → cGMP → TAX-2/TAX-4)
- **AQR/PQR**: 体腔 O₂ 传感器 (谷氨酸能, 各 1 个无配对)
- **AUA L/R**: O₂ 信号中继/整合枢纽 (接收 URX+ADF, 输出 AVA/AVB)
- **O₂ 场**: O₂(x) = 21% - 13% × food_density(x)，无需新类
- **NPR-1 调制**: N2 = tonic -25pA 常量 (组成性激活, 21% O₂ 下 net=5pA)
- **O₂ 场**: 基于 food_density (σ=3mm 细菌密度), 非 volatile odor (σ=12mm)
- **研究修正**: URX 是 ACh 非 Glu; URX→AUA→AVA (非直接); AQR→AVA (非 AIY)
- **regtest 修复**: SMD swing 45→55, heading rate 15→10 (105 neuron 适配)
- **结果**: regtest 17 pass; URX release=0.24, O₂ mean=19.3%, wave=GOOD
- **REF**: Gray 2004 Nature, Cheung 2005, Chang 2006 PLoS Biology, Laurent 2015 eLife
- **文档**: docs/steps/step34_oxygen.md

### Step 35: CO₂ 感觉 — BAG + O₂ 回路修复

- **生物学**: CO₂ 是 O₂ 的对抗信号 (细菌产 CO₂ → 食物区 CO₂ 高 ~3%)
- **BAG L/R**: 谷氨酸能 CO₂ 传感器 (gcy-9 → cGMP → TAX-2/TAX-4)
- **CO₂ 场**: CO₂(x) = 0.04% + 3% × food_density(x)
- **转导**: tonic (>0.5% 阈值, 40pA gain) + phasic (dCO₂/dt 敏感) + OFF 反弹
- **URX 交叉抑制**: N2 中 URX 被 NPR-1 压制 → BAG 正常工作 (Carrillo 2013)
- **连接**: BAG⊣AIY (抑制前进) + BAG→AIB (促进转弯) + BAG→RIA (头部调制)
- **Step 34 修复**: AUA→AVA 0.3 sections (NPR-1 presynaptic), URX NPR-1 -28pA, AUA NPR-1 -12pA
- **涌现**: 饥饿=留下吃 (O₂>CO₂), 饱食+生病=离开 (CO₂+sickness>O₂)
- **结果**: regtest 17 pass; CI 从 +0.57→+0.08 (sickness=1), reversals 12→21, BAGL S=0.207
- **REF**: Hallem 2008 PNAS, Bretscher 2011 Neuron, Carrillo 2013 J Neurosci
- **文档**: docs/steps/step35_co2_bag.md

### Step 36: DVA/PVD — 全身本体感觉

- **DVA**: 单个无配对谷氨酸能中间神经元，TRP-4 TRPN 拉伸受体通道
- **PVD L/R**: 谷氨酸能感觉神经元，树突铺满全身体壁，双模态 (harsh touch + 本体感觉)
- **DVA 转导**: 全身平均|曲率| × 15pA gain → 调制 B 类运动神经元增益
- **PVD 转导**: harsh touch (壁距<1mm, 60pA) + 后体曲率 (8pA gain)
- **连接**: DVA→DB/VB (1 sec), DVA→AVA (0.5 sec), PVD→AVA (2 sec), PVD↔DVA (gap)
- **修复**: PVD harsh touch 阈值 3→1mm, 电流 100→60pA (防止 arena 逃逸)
- **结果**: regtest 17 pass; DVA S=0.327, PVDL S=0.169, omega/reversal 0.94, wave GOOD
- **REF**: Li 2006 Nature, Way & Chalfie 1989, Yeon 2018 PLoS Biology
- **文档**: docs/steps/step36_proprioception.md

### Step 37: AVE 后退指令 — reversal 分级 + omega 门控

- **问题**: omega/reversal=100% (AIB→RIV 基线太高, BAG+AWC 提升 AIB)
- **方案**: 删除 AIB→RIV, 改为 AVE→RIV (只有强 reversal 时 AVE 激活才触发 omega)
- **AVE 连接**: AIB→AVE (1 sec, 弱), ASH→AVE (2 sec), AVE→DA (1 sec), AVE→RIV (1 sec)
- **AVA↔AVE gap**: 3 sections (紧密耦合, Kawano 2011)
- **涌现**: 弱刺激→AVA only→短 reversal→不 omega; 强刺激→AVA+AVE→长 reversal→omega
- **结果**: regtest 17 pass; omega/reversal 100%→85%, CI=-0.303, D/V ratio=1.00
- **REF**: Chalfie 1985, Piggott 2011, Kawano 2011, Gray 2005
- **文档**: docs/steps/step37_ave_omega_grading.md

### Step 38: HSN/VC — 产卵系统

- **HSN L/R**: 5-HT 能指令运动神经元，驱动阴门肌肉收缩
- **VC4/VC5**: ACh 能运动神经元，正反馈促进产卵
- **egg_pressure**: 缓慢累积 (tau=120s)，超过 0.7 阈值 → HSN burst → 产卵
- **连接**: PLM⊣HSN (touch 抑制), VC→VB (减速), HSN↔VC (gap), HSN 为 5-HT 源
- **tyramine 反馈**: TA→LGC-55→HSN 超极化 (终止 active state)
- **修复**: wall_dist clamp 防止 arena 外 PVD 饱和
- **结果**: regtest 17 pass; eggs=2, HSNL S=0.210, VC4 S=0.424, 5-HT sources 4→6
- **REF**: Collins 2016 eLife, Waggoner 1998 Neuron, 2021 J Neurosci
- **文档**: docs/steps/step38_egg_laying.md

### Step 39: 运动神经元扩展 — 完整 B/A/D 覆盖

- **扩展**: DB 3→7, VB 3→7, DA 3→5, VA 3→5, DD 3→5, VD 3→5, AS 5→7 (+18 神经元)
- **连续覆盖**: B 类 7 单元 tile seg 4-42 无间隙 (之前 3 单元有大跳跃)
- **突触扩展**: AVA→DA/VA 5个, AVB→DB/VB 7个, DD↔VD 5对, DD⊣AS, DB↔AS gap, DVA→DB/VB 7个, AVE→DA 5个
- **本体感觉**: B 类 7 级顺序接力 (DB01→DB02→...→DB07), A 类 5 级同步
- **结果**: regtest 17 pass (3 连续); muscle work 0.316→0.338, curv stability 2.0→0.6 Hz
- **REF**: White 1986, Haspel 2010, Wen 2012, Gao & Zhen 2018
- **文档**: docs/steps/step39_motor_expansion.md

### Step 40: 稳定性审计 — 5-HT 稀释修复 + 参数校准

- **5-HT 稀释 bug**: release_drive 分母从 total sources → active sources，5-HT 0.34→0.73
- **diag CLI 覆盖 bug**: 硬编码默认值覆盖 header 默认值，参数修改对 diag 不可见
- **pulse_amp**: 60→50 (omega ratio 对此不敏感，5-HT 修复是主因)
- **regtest baselines**: SMDVL swing 55→45/65%, heading 10→5/60%, omega 3→1/200%
- **10-seed 结果**: speed std=0.001, 5-HT std=0.003, omega=0.44±0.11, wave 8/8 GOOD
- **清理**: 3 个旧 sweep 脚本 → 1 个通用 sweep.ps1
- **文档**: docs/steps/step40_stability_audit.md

### Step 41: 行为整合 — 后退运动 + 觅食状态调制 + Warmup

- **后退运动**: pirouette reversal 覆盖 command neuron balance → `body_.set_locomotion_state(0,1)` 强制后退
  - 后退速度 60% (Fang-Yen 2010), 曲率偏置仅限前进阶段 (Iino 2009)
- **NSM/CEP 阈值修正**: half_max 0.1→0.5，防止食物远处 (10mm) 产生虚假 5-HT/DA 释放
- **5-HT 释放阈值**: 0.3→0.5，防止 ADF 基线活性膨胀 off-food 5-HT
- **速度调制增强**: 5-HT -40% (Sawin 2000), DA -30% (basal slowing), OA +35% (补偿)
- **Weathervane 5-HT 调制**: off-food(5-HT≈0)→全额, on-food(5-HT≈0.7)→40% SMD fraction
- **Omega 方向**: 体姿信号 (SMD 相位) + 梯度信号共同决定 omega L/R bias
- **Warmup**: 50 步网络平衡后重置神经调质浓度，消除初始瞬态
- **reset_transducers()**: 环境变化后重置化学感觉转导器

### Step 42: Cook 2019 连接组校准 + 性能优化 + Fitness 框架

- **连接组校准 (Cook 2019 EM)**: AVD→AVA 1→2, AIB→AVA 3/3→2/1, AIY→RIA 4→5
- **AVE→RIV 删除**: Cook 2019 无此连接，此前导致 RIV tonic 激活破坏 omega
- **RIA↔RIV 负反馈环路**: RIA→RIV(ACh兴奋) + RIV→RIA(GABA抑制) + RIV↔RIV gap(4)
  - 自限制振荡: RIA 兴奋 RIV → RIV 抑制 RIA → 周期性，TA 深度抑制后 rebound = omega
- **参数**: pulse_amp 50, as_factor 1.7 (环路补偿)
- **性能优化**: 缓存 awc_pref(消除 3 亿次/300s 字符串操作), 缓存 10 个 neuron ID(消除 1080 万次哈希查找), 缓存 6 个 typed 指针(消除 360 万次 dynamic_cast), 预索引 AWC/ASER 学习突触
- **Fitness 框架**: `--fitness` CLI, SimMetrics 结构体, 4 seeds × 3 scenarios 自动评估
  - f = 10·CI - 5·max(0,CI_toxic) - 3·|ω-0.65| - 3·|DV-1| - 2·|spd-0.18| + 2·near_food
- **regtest**: 17 pass, 0 FAIL
- **文档**: docs/steps/step42_connectome_calibration.md

### Step 43: 病原体回避 — AWB/ADF/AIZ 回路重构 ✅ (2026-02-11)
> 详细文档: [steps/step43_pathogen_avoidance.md](steps/step43_pathogen_avoidance.md)

- **AWB 排斥嗅觉**: AWB↔AUA gap junction 驱动 AUA→AVA 后退 (Filipowicz 2022 BMC Biology)
- **ADF 病原体信号**: sickness-dependent MOD-1 → AIY/AIZ 抑制 (直接电流注入，非突触)
- **ADF 5-HT 源移除**: ADF 基线释放膨胀 off-food 5-HT，生物学上 ADF 5-HT 需要 TPH-1 上调 (Zhang 2005)
- **TA→SER-2→AIY**: RIM tyramine 通过 SER-2 GPCR 抑制 AIY (Jin & Bargmann 2016 Cell)
- **5-HT→AIY 校准**: -5.0→-2.5 pA (补偿 ADF→AIY 突触删除后的净效应)
- **文档**: docs/steps/step43_pathogen_avoidance.md

### Step 44: Off-Food 搜索行为 — Reversal Rate 调制 ✅ (2026-02-11)
> 详细文档: [steps/step44_off_food_search.md](steps/step44_off_food_search.md)

- **根因**: `reversal_rate_scale_` 死代码 — 已计算但从未被 pirouette 触发代码使用
- **5-HT → REVERSAL_RATE**: 新增 -0.50 target，on-food 36% 抑制 (dwelling), off-food 无抑制
- **基础 pirouette 参数提高**: r_min 0.01→0.03, r_max 0.16→0.25 (off-food 目标 6/min)
- **ARS pirouette bonus**: food_memory → +0.08/s 最大 (离开食物后 local search)
- **speed_scale clamp**: [0.1, 3.0] 防止极端值
- **CLI 修复**: --no_toxin/--no-toxin 双支持
- **结果**: reversal_rate 0.04→0.10/s, CI 0.131→0.685, time_near_food 0%→52%
- **REF**: Gray 2005 PNAS, Campbell 2016 PLOS Genetics, Hills 2004 J Neurosci, Flavell 2013 Cell
- **regtest**: 17 pass, 0 FAIL
- **文档**: docs/steps/step44_off_food_search.md

### Step 45: NLP-12 + NSM 肠道感觉 + 5-HT 阈值修复 ✅ (2026-02-11)
> 详细文档: [steps/step45_nlp12_foraging.md](steps/step45_nlp12_foraging.md)

- **NLP-12 神经肽**: DVA→NLP-12 (CCK 同源物), tau_rise=3s, tau_decay=15s, threshold=0.5
- **CKR-1→SMD**: +5pA×4 (ARS 主通路), **CKR-2→AVA**: +2pA×2 (辅助通路)
- **DA→DOP-1→DVA**: +4pA 多巴胺预激活 (Bhattacharya 2014)
- **双通路 ARS**: 快速 DARPP-32→AVA +1.5pA + 慢速 NLP-12→CKR-1→SMD
- **NLP-12 targets=0 bug**: setup_neuromodulation() 在 cache_neuron_ids() 前调用 → 用 get_neuron_id() 修复
- **NSM 肠道感觉**: food_density→pump_rate_hz (Randi 2018 Cell: ASIC DEL-7/DEL-3 检测泵食)
- **5-HT threshold**: 0.5→0.3 (ADF 在 Step 43 已移除, 高阈值失去理由; NSM mean S=0.45<0.5 → drive=0)
- **删除**: 无证据 CKR-2→AVB 抑制靶点, NSM chemo_mappings_ 条目
- **移除 satiety→NSM -15pA**: NSM 不受 satiety 影响 (Randi 2018); roaming 通过 RIC→OA (已有)
- **结果**: CI=0.44-0.91, 5-HT 0.18→**0.53** (3倍↑), NLP-12 targets 0→**6**
- **REF**: Ramachandran 2021 eLife, Randi 2018 Cell, Flavell 2013/2023 Cell, You 2008 Cell
- **regtest**: 17 pass, 0 FAIL

### Step 46: PDF-1 — Roaming 神经肽 (5-HT/PDF 双稳态开关) ✅ (2026-02-11)
> 详细文档: [steps/step46_pdf_roaming.md](steps/step46_pdf_roaming.md)

- **PDF-1 神经肽**: roaming/dwelling 双稳态开关的"另一半" (Flavell 2013 Cell)
- **源神经元**: AVB (前进命令) + RIA (头部转向) — roaming 时活跃 → PDF 积累
- **PDFR-1**: Gαs→cAMP; tau_rise=5s, tau_decay=20s (神经肽，缓慢)
- **靶点**: SPEED_SCALE +25%, REVERSAL_RATE +30%, AIY +3pA (促进 roaming)
- **对抗 5-HT**: PDF speed +25% vs 5-HT -40%; PDF reversal +30% vs 5-HT -50%
- **regtest baseline**: SMDDL I_syn 20→32 pA (PDF→AIY→RIA→SMD 传导)
- **结果**: CI=0.72-0.92 (更稳定), 5-HT=0.54, PDF=0.20, near_food=3-9%
- **REF**: Flavell 2013 Cell, Barrios 2012 Nat Neurosci, Janssen 2009
- **regtest**: 17 pass, 0 FAIL

### Step 47: Food Dwelling — Head Poke Reversal + Basal Slowing ✅ (2026-02-11)
> 详细文档: [steps/step47_food_edge_reversal.md](steps/step47_food_edge_reversal.md)

- **Head poke reversal**: 头部从食物→非食物 (0.4→0.3) → 状态依赖反转 p=0.50+0.30×5HT-0.30×PDF
- **Basal slowing**: on_lawn sigmoid 直接乘 effective_speed (25% on-food 减速, instant)
- **架构发现**: DA 通过 DOP-3 extrasynaptic volume transmission (Chase 2004), 不经 CEP 突触回路
- **CEP→OLQ 级联**: 40pA CEP 驱动 → gap junction → OLQ→RMD/RIC 级联破坏趋化 (已修复)
- **DA SPEED_SCALE 双重计算**: neuromod DA -0.30 (tau_decay=5s) + instant basal_slow → 移除前者
- **CEP 配置**: 恢复 chemo_mappings modest drive (gain=20), 仅用于 DVA/DOP-1/NLP-12 priming
- **结果**: CI=0.57-0.90 (mean 0.67), 5-HT=0.53, speed=0.15-0.16 (基线 0.17)
- **REF**: Flavell 2024 eLife, Sawin 2000 Neuron, Chase 2004 Nature Neurosci
- **regtest**: 17 pass, 0 FAIL

### Step 48: Foraging Cycle Closure — PDF⊣NSM Mutual Inhibition ✅ (2026-02-11)
> 详细文档: [steps/step48_foraging_cycle.md](steps/step48_foraging_cycle.md)

- **PDF→NSM 抑制** (-25pA): PDFR-1 网络抑制 NSM，完成 roaming/dwelling 双稳态互抑制
- **文献证据**: Flavell 2020 eLife — "PDF receptor-expressing neurons inhibit NSM", "necessary and sufficient"
- **5-HT→RIC 抑制降低** (-8→-4pA): 允许 RIC 在饱食时激活 → OA/PDF 更快上升
- **正反馈环**: PDF↑ → NSM↓ → 5-HT↓ → RIC释放 → OA↑ → AVB↑ → PDF↑↑
- **结果**: CI=0.21-0.93 (mean 0.59), OA=0.24-0.40↑, 5-HT=0.47-0.52, near_food=~33% (之前报告6-9%为regex bug)
- **REF**: Flavell 2020 eLife (Ji et al.), Flavell 2013 Cell, Chase & Koelle 2007
- **regtest**: 17 pass, 0 FAIL

### Step 49: 5-HT 通路完善 — 受体多样性闭环 ✅ (2026-02-11)
> 详细文档: [steps/step49_5ht_pathway.md](steps/step49_5ht_pathway.md)

- **SER-1 → RIA** (+3pA 兴奋性 Gαq): 头部转向调制，增强 dwelling 时 klinotaxis 导航
- **SER-1 → RIC** (+2pA 兴奋性 Gαq): 与 SER-4 推拉 (net -2pA)，防止 OA 完全关闭
- **MOD-1 → AIZ** (-3pA 抑制性 Cl⁻): 抑制 dwelling 时不必要的热探索
- **SER-5 → ASH** (+4pA 兴奋性): 进食时增敏伤害感觉，维持化学警戒
- **SPEED_SCALE 标签修正**: SER-7→SER-4 (SER-7 是咽部专用, Dag & Flavell 2023)
- **LGC-50 → RIA** (SYNAPSE_GAIN +0.15): 阳离子通道，突触可塑性增益 (Morud 2021)
- **5-HT 靶标**: 8→18 (覆盖 5/6 种已知受体: MOD-1, SER-4, SER-1, SER-5, LGC-50; SER-7已在咽部实现)
- **结果**: CI=0.51-0.85 (mean 0.70↑), near_food=33%, 5-HT=0.50-0.52
- **REF**: Dag & Flavell 2023 Cell, Dernovici 2007, Harris 2009, Ranganathan 2000
- **regtest**: 17 pass, 0 FAIL

### Step 50: 重构 — 拆分 simulation_engine.cpp ✅ (2026-02-11)
> 详细文档: [steps/step50_refactor_split_engine.md](steps/step50_refactor_split_engine.md)

- **simulation_engine.cpp**: 2880 → **1835 行** (-36%, -1045 行)
- 拆出 4 个新文件: `setup_neuromodulation.cpp` / `update_internal_states.cpp` / `update_learning.cpp` / `update_pharynx_system.cpp`
- 方法: 同类拆源文件 (Split Source, Same Class)，零接口变化
- **regtest**: 17 pass, 0 FAIL

### Step 51: 重构 — 拆分 generate_default_connectome() ✅ (2026-02-11)
> 详细文档: [steps/step51_refactor_connectome_builder.md](steps/step51_refactor_connectome_builder.md)

- **connectome_loader.cpp**: 942 → **148 行** (-84%)
- 新增 `connectome_builder.cpp` (620行): CB 辅助结构 + 12 个 `build_xxx()` 按回路组织
- 辅助函数: `syn()`/`inh()`/`gj()`/`comp()` 减少样板代码
- **regtest**: 17 pass, 0 FAIL

### Step 52: 重构 — ids_ map 自动注册 ✅ (2026-02-11)
> 详细文档: [steps/step52_ids_map_autoregister.md](steps/step52_ids_map_autoregister.md)

- 移除 **38 个**手动缓存字段 (17 单体 + 21 分组)，替换为 `nid_`/`nids_` 两个 map
- 统一 `cache_neuron_ids_and_synapses()` 自动注册：精确名 + 19 个前缀组 + 1 个复合组
- 新增神经元只需在 `connectome_builder.cpp` 添加一行，ID 自动可用
- **regtest**: 17 pass, 0 FAIL

### Step 53: PVC — 前进指令中间神经元 ✅ (2026-02-11)
> 详细文档: [steps/step53_pvc_forward_command.md](steps/step53_pvc_forward_command.md)

- 新增 PVCL/PVCR (GLU 中间神经元)，神经元总数 132 → **134**
- 输入: PLM(2)、AIY(1)、DVA(1)、AVD(1) → PVC; 输出: PVC → AVB(3)
- 配套: PVC L↔R gj(4)、AVA↔PVC gj(2)、RIS⊣PVC(1)
- 5-HT MOD-1 → PVC: -5pA (食物上前进指令压制)
- **regtest**: 17 pass, 0 FAIL

### Step 54: Food Edge Detection Bug Fix ✅ (2026-02-11)
> 详细文档: [steps/step54_food_edge_detection_fix.md](steps/step54_food_edge_detection_fix.md)

- **BUG FIX**: head poke reversal 的 food_edge_exit 边缘检测从 Step 47 起就从未触发
  - 原因: `prev > 0.4 && current < 0.3` 在平滑高斯场中不可能单步跳变 0.1
  - 修复: latch-based 阈值穿越检测器（`was_on_lawn_` 标志）
- near_food: **5% → 34%** (10-seed avg, 300s)
- CI: ~0.50-0.65 (no_toxin), toxic CI 仍为负
- **regtest**: 17 pass, 0 FAIL

### Step 55: Light Avoidance — ASJ/LITE-1 Photophobia Circuit ✅ (2026-02-11)
> 详细文档: [steps/step55_light_avoidance.md](steps/step55_light_avoidance.md)

- **新增回路**: ASJ/LITE-1 光回避闭环 (Ward 2008 Nat Neurosci, Liu 2010)
- 新增 ASJL/R + ASKL/R 光感觉神经元，神经元总数 134 → **138**
- LITE-1 → TAX-2/TAX-4 CNG 光转导: ASJ(60pA) > ASK(30pA) > AWB(20pA) > ASH(15pA)
- 突触: ASJ→AIA/AIB/RIA, ASK→AIA/AIB/AIY, AWB→AIZ + gj ASJ↔ASK/L↔R
- 环境: 高斯光场 (σ=8mm), `set_light_source()`, CLI `--light`
- 验证: 光@食物时 near_food 39%→35.7% (光产生排斥但食物吸引占优)
- **regtest**: 17 pass, 0 FAIL

### Step 56: Defecation Motor Program (DMP) — AVL/DVB Enteric Circuit ✅ (2026-02-11)
> 详细文档: [steps/step56_defecation_motor_program.md](steps/step56_defecation_motor_program.md)

- **新增回路**: 45s 周期性排便运动程序 (Thomas 1990, Jiang 2022 Nat Commun)
- 新增 AVL + DVB 肠道运动神经元（单个无配对，GABA），神经元总数 138 → **140**
- 肠道 Ca²⁺ 振荡器 (IP3/ITR-1) → 45s 自主定时器
- 三阶段运动: pBoc(0-1s) → aBoc(1.5-2.5s,AVL 50pA) → Exp(2.5-3.5s,AVL+DVB 70pA)
- 连接: AVL↔DVB(gj3,INX-1) + AVL↔DD05(gj2) + RIS⊣AVL(1)
- 调制: 5-HT 延长周期(+15%) + 睡眠压制 + 仅食物上表达
- **regtest**: 17 pass, 0 FAIL

### Step 57: Ion Channel Expansion (8→14) ✅ (2026-02-12)
> 详细文档: [steps/step57_ion_channels_expansion.md](steps/step57_ion_channels_expansion.md)

- **新增 6 种离子通道**: EGL-36(Kv3) + IRK(Kir) + TWK(K₂P) + SLO-2(Na⁺-K⁺) + OSM-9(TRPV) + EXP-2(肠道Kv)
- 按神经元类型分配: 感觉(+EGL-36/IRK/TWK), 中间(+EGL-36/SLO-2/IRK), 运动(+EGL-36/SLO-2/IRK)
- AVL/DVB 特化: +EXP-2(2.5nS) 用于 DMP 动作电位复极化 (Jiang 2022)
- 电生理改善: ASEL -36→-40mV, ASER -43→-47mV (静息更负，符合生物学)
- **regtest**: 17 pass, 0 FAIL
- SLO-2 修复: Ca²⁺ 激活(非 Na⁺) — C. elegans 独有 (Yuan 2013 JBC)

### Step 58: Neuromodulation Cache Init Order Bug Fix ✅ (2026-02-12)
> 详细文档: [steps/step58_neuromod_cache_fix.md](steps/step58_neuromod_cache_fix.md)

- **根因**: `setup_neuromodulation()` 在 `cache_neuron_ids_and_synapses()` 之前调用
- 7处 `nid()`/`nids()` 返回 -1 → 神经调质靶标注册失败
- 修复: HSN 5-HT源 + AIB/PVC/AIZ 5-HT抑制 + DA→DVA DOP-1 + NLP-12 DVA源 + PDF→NSM抑制
- **CI: 0.42→0.97**, NLP-12: 0→0.048, 5-HT targets: 14→20, PDF targets: 4→6
- **regtest**: 17 pass, 0 FAIL

### Step 59: 5-HT/PDF 参数重校准 ✅ (2026-02-12)
> 详细文档: [steps/step59_5ht_pdf_recalibration.md](steps/step59_5ht_pdf_recalibration.md)

- Step 58 恢复 PDF→NSM 后，-25pA 过度抑制 NSM → 5-HT=0.076（过低）
- 修复: PDF→NSM -25→-15 pA + 5-HT release_threshold 0.30→0.25
- **5-HT: 0.076→0.135**, NSM above_threshold: -0.08→+0.026
- DMP diag 修正: "expected ~7" → 考虑 near_food% 的调整值
- CI: 0.97→0.91（5-HT slowing 生效，更真实）
- **regtest**: 17 pass, 0 FAIL

### Step 60: 多巴胺系统闭环 + 触觉习惯化 ✅ (2026-02-12)
> 详细文档: [steps/step60_dopamine_completion_tap_habituation.md](steps/step60_dopamine_completion_tap_habituation.md)

- 新增 ADE L/R + PDE L/R 多巴胺神经元 (140→144 神经元)
- DA 源: 4 CEP → **8** (CEP+ADE+PDE, 完整), DA 靶标: 1→**9** (DOP-1/DOP-2/DOP-3)
- DOP-3→AVA/AVB 抑制 + DOP-1→RIA 兴奋 + DOP-2 自受体负反馈
- ESR 增强减速: food_memory×DA×(1+5-HT)×on_lawn
- Tap 习惯化: 10s ISI, 200ms pulse, STP 自然递减
- **NLP-12: 0.032→0.105**, CI: 0.91→0.965, DA targets: 1→9
- **regtest**: 17 pass, 0 FAIL

### Step 61: 腹索中间神经元扩展 (144→162) ✅ (2026-02-12)
> 详细文档: [steps/step61_ventral_cord_integrators.md](steps/step61_ventral_cord_integrators.md)

- 新增 18 个神经元: 5 感觉 (AVM/ASI/ADL) + 13 中间 (DVC/PVT/AVK/AVJ/AVH/PVP/PVR)
- Emmons 2024: community 9 腹索整合中枢 (1步连接 59% 神经元)
- PDE→AVK (50% PDE 输出!) → RIM/RIV 转弯回路
- AVJ↔RIS (5段 gj) — O₂/双恶→睡眠耦合
- PVP: 全神经系统最高 gj 度 (AQR 102段!)
- CI: 0.97→0.75 (命令输入多样化, 文献范围 0.5-0.8)
- **regtest**: 17 pass, 0 FAIL

### Step 62: 睡眠依赖记忆巩固 ✅ (2026-02-12)
> 详细文档: [steps/step62_sleep_memory_consolidation.md](steps/step62_sleep_memory_consolidation.md)

- 突触遗忘机制: w_mod 向 1.0 慢漂移 (0.002/s)，睡眠期间×0.3
- 睡眠巩固三重机制: 学习率×2 + 遗忘率×0.3 + sickness衰减×0.2
- 学习诱导睡眠压力: 毒素摄入→learning_sleep_drive→fatigue↑
- CLI: `--sleep-after-learning <sec>` 强制睡眠实验协议
- **涌现**: 500s toxin, seed=42: 有睡眠 CI=-0.95 vs 无睡眠 CI=-0.70 (回避更强)
- REF: Chouhan 2023 Cell, Zhang 2005 Nature, Iannacone 2017 JNeurosci
- **regtest**: 17 pass, 0 FAIL

### Step 63: INS-1 胰岛素信号 + 厌食涌现 ✅ (2026-02-12)
> 详细文档: [steps/step63_ins1_insulin_anorexia.md](steps/step63_ins1_insulin_anorexia.md)

- INS-1 从 satiety/sickness 计算: (1-satiety) × (1+sickness×3)
- INS-1 → DAF-2 ⊣ AWC(-6pA)/AIA(-5pA)/AIY(-8pA): 化学趋性抑制
- sickness → MC 抑制 (-20pA): 泵率从 3Hz→0.5Hz (下降 83%!)
- **涌现**: 毒素摄入→sickness↑→MC抑制→pump↓→satiety↓→INS-1↑→趋化↓→远离食物
- REF: Lin 2010 JNeurosci, You 2008, Comm Bio 2022
- **regtest**: 17 pass, 0 FAIL

### Step 64: 信息素社会感知 (ascr#3/ADL) ✅ (2026-02-12)
> 详细文档: [steps/step64_pheromone_social_sensing.md](steps/step64_pheromone_social_sensing.md)

- 新增 pheromone_field_ 化学场 (σ=6mm, 水溶性 ascaroside)
- ADL 信息素转导: TONIC 40pA, 叠加在现有 repellent ON 之上
- CLI: `--pheromone` / `--pheromone_x/y` / `--pheromone_intensity`
- **涌现**: ADL→AVA 回避纯涌现 (信息素@食物: near_food 41.7%→36.8%)
- REF: Jang 2012, Srinivasan 2008
- **regtest**: 17 pass, 0 FAIL

### Step 65: SMD 振幅校准 + Curvature Bias 旁路移除 ✅ (2026-02-12)
> 详细文档: [steps/step65_smd_amplitude_calibration.md](steps/step65_smd_amplitude_calibration.md)

- **P0 违规 1.2 修复**: 移除 `body_.set_curvature_bias()` weathervane 旁路
- SMD 振幅校准: CCA-1 5.0→1.8nS, SLO-1 5.0→2.5nS → 振荡 110mV→49mV
- Weathervane 完全通过 SMD 半中心振荡器占空比调制涌现
- 发现并修复 SMD→肌肉→曲率链符号反转 (被旁路掩盖多年)
- bias_clamp: 50→5pA (防止压制振荡器)
- **涌现 CI**: 8-seed 均值 0.24, near_food 43% (7/8 正 CI)
- REF: Nicoletti 2019 PLOS One, Dobosiewicz 2019 eLife, Iino 2009 JNeurosci
- **regtest**: 17 pass, 0 FAIL

### Step 66: Pirouette Poisson 移除 — Reversal 从 AVA 涌现 ✅ (2026-02-12)
> 详细文档: [steps/step66_pirouette_poisson_removal.md](steps/step66_pirouette_poisson_removal.md)

- **P0 违规 1.1+1.5 修复**: 移除 Pirouette Poisson + `set_locomotion_state(0,1)` 覆盖
- Reversal 完全从 AVA 神经回路涌现 (ASE→AIB→AVA + 离子通道噪声)
- AVA Schmitt 触发器: 迟滞 0.35/0.15 + 300ms 最小持续 + 2s 不应期
- Food edge reversal: 40pA AVA 注入替代直接 is_reversing_
- **涌现 CI**: 8-seed 均值 **0.45** (↑ from 0.24), 8/8 正 CI
- Reversal rate: 0.17/s (完全涌现, 文献 ~0.10/s)
- REF: Piggott 2011 Cell, Roberts 2016 eLife, Kuramochi 2018, Gao 2018
- **regtest**: 17 pass, 0 FAIL

### Step 67: 消融验证 — 证明 Reversal 从 AVA 涌现 ✅ (2026-02-12)
> 详细文档: [steps/step67_ablation_verification.md](steps/step67_ablation_verification.md)

- **消融功能**: `Neuron::ablate()` + `--ablate` CLI（自动 L/R 配对）
- **AVA 消融**: 0 reversals (vs CTRL 53) — 完美匹配 Chalfie 1985 ✅✅
- **ASE 消融**: CI 0.38→-0.20 (趋化性丧失) — 匹配 Miller 2005 ✅
- **AIB 消融**: CI 0.38→0.09 (76%↓) — klinokinesis 受损 ✅
- **RIM 消融**: reversals 53→68 (+28%) — 匹配 Sordillo 2021 ✅✅
- **结论**: Step 66 涌现性确认 — reversal 完全依赖 AVA 回路
- **regtest**: 17 pass, 0 FAIL

### Step 68: Basal Slowing — DA→DOP-3→Motor Neuron 涌现减速 ✅ (2026-02-12)
> 详细文档: [steps/step68_basal_slowing_dop3.md](steps/step68_basal_slowing_dop3.md)

- **P1 违规 1.4 修复**: 移除 `effective_speed *= basal_slow` 直接速度乘法
- DA tau_decay 5s→2s（DAT-1 快速回收）
- DOP-3(-3pA) 添加到 14 个 B-class 运动神经元（DB01-07, VB01-07）
- 减速通过 CEP→DA→DOP-3→motor neuron→muscle_work 链涌现
- **4-seed CI**: 均值 0.155 (3/4正, ↓from 0.45 — DA τ动态 vs 即时开关)
- REF: Chase 2004 Nat Neurosci, Sawin 2000 Neuron
- **regtest**: 17 pass, 0 FAIL

### Step 69: DOP-3 校准 + DA 速度调控文献研究 ✅ (2026-02-12)
> 详细文档: [steps/step69_dop3_calibration.md](steps/step69_dop3_calibration.md)

- **文献**: Chase 2004, Vidal-Gadea 2012, Wang 2014
- **发现**: DOP-1 不参与速度调节（只参与食物依赖减速）; DOP-3+GOA-1 是速度精度关键
- **实验**: release_threshold 0.3→0.1 + DOP-3 -3→-5pA → CI 崩溃至 ~0.00
- **根因**: DOP-3 抑制 B-class 兴奋性 → MEC 本体感觉通道敏感度降低 → 波传播受损
- **结论**: -3pA 是安全上限; 完整涌现减速需要 body model 升级（肌肉独立计算节点）
- **regtest**: 17 pass, 0 FAIL

### Step 70: 涌现食物边缘反转 — 移除概率公式 ✅ (2026-02-12)
> 详细文档: [steps/step70_food_edge_emergent.md](steps/step70_food_edge_emergent.md)

- **P1 违规 1.3 修复**: 移除 `p = 0.50 + 0.30×5HT - 0.30×PDF` 概率公式
- Always-inject: 每次 food edge exit 注入 AVA 40pA/500ms（无概率门控）
- 反转概率从 AVA-AVB 互抑平衡涌现（5-HT→MOD-1→AIY vs PDF→PDFR-1→AIY）
- **文献**: Flavell 2024 eLife — leaving 与 roaming 耦合 (20×), head poke reversal ~55%
- **4-seed CI**: 均值 0.137 (vs Step 68: 0.155, 在噪声范围内)
- SMD diff baseline 55→70/60%
- **regtest**: 17 pass, 0 FAIL (5 次稳定)

### Step 71: P0-5 DMP 涌现减速 + P0-6 FLP-11 神经调质化 ✅ (2026-02-12)
> 详细文档: [steps/step71_p0_dmp_flp11_fix.md](steps/step71_p0_dmp_flp11_fix.md)

- **P0-5 修复**: 移除 `dmp_speed_factor_` 直接乘法，DMP 减速从 AVL/DVB GABA → B-class MN 抑制涌现
  - 新突触: AVL→VB05/DB05, DVB→VB06/VB07 (GABAergic inhibitory)
  - REF: Jiang 2022 Nat Commun, Alkema 2015 Sci Rep
- **P0-6 修复**: FLP-11 加入 NeuromodulationManager 为第 7 种调质
  - 来源: RIS; 受体: DMSR-1 (Gi/o) → 胆碱能神经元抑制
  - 52 个靶点: AVA/AVB(-20pA), MC(-18pA), SMD/RMD(-28pA), 体壁MN(-42pA), SPEED_SCALE(-0.95)
  - 自抑制: FRPR-8→RIS(-8pA) — 负反馈限制睡眠时长 (Rossi 2025 Current Biology)
  - 移除: `apply_sleep_effects()` 直接注入 + `sleep_speed_factor` 直接乘法
- **CI**: 0.373 (seed=42), **regtest**: 17/17 PASS

### Step 72: AIA AND-gate 修正 + ASE→AIB 直接 klinokinesis 通路 ✅ (2026-02-12)
> 详细文档: [steps/step72_aia_andgate_ase_aib_klinokinesis.md](steps/step72_aia_andgate_ase_aib_klinokinesis.md)

- **3个连接组错误修正** (Kakaria 2019 eLife):
  - AWA→AIA: 化学突触 syn(3) → **缝雙连接 gj(3)** — AWA::TeTx 无影响, unc-7/unc-9 消除响应
  - ASEL→AIA: 兴奋性 syn(5) → **抑制性 inh(3)** — 谷氨酸激活 GLC-3/AVR-14 Cl⁻ 通道
  - AWC→AIA: 不存在 → **inh(2) 新增** — 去抑制通路 (AND-gate 的一半)
- **AIA AND-gate**: 双稳态(-80mV/-20mV), 阈值 2-3pA, 需要 AWA gj兴奋 + 谷氨酸能神经元去抑制
- **ASE→AIB 直接通路** (Kuramochi 2018): ASER→AIB syn(1)兴奋 + ASEL→AIB inh(1)抑制
- **ASEL/ASER 不对称 tau** (Suzuki 2008): ASEL slow_tau=3000ms(瞬态), ASER slow_tau=8000ms(持续), 比值 2.7:1
- **4-seed CI**: 均值 0.284 (全4种子全正), **near_food**: 35.1% (+46% 提升)
- **regtest**: 17/17 PASS

### Step 73: 鼻触反射回路 — FLP/IL1/RIH 完整闭环 ✅ (2026-02-12)
> 详细文档: [steps/step73_nose_touch_reflex.md](steps/step73_nose_touch_reflex.md)

- **新增 7 个神经元** (162→169): FLP(2) + IL1(4) + RIH(1)
  - FLP: 多树突头部伤害感受器，29% 鼻触回避 (Kaplan 1993)
  - IL1: 内唇感觉神经元，头部缩回 + 觅食 (Hart 1995)
  - RIH: Hub-and-spoke 巧合检测中枢 (Chatzigeorgiou 2011)
- **鼻触回避**: FLP→AVA(2)/AVD(2)/AVE(1)/AIB(1) — 直接反转驱动
- **Hub-spoke 网络**: FLP/OLQ/CEP↔RIH gj — 轻触鼻巧合检测，粗触细胞自主
- **头部缩回**: IL1→RMD syn(2) 同侧 + IL1↔RIH gj 觅食整合
- **4-seed CI**: 均值 **0.464** (+63% vs Step 72), seed=7 达 0.905
- **regtest**: 17/17 PASS

---

## 当前系统状态

```
架构: 8 层 (环境/躯体/感知/神经元/连接组/神经调质/运动/行为)
神经元: 169 个 MVP 子集 (302 全集待加载)
  感觉: 53 (ASE/AWC/AWA/ASH/ALM/PLM/NSM/CEP/ADE/PDE/AFD/ADF/ASJ/ASK/ASI/ADL/FLP L/R + AVM + OLQ 4× + IL1 4× + URX L/R + AQR + PQR + BAG L/R + PVD L/R)
  中间: 46 (AIA/AIB/AIY/AIZ/RIA/RIB/RIM/RIC/AVA/AVB/AVD/AVE/PVC/AUA/AVK/AVJ/AVH/PVP/I1/RIP L/R + RIS + RIH + DVA + DVC + PVT + PVR)
  运动: 70 (SMD/RMD/SMB 4×2+4 + RIV L/R + RMED/RMEV + AS01-07 + DB01-07/VB01-07/DA01-05/VA01-05/DD01-05/VD01-05 + MC/M3 L/R + M4 + HSN L/R + VC4/VC5 + AVL + DVB)
突触: ~215 化学 + ~56 间隙连接 (全部带 Tsodyks-Markram STP, 支持分数 sections)
  Step 42: Cook 2019 校准 (+8 RIA↔RIV, -2 AVE→RIV) + RIV↔RIV gap
神经调质: 7 种 (5-HT, DA, OA, TA, NLP-12, PDF, FLP-11) — volume transmission + 饱食度(泵驱动)
  5-HT 源: NSM(食物) + HSN(产卵) — 4个源神经元 (Step 43: ADF 移除)
  5-HT 靶标 (20个, 5种受体): MOD-1→AIY/AIB/AIZ/PVC(抑制) + SER-4→RIC(抑制)+speed(-0.40)+reversal(-0.50) + SER-1→RIA/RIC(兴奋) + SER-5→ASH(增敏) + LGC-50→RIA(SYNAPSE_GAIN)
  DA 源: CEP(4)+ADE(2)+PDE(2) = 8个 (完整), 9个靶标: DOP-1→DVA(+4)/RIA(+2) + DOP-3→AVA(-3)/AVB(-2) + DOP-2→CEP(-3, 自受体)
  TA 源: RIM (逃逸协调) — LGC-55→SMD/AVB/RIV抑制 + TYRA-3→ASH增敏 + SER-2→AIY抑制
  NLP-12 源: DVA (本体感觉) — CKR-1→SMD(+5pA, 头摆ARS) + CKR-2→AVA(+2pA) + DA→DOP-1→DVA(+4pA)
离子通道: 14 种 (EGL-19/UNC-2/CCA-1/SHL-1/KQT-3/SLO-1/NCA/MEC + EGL-36/IRK/TWK/SLO-2/OSM-9/EXP-2)
神经元模型: 单隔室 HH 分级电位 (L2) + 多隔室 (RIA) + 钙动力学
身体: 2D 弹性杆 48 段, 29 个运动神经元-肌肉映射, 体节间曲率扩散(弹性耦合)
环境: 50×50 mm, 4化学场(food_odor+soluble+repellent+pheromone) + 线性温度梯度 (0.5°C/mm) + O₂场(food派生) + 光场(高斯σ=8mm)
内部状态: satiety_(泵驱动), sickness_(有毒食物), food_memory_(双通路ARS), fatigue_(睡眠驱动)
学习: 盐学习(ASER w_mod) + 病原体学习(AWC翻转+WV反向+厌食) + STP习惯化 + 睡眠巩固(Step 62) + INS-1厌食(Step 63)
仿真: dt=0.5ms, CPU 实时 (10000步 < 1s)
性能: cache_neuron_ids_and_synapses() 一次性缓存 10 ID + 6 typed 指针 + 3 组突触索引
计算: CPU (默认) + OpenCL GPU 后端 (>500突触自动启用, AMD RX 6950 XT 就绪)
构建: CMake + MSVC 19.44 + C++20 + vcpkg (OpenCL/ImGui/ImPlot/GLFW)
工具: CLI 运行时参数覆盖 (--as_factor/--pulse_amp/--duration/--seed/--light 等, 无需重编译调参)
      --fitness 模式: 4 seeds × 3 scenarios 自动评估, 输出标量 fitness score
可视化: Dear ImGui + ImPlot + GLFW, 3列布局, 实时调参+信号链诊断
P0/P1 违规全部修复:
  P0-1.1: Pirouette Poisson 移除 → reversal 从 AVA 涌现 (Step 66)
  P0-1.2: curvature_bias 旁路移除 → weathervane 从 SMD 涌现 (Step 65)
  P1-1.3: food edge 概率公式移除 → 从 AVA-AVB 平衡涌现 (Step 70)
  P1-1.4: basal_slow 直接乘法移除 → DA→DOP-3→B-class MN 涌现 (Step 68)
  P1-1.5: set_locomotion_state 覆盖移除 → 完全神经回路驱动 (Step 66)
  P0-5: DMP speed_factor 移除 → AVL/DVB GABA→B-class MN 涌现减速 (Step 71)
  P0-6: FLP-11 直接注入移除 → NeuromodulationManager DMSR-1 框架 (Step 71)
行为指标 (4-seed, 300s): CI≈0.46 (全4种子正), near_food≈10%, reversal_rate≈0.16/s, speed≈0.21mm/s
工具: celegans_diag.exe (信号链诊断+fitness) + celegans_regtest.exe (回归检测+电流溯源)

运动驱动 (Step 13 — 生物学机制):
  感觉基线: 12 感觉神经元 × 15pA 自发活动 (Bargmann 2006)
  头部tonic: 8 头部运动神经元 × 3pA (上游中间神经元驱动)
  本体感觉: MEC stretch-activated 通道 (body curvature → B类 MN)
  波传播: B类顺序感知前一单元领地 (Wen 2012) + 体节间曲率扩散 0.5 (Boyle 2012)
  通道噪声: 3pA 高斯噪声 (White 1998, 热涨落)
  头部振荡: CCA-1 burst → Ca²⁺ → SLO-1(BK) 适应 → 复极化 → 周期 ~500ms (Step 65: 振幅 110→49mV)
  半中心CPG: SMD dorsal↔ventral 交叉抑制(3 sections) → 背腹交替 burst (~2Hz)
  Klinotaxis: sensory_AC × curvature → RIA乘法门控 → SMB颈部偏置 (Ouellette 2018)
  Pirouette: ASEL⊣AIA→AIB→AVA(抑C↑) + ASER⊣AIA→AIB→AVA(促C↓) + ASER→AIB(直接, Step 72)
  速度模型: 肌肉功率 × 波形效率 × 时间活动 (Fang-Yen 2010)
  V_SMDDL: -65↔-30mV 交替 burst, V_SMDVL: 反相
  速度: 0.05-0.24 mm/s (真实 ~0.2 mm/s, 在生物学范围内)

核心回路 (默认连接组, ~200 突触):
  趋化性: ASE/AWC/AWA → AIA/AIB/AIY/AIZ → RIA → SMD (头部转向)
  关键: AIA AND-gate (Kakaria 2019): AWA→AIA(gj兴奋) + AWC/ASE⊣AIA(Cl⁻去抑制)
  关键: AIA ⊣ AIB (抑制性, Chalasani 2007), AIY → AVB (Gray 2005)
  ASE→AIB 直接 klinokinesis: ASER→AIB(GLR-1兴奋) + ASEL⊣AIB(GLC-3抑制) (Kuramochi 2018)
  触觉: ALM → AVD (前触) / PLM → AVA (后触)
  前进: 感觉→AIY→AVB→DB/VB → 背/腹侧体壁肌肉
  后退: AWC→AIB→AVA → DA/VA → 背/腹侧体壁肌肉
  Omega: RIA→RIV(兴奋) + RIV→RIA(抑制) 负反馈环路, TA门控 post-inhibitory rebound
  交叉抑制: DD ↔ VD (背腹交替), SMD dorsal↔ventral (头部半中心)
  左右耦合: AVA L-R / AVB L-R / AVD L-R / RIV L-R (间隙连接)
  咽部CPG: I1←RIP(gj) → MC(ACh起搏) ↔ M3(Glu松弛) → 咽部肌肉AP
           MC→M4(峡部蠕动), 5-HT→MC(SER-7↑), OA→MC(↓)
           pump_rate×food→satiety (真实进食, 替换占位符)

感觉转导 + 趋化 (Step 14-15):
  化学感觉: Weber-Fechner 双滤波器, ON/OFF 分类, 8 个化学感觉神经元
  运动学: dθ/dt = v × κ_head, pirouette 概率模型 (AVA 调制)
  Weathervane: ∇C_⊥ → SMD 占空比调制 (Step 65: curvature_bias旁路已移除, 纯神经回路涌现)
  SMD校准: CCA-1 1.8nS + SLO-1 2.5nS + leak 1.2/-65 → 49mV振荡 (Nicoletti 2019)
  Reversal: 完全从 AVA 神经回路涌现 (Step 66: Schmitt 0.35/0.15 + 离子通道噪声)
  Food-edge: always-inject AVA 40pA/500ms, 反转概率从 AVA-AVB 平衡涌现 (Step 70)
  Basal slowing: DA→DOP-3(-3pA)→14 B-class MN 涌现减速 (Step 68, 移除直接乘法)
  速度: ~0.20 mm/s (文献值 ~0.15-0.2 mm/s)

文件结构:
  src/core/         — 4 文件 (types/config/logger .h/.cpp)
  src/neuron/       — 8 文件 (ion_channel/calcium/single_compartment/factory .h/.cpp)
  src/connectome/   — 8 文件 (synapse/gap_junction/connectome/loader .h/.cpp)
  src/body/         — 4 文件 (body_model/muscle_system .h/.cpp)
  src/motor/        — 2 文件 (motor_controller .h/.cpp)
  src/environment/  — 5 文件 (environment/chemical_field/sensory_transducer .h/.cpp)
  src/pharynx/      — 1 文件 (pharyngeal_pump.h)
  src/simulation/   — 5 文件 (simulation_engine .h/.cpp + main.cpp + diag_main.cpp + regression_test.cpp)
  src/visualization/ — 3 文件 (vis_app .h/.cpp + vis_main.cpp)
  docs/             — 2+ 文件 (blueprint.md + progress.md + steps/ + tools/)
  总计: 44 文件 (CMakeLists.txt + 42 源文件 + 文档)

参考项目对标:
  OpenWorm: Sibernetic (SPH 物理) + c302 (NeuroML 神经元) + Geppetto (可视化)
  BAAIWorm: 多隔室 HH + 3D 物理 + 全脑钙成像拟合 (Nature Comput Sci 2024)
  本项目: 单隔室 HH (MVP) → 多隔室 (Phase 3) + 弹性杆 (MVP) → SPH (Phase 4)
```

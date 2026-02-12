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

### Step 20-22: 神经调质与突触可塑性 ✅ (2026-02-10)
> 中文档: [steps/step20-22_neuromodulation_plasticity.md](steps/step20-22_neuromodulation_plasticity.md) | 子文档: [step20](steps/step20_neuromodulation.md) / [step21](steps/step21_synaptic_plasticity.md)

| Step | 核心内容 | 关键指标 |
|------|---------|---------|
| **20** | NeuromodulationManager + 4调质(5-HT/DA/OA/food_memory) + 觅食循环涌现 | 神经元 64→**72**, CI=0.520 |
| **21** | Tsodyks-Markram STP(STD/STF) + 盐学习 + Omega偏置 + Klinokinesis | 10-seed CI=0.43, good=8/10 |
| **22** | OpenCL GPU 后端, >2000突触自动启用 (AMD RX 6950 XT) | CPU/GPU 自动切换 |

- **本组关键**: Layer 5-6 建立 — 觅食循环(roam↔dwell) + 突触状态(n/p/w_mod) + ARS局部搜索 + GPU基础设施

### Step 23-24: 新感觉模态与咽部系统 ✅ (2026-02-10)
> 中文档: [steps/step23-24_sensory_pharynx.md](steps/step23-24_sensory_pharynx.md) | 子文档: [step23](steps/step23_thermotaxis.md) / [step24](steps/step24_pharyngeal_pump.md)

| Step | 核心内容 | 关键指标 |
|------|---------|---------|
| **23** | AFD 温度趋性, 共享 AIY 下游通路, 架构通用性验证 | 不破坏趋化, CI 正常 |
| **24** | 咽部泵食 4 相状态机, 9 咽部神经元, 真实进食替换占位符 | satiety 0.4-0.55, 泵频 2-3Hz |
| **Post-24** | Pirouette 调制修复 + SMD 振荡器修复 + regtest 工具 | CI=**0.746**, SMD 222→115mV |

- **本组关键**: 温度感觉验证架构可扩展性 + 占位符→真实咽部泵食 + regtest 工具建立

### Step 25-26: 化学回避与病原体学习 ✅ (2026-02-10)
> 中文档: [steps/step25-26_chemical_avoidance_learning.md](steps/step25-26_chemical_avoidance_learning.md) | 子文档: [step25](steps/step25_chemical_avoidance.md) / [step26](steps/step26_pathogen_learning.md)

| Step | 核心内容 | 关键指标 |
|------|---------|---------|
| **25** | ASH 伤害感觉(TONIC) + repellent_field_ + 排斥 Weathervane 涌现绕路 | CI=0.655, 绕路 r≥1.4mm |
| **26** | 条件性病原体学习: sickness→AWC翻转+厌食, 三层回避(ASH/AWC/5-HT) | **CI=-0.18**(反向!), +ADF, +sickness |

- **本组关键**: 先天回避(ASH) + 学习性回避(AWC翻转) + 三层化学回避架构 + sickness 内部状态

### Step 27-28: 睡眠与多隔室模型 ✅ (2026-02-10)
> 中文档: [steps/step27-28_sleep_multicompartment.md](steps/step27-28_sleep_multicompartment.md) | 子文档: [step27](steps/step27_sleep_lethargus.md) / [step28](steps/step28_multi_compartment.md)

| Step | 核心内容 | 关键指标 |
|------|---------|---------|
| **27** | RIS/FLP-11 睡眠-觉醒循环, fatigue 内部状态, 涌现唤醒阈值 | 觉醒/睡眠 ~100s/80s, 速度比 10:1 |
| **28** | RIA 3隔室模型(soma/nrV/nrD), Ca²⁺ nrV-nrD 差异→klinotaxis | CI avg +0.20, sleep 20% |

- **本组关键**: +fatigue 内部状态 + 多隔室神经元模型(RIA) + 睡眠-觉醒循环涌现

### Step 29-33: 运动回路精化 ✅ (2026-02-10)
> 中文档: [steps/step29-33_motor_circuit_refinement.md](steps/step29-33_motor_circuit_refinement.md) | 子文档: [step29](steps/step29_proprioceptive_wave.md) / [step30](steps/step30_tyramine_rim.md) / [step31](steps/step31_riv_omega_turn.md) / [step32](steps/step32_as_motor.md) / [step33](steps/step33_olq_rme.md)

| Step | 核心内容 | 关键指标 |
|------|---------|---------|
| **29** | B类顺序本体感觉波 + 半隐式Euler, speed +153% | speed 0.076→**0.192** |
| **30** | Tyramine(第4调质) — LGC-55→SMD/AVB抑制, 承诺式逆转涌现 | TA conc 0.27 |
| **31** | RIV omega turn 涌现, 删除硬编码P(omega), TA门控 | omega=4, 硬编码→涌现 |
| **32** | AS01-05 背侧偏置, SMD相位→omega门控, TuningParams CLI | ω/rev 100%→**67%** |
| **33** | OLQ鼻触 + RME(GABA)头部增益控制, D/V对称修复 | D/V 3.6×→**1.06×** |

- **本组关键**: 波传播+TA逃逸+Omega涌现+AS背侧门控+RME对称修复 — 运动系统从基础→精化

### Step 34-35: 气体感觉 (O₂/CO₂) ✅ (2026-02-10)
> 中文档: [steps/step34-35_gas_sensing.md](steps/step34-35_gas_sensing.md) | 子文档: [step34](steps/step34_oxygen.md) / [step35](steps/step35_co2_bag.md)

| Step | 核心内容 | 关键指标 |
|------|---------|---------|
| **34** | URX/AQR/PQR O₂感觉 + AUA中继 + NPR-1调制, O₂场=21%-13%×food | URX S=0.24, O₂=19.3% |
| **35** | BAG CO₂感觉 + URX交叉抑制, 涌现: 饥饿留食 vs 饱食+生病离开 | sickness=1时 CI +0.57→**+0.08** |

- **本组关键**: O₂(食物代理) + CO₂(对抗信号) — 气体感觉完成，涌现饥饿/饱食决策

### Step 36-38: 全身感觉与产卵 ✅ (2026-02-10)
> 中文档: [steps/step36-38_body_sensing_egglaying.md](steps/step36-38_body_sensing_egglaying.md) | 子文档: [step36](steps/step36_proprioception.md) / [step37](steps/step37_ave_omega_grading.md) / [step38](steps/step38_egg_laying.md)

| Step | 核心内容 | 关键指标 |
|------|---------|---------|
| **36** | DVA(TRP-4全身曲率) + PVD(harsh touch+本体感觉) 双模态 | DVA S=0.327, wave GOOD |
| **37** | AVE后退指令分级: 弱→短reversal, 强→长reversal+omega | ω/rev 100%→**85%** |
| **38** | HSN/VC产卵系统, egg_pressure→HSN burst, HSN为5-HT源 | eggs=2, 5-HT源 4→**6** |

- **本组关键**: DVA/PVD全身感觉 + AVE强度分级omega门控 + 产卵系统(HSN新增5-HT源)

### Step 39-42: 运动扩展与行为整合 ✅ (2026-02-11)
> 中文档: [steps/step39-42_motor_expansion_integration.md](steps/step39-42_motor_expansion_integration.md) | 子文档: [step39](steps/step39_motor_expansion.md) / [step40](steps/step40_stability_audit.md) / [step42](steps/step42_connectome_calibration.md)

| Step | 核心内容 | 关键指标 |
|------|---------|---------|
| **39** | B/A/D 类 MN 3→7/5/5 扩展(+18), 7级顺序本体感觉 | muscle work 0.316→**0.338** |
| **40** | 5-HT稀释bug修复(0.34→0.73), 参数校准, 10-seed验证 | omega=0.44±0.11, 8/8 GOOD |
| **41** | 后退运动+觅食调制+Warmup, NSM/CEP阈值修正 | 速度调制: 5-HT -40%, DA -30% |
| **42** | Cook 2019 EM校准, RIA↔RIV负反馈环路, 性能优化, Fitness框架 | --fitness CLI, 4-seed×3-scenario |

- **本组关键**: MN扩展→连续覆盖 + Cook 2019校准 + 5-HT修复 + Fitness自动评估框架

### Step 43-49: 觅食行为完善 ✅ (2026-02-11)
> 中文档: [steps/step43-49_foraging_neuromod.md](steps/step43-49_foraging_neuromod.md) | 子文档: [step43](steps/step43_pathogen_avoidance.md) / [step44](steps/step44_off_food_search.md) / [step45](steps/step45_nlp12_foraging.md) / [step46](steps/step46_pdf_roaming.md) / [step47](steps/step47_food_edge_reversal.md) / [step48](steps/step48_foraging_cycle.md) / [step49](steps/step49_5ht_pathway.md)

| Step | 核心内容 | 关键指标 |
|------|---------|---------|
| **43** | AWB/ADF回路重构, ADF 5-HT源移除, TA→SER-2→AIY | 病原体回避闭环 |
| **44** | Off-food reversal rate调制, 修复死代码, ARS pirouette bonus | CI 0.131→**0.685** |
| **45** | NLP-12(DVA→CKR-1→SMD) + NSM肠道感觉 + 5-HT阈值修复 | 5-HT 0.18→**0.53**, NLP-12=6靶 |
| **46** | PDF-1 roaming神经肽, 5-HT/PDF双稳态对抗 | CI 0.72-0.92 |
| **47** | Head poke reversal + basal slowing (DOP-3 volume transmission) | CI mean 0.67 |
| **48** | PDF⊣NSM互抑制闭环, 正反馈环完成 | OA 0.24-0.40↑, near 33% |
| **49** | 5-HT 5种受体闭环(MOD-1/SER-4/SER-1/SER-5/LGC-50), 靶标8→18 | CI mean **0.70** |

- **本组关键**: NLP-12+PDF双调质 + 5-HT受体多样性 + PDF⊣NSM互抑制 → 觅食循环完整闭环

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

### Step 74: Regtest 升级 — 连接组完整性检测 ✅ (2026-02-12)
> 详细文档: [steps/step74_regtest_upgrade.md](steps/step74_regtest_upgrade.md)

- **新增 3 个确定性指标** (17→20): Neuron count(169), Synapse count(331), Gap junction count(96)
- 通过 `Connectome::num_neurons/synapses/gap_junctions()` API 获取
- tolerance=1%: 任何连接组构建错误都会立即被捕获
- **regtest**: 20/20 PASS

### Step 75: AWB→RMG 病原体嗅觉回避完整回路 ✅ (2026-02-12)
> 详细文档: [steps/step75_awb_rmg_pathogen_aversion.md](steps/step75_awb_rmg_pathogen_aversion.md)

- **新增 2 个神经元** (169→171): RMG L/R — 病原体回避 hub (Cook 2019 重分类为中间神经元)
- **AWB↔RMG** 缝隙连接 (2 sec) — Filipowicz 2022: AWB 电耦合到 AUA **和** RMG
- **RMG→AVA/AVD** 化学突触 — 驱动习得性反射后退运动
- **AUA→AVD** 化学突触 (0.5 sec) — 完善 AUA 平行后退通路
- 4 层回路完成: AWB → AUA/RMG → AVA/AVD → 运动神经元
- **regtest**: 20/20 PASS (171/337/98)

### Step 76: 增强减速响应 (ESR) — 涌现行为 ✅ (2026-02-12)
> 详细文档: [steps/step76_esr_enhanced_slowing.md](steps/step76_esr_enhanced_slowing.md)

- **ESR 涌现机制**: 饥饿 → MOD-1/SER-4 受体慢速上调 (τ=60s) → 5-HT 效应放大
- **回路级效应**: 额外抑制电流注入 AIY(-8pA) + PVC(-8pA) + RIC(-4pA) × receptor × 5-HT
- **非直接速度操控**: 速度降低从 AIY/PVC 抑制 → 前进驱动↓ → 运动神经元活性↓ 涌现
- BSR (DA, Step 68) vs ESR (5-HT, 此步骤): 两条独立通路，匹配 Sawin 2000 cat-2/tph-1 解离
- **regtest**: 20/20 PASS (30s 内受体上调不充分，不影响基线)

### Step 77: 盐学习规则修复 + 涌现验证 ✅ (2026-02-12)
> 详细文档: [steps/step77_salt_learning_fix.md](steps/step77_salt_learning_fix.md)

- **Bug**: 旧 Hebbian 规则 (S_pre×S_post) 300s 仅产生 0.6% w_mod 变化
- **修复**: 移除 S_post (PI3K 细胞自主, Tomioka 2006) + lr 10× (时间尺度压缩)
- **结果**: w_mod 降至 0.73-0.86 (-14~27%)，seed=7 **CI 翻转为负 (-0.142)** — 盐厌恶涌现
- **涌现链**: 饥饿 → learn_signal<0 → ASER w_mod↓ → ASER→AIB 减弱 → 趋化↓ → CI↓
- **regtest**: 20/20 PASS (171/337/98)

### Step 78: 轻触习惯化涌现验证 ✅ (2026-02-12)
> 详细文档: [steps/step78_tap_habituation.md](steps/step78_tap_habituation.md)

- **Bug 修复**: 触觉神经元 I_ext 不重置 → pool 永久耗竭；STP tau_rec=4s 太快
- **STP 调优**: tau_recovery 4000→15000ms, alpha_d 0.0005→0.001 (匹配 Rankin 1990 恢复时间)
- **结果**: 4 seed 完美一致 — **前 5 次 tap 100% 反转 → 后 5 次 0%** (习惯化涌现)
- **多机制涌现**: STP 耗竭 + gap junction 反馈 + 回路适应 + Schmitt trigger 阈值
- **regtest**: 20/20 PASS (171/337/98)

### Step 79: 反习惯化/敏化涌现 ✅ (2026-02-12)
> 详细文档: [steps/step79_dishabituation.md](steps/step79_dishabituation.md)

- **双过程理论** (Groves & Thompson 1970): S-过程(STP 习惯化) + R-过程(敏化)
- **实现**: apply_sensitization() — ASH 强激活 → sensitization_ 慢衰减(τ=30s) → 触觉突触 pool 恢复
- **Bug 修复**: ASH/FLP 未在 nids_ 缓存注册 → nids("ASH") 返回空 → 敏化完全无效
- **结果**: 4 seed 一致 — 习惯化(0%) → **反习惯化(50-75%)** → 再习惯化(0%)
- **CLI**: --dishabit-at <sec> 指定反习惯化刺激时间
- **regtest**: 20/20 PASS (171/337/98)

### Step 80: 温度培养学习 (Tc 可塑性) ✅ (2026-02-12)
> 详细文档: [steps/step80_thermotaxis_learning.md](steps/step80_thermotaxis_learning.md)

- **喂食门控 Tc 适应** (Hedgecock & Russell 1975, Chi 2007): 食物存在→Tc 趋近当前温度，离食→Tc 远离
- **实现**: ThermoTransducer::adapt_tc() + food_here 信号（非 satiety）
- **涌现**: 有食→FOOD wins(+21mm), 无食→TEMP wins(-10mm) — 行为分离
- **Tc 学习**: 有食 seed 7 dTc=-0.69°C（正关联），无食 dTc=+1.43°C（饥饿厌恶）
- **regtest**: 20/20 PASS (171/337/98)

### Step 81: Phasmid 尾部化学感觉 (PHB/PHA) ✅ (2026-02-12)
> 详细文档: [steps/step81_phasmid_tail_chemosensation.md](steps/step81_phasmid_tail_chemosensation.md)

- **+4 神经元**: PHBL/PHBR(尾部排斥物) + PHAL/PHAR(尾部食物/信息素)
- **定向逃逸** (Hilliard 2002): PHB⊣AVA 抑制反转 + PHB→PVC 促进前进
- **实现**: apply_tail_chemosensation() 在尾部位置采样排斥物/食物
- **头尾拮抗**: ASH(头)→AVA(+) vs PHB(尾)→AVA(-) → AVA 整合定向逃逸
- **regtest**: 20/20 PASS (175/347/104)

### Step 82: AIN/RIG 中继中间神经元 ✅ (2026-02-12)
> 详细文档: [steps/step82_ain_rig_interneurons.md](steps/step82_ain_rig_interneurons.md)

- **+3 神经元**: AINL/AINR(化学趋化中继) + RIG(腹索→导航中继)
- **AIN**: ASE→AIN→AIY/RIA 并行化学趋化通路，增强趋化信号鲁棒性
- **RIG**: DVC/PVT→RIG→AIY/AIZ/RIA/AVK 腹索→头部导航桥接 (Emmons 2024)
- **新闭合回路**: ASK→AVH→RIG→AIZ/RIA 信息素→感觉桥→导航
- **regtest**: 20/20 PASS (178/367/107)

### Step 83: AVF/LUA 尾部中继 + 第二前进命令 ✅ (2026-02-12)
> 详细文档: [steps/step83_avf_lua_tail_relay.md](steps/step83_avf_lua_tail_relay.md)

- **+4 神经元**: AVFL/AVFR(第二前进命令) + LUAL/LUAR(尾部感觉中继)
- **AVF**: PHA/PVC→AVF→AVB 第二前进命令通路 (Emmons 2024)
- **LUA**: PHB/PLM→LUA→AVD/PVC 尾部感觉中继，闭合 Step 81 PHB/PHA 下游回路
- **regtest**: 20/20 PASS (182/381/111)

### Step 84: 腹索运动神经元扩展 (DA5→9, VA5→12) ✅ (2026-02-12)
> 详细文档: [steps/step84_ventral_cord_expansion.md](steps/step84_ventral_cord_expansion.md)

- **+11 神经元**: DA06-09(4) + VA06-12(7) — A-class 反向运动神经元完整补全
- **体节精化**: DA ~8→~4 段/神经元，VA ~8→~3 段/神经元 (Haspel 2011)
- **A-class 内源振荡器** (Gao 2018 eLife): AVA 双重调控 (gj+化学突触)
- **regtest**: 20/20 PASS (193/412/111)

### Step 85: 神经调质再校准 (NSM/DA/PDF/bias_clamp) ✅ (2026-02-12)
> 详细文档: [steps/step85_neuromodulation_recalibration.md](steps/step85_neuromodulation_recalibration.md)

- **NSM drive** 30→50pA: 5-HT 从 0.09→0.53 (Step 57 离子通道漂移补偿)
- **CEP/ADE gain** 20→35/15→25: DA 从 0.003→0.23, DOP-3 减速恢复
- **PDF→NSM** -15→-10pA: 5-HT 可与 PDF 竞争，双稳态保留
- **bias_clamp** 5→12pA: weathervane 不再饱和，CI 0.25→0.58
- **regtest**: 20/20 PASS (无基线变更)

### Step 86: D-class GABAergic 扩展 (DD5→6, VD5→13) ✅ (2026-02-12)
> 详细文档: [steps/step86_dd_vd_expansion.md](steps/step86_dd_vd_expansion.md)

- **+9 神经元**: DD06(1) + VD06-13(8) — D-class GABAergic 交叉抑制完整补全
- **交叉抑制精化**: DD ~8→~6 段, VD ~8→~3 段 (White 1986)
- **VD→AVA 逆向抑制** (Gao 2015 Nat Commun): UNC-49 GABA受体偏向奖励行为
- **regtest**: 20/20 PASS (202/434/112)

### Step 87: VB B-class 前进运动扩展 (VB7→11) ✅ (2026-02-12)
> 详细文档: [steps/step87_vb_expansion.md](steps/step87_vb_expansion.md)

- **+4 神经元**: VB08-11 — B-class 胆硷能前进 MN 完整补全 (11/11)
- **VB↔VB 本体感觉耦合**: 10对相邻间隙连接 (Wen 2012 Neuron)
- **体节精化**: VB ~5-6→~3-4 段/神经元
- **regtest**: 20/20 PASS (206/443/122)

### Step 88: AS 背侧单极 MN 扩展 (AS7→11) ✅ (2026-02-12)
> 详细文档: [steps/step88_as_expansion.md](steps/step88_as_expansion.md)

- **+4 神经元**: AS08-11 — 背侧单极 MN 完整补全 (11/11)
- **腹索运动神经元 69/69 全部完整**: DA(9)+VA(12)+DB(7)+VB(11)+DD(6)+VD(13)+AS(11)
- **Tolstenkov 2018 eLife**: AS 双模式活动 + 背侧偏转 + UNC-7 电反馈
- **regtest**: 20/20 PASS (210/455/126)

### Step 89: VNC 运动回路互连完善 ✅ (2026-02-12)
> 详细文档: [steps/step89_vnc_interconnect.md](steps/step89_vnc_interconnect.md)

- **AS→VD 背侧偏转**: 19条兴奋性突触 (Tolstenkov 2018 eLife)
- **AS↔AVA 电反馈**: 11条间隙连接 (UNC-7 innexin)
- **DB↔DB 本体感觉波**: 6条相邻间隙连接 (对称 VB↔VB, Wen 2012)
- **regtest**: 20/20 PASS (210/474/143)

### Step 90: A-class 后退运动回路互连完善 ✅ (2026-02-12)
> 详细文档: [steps/step90_aclass_interconnect.md](steps/step90_aclass_interconnect.md)

- **VA↔VA + DA↔DA 后退波**: 19对相邻间隙连接 (Gao 2018 eLife)
- **DA↔AS 背侧同步**: 9条间隙连接 (White 1986)
- **VA→DD 交叉抑制**: 12条化学突触
- **regtest**: 20/20 PASS (210/486/171)

### Step 91: B-class + DA 交叉抑制通路补全 ✅ (2026-02-12)
> 详细文档: [steps/step91_cross_inhibition.md](steps/step91_cross_inhibition.md)

- **VB→VD + DB→DD + DA→VD**: 27条化学突触 (White 1986)
- **四条交叉抑制通路全部完成**: VB→VD + DB→DD + VA→DD + DA→VD
- **regtest**: 20/20 PASS (210/513/171)

### Step 92: SimulationEngine 模块化拆分 ✅ (2026-02-12)
> 详细文档: [steps/step92_engine_modularization.md](steps/step92_engine_modularization.md)

- **simulation_engine.cpp 从 2012→693 行**: 拆出 3 个新编译单元
- **apply_sensory_systems.cpp**: 感觉转导 5 函数 (化学/温度/触觉/O₂/CO₂/光/信息素/产卵/食物边缘)
- **apply_motor_control.cpp**: 运动控制 6 函数 (weathervane/RIA-SMD/SMB/本体感觉/RIV omega/head tonic)
- **setup_gpu_stp.cpp**: STP 参数 + GPU 后端 3 函数
- 纯工程重构，零逻辑变更，编译零错误

### Step 93: 行为调优 — Omega比例/病原体回避/偏置钳位 ✅ (2026-02-13)
> 详细文档: [steps/step93_behavior_tuning.md](steps/step93_behavior_tuning.md)

- **omega/reversal 96%→57%**: as_factor 1.7→2.8 (DD/VD交叉抑制压低dorsal tone后补偿)
- **病原体回避生效**: CI从+0.35→-0.01 (sickness=1时)
  - klinokinesis极性翻转: 食物梯度强→高pirouette→逃离 (Zhang 2005, Ha 2010)
  - MOD-1抑制增强: AIY -12→-20pA, AIZ -6→-10pA
- **bias_clamp 12→30pA**: 消除weathervane饱和
- **AS01 seg 2-6→4-8**: 避免head区域背侧偏置
- **regtest**: 20/20 PASS

### Step 94: 后退波传播修复 — A-class 本体感觉方向反转 ✅ (2026-02-13)
> 详细文档: [steps/step94_backward_wave.md](steps/step94_backward_wave.md)

- **A-class 本体感觉方向修正**: DA/VA 从感知前方→感知后方 (tail→head 波)
- **VB08-11 前向本体感觉补全**: Step 87 遗漏的映射
- **DA06-09/VA06-12 完整补全**: 从 5→9/12 个完整 A-class 映射
- **REF**: Kawano 2011, Wen 2012, Gao 2018 — 后退波反向传播
- **regtest**: 20/20 PASS

### Step 95: Roaming/Dwelling 行为状态涌现 ✅ (2026-02-13)
> 详细文档: [steps/step95_roaming_dwelling.md](steps/step95_roaming_dwelling.md)

- **双稳态涌现**: 5-HT/PDF 竞争产生 Roaming↔Dwelling 状态切换
- **Dwelling**: 90% awake time, speed=0.18 mm/s, 5-HT=0.56
- **Roaming**: 10% awake time, speed=0.22 mm/s, 5-HT=0.28
- **7 R↔D transitions** in 300s, speed ratio=1.2x
- **5-HT SER-4 SPEED_SCALE**: -0.40→-0.60 (Flavell 2013: dwelling ~50% roaming speed)
- **REF**: Flavell 2013 Cell, Ben Arous 2009, Dag & Flavell 2023
- **regtest**: 20/20 PASS

### Step 96: 社交/独居进食行为 — NPR-1/RMG Hub ✅ (2026-02-13)
> 详细文档: [steps/step96_social_solitary.md](steps/step96_social_solitary.md)

- **RMG hub-and-spoke 网络**: 完整 7 类 spoke (+14 gj, +4 IL2 neurons)
  - URX/ASK/ADL/ASH/AWB/IL2/AUA ↔ RMG (Macosko 2009 Fig 3a)
- **NPR-1 直接抑制 RMG**: -20pA (N2 独居), 0pA (Hawaiian 社交)
- **Bug fix**: NPR-1 add_synaptic_current 在 I_syn_ reset 之前 → 被清零 → 移到 reset 后
- **N2 vs Hawaiian**: RMG S = 0.007 vs 0.23 (33x 差异) ✅
- **CLI**: --npr1 参数切换菌株表型
- **REF**: Macosko 2009 Nature, de Bono 2002, Hall & Bhatt 2017 Dev Neurobiol
- **regtest**: 20/20 PASS (214 neurons, 185 gj)

### Step 97: O₂ 偏好 / 空间分布诊断 ✅ (2026-02-13)
> 详细文档: [steps/step97_o2_preference.md](steps/step97_o2_preference.md)

- **O₂ 空间分布诊断**: Section 35, 分 3 区域 (center/border/open)
- **N2 vs Hawaiian 对比** (seed=42): Hawaiian open field 30% vs N2 8% (3.75x)
- **URX 活性差异**: Hawaiian S=0.086 vs N2 S=0.055 (+56%)
- **注意**: 高斯食物模型中 OAI 被趋化性混淆，raw zone percentages 更可靠
- **REF**: Chang 2006 PLoS Biology, Gray 2004 Nature, Cheung 2005
- **regtest**: 20/20 PASS

### Step 98: 觅食策略 — Area-Restricted Search (ARS) ✅ (2026-02-13)
> 详细文档: [steps/step98_ars_foraging.md](steps/step98_ars_foraging.md)

- **food_memory tau_decay**: 90s → 300s (匹配 Hills 2004 生物学 5min 时间尺度)
- **food_memory→AVA**: 1.5 → 4.0 pA (增强 local search reversal 驱动)
- **--food-removal CLI**: 指定时间清除食物，测试 ARS 过渡
- **5-seed 群体验证**: t+90s 后 reversal rate 0.19/s (无食物基线 0.11/s, +73%)
- **REF**: Hills 2004 J Neurosci, López-Cruz 2019 Neuron, Margolis 2023 eLife
- **regtest**: 20/20 PASS

### Step 99: 多种子并行运行能力 ✅ (2026-02-13)
> 详细文档: [steps/step99_parallel_multiseed.md](steps/step99_parallel_multiseed.md)

- **`--seeds N -j M`**: N 个种子 M 线程并行，自动聚合 mean±std
- **两种模式**: `--fitness --seeds N` (3 scenarios/seed) 或 `--seeds N` (单 scenario 聚合)
- **性能**: 8 seeds/8 jobs/300s → 74.2s wall time (~8x 加速)
- **默认并行上限**: min(8, hardware_concurrency)
- **regtest**: 20/20 PASS

### Step 100: 系统状态刷新 + FLP-11/DOP-3 覆盖修复 ✅ (2026-02-13)
> 详细文档: [steps/step100_system_status_refresh.md](steps/step100_system_status_refresh.md)

- **Bug 1**: FLP-11 body_mn[] 移除 DD/VD(GABAergic) + 添加 DA06-09/VA06-12/VB08-11(胆碱能) — Rossi 2025 Current Biology
- **Bug 2**: DOP-3 b_class_names[] 添加 VB08-11 (14→18 B-class MN) — Chase 2004 Nat Neurosci
- **注释**: 6→7 neuromodulators
- **文档**: 「当前系统状态」全面刷新 (突触 215→513, 映射 29→75, 文件 44→62, 感觉 61→63, 运动 98→96 等)

### Step 101: 温度趋性学习 — 食物-温度联合记忆 ✅ (2026-02-13)
> 详细文档: [steps/step101_thermotactic_learning.md](steps/step101_thermotactic_learning.md)

- **Bug fix**: 温度 weathervane 使用 learned_tc() 替代固定 cultivation_temp_ — Tc 学习终于影响导航方向
- **AWC 饥饿温度响应**: 饥饿时 AWC 对 |T-Tc| 敏感 → AWC⊣AIA → 温度趋性中断 (Hawk 2021 eLife)
- **零新增神经元/突触**: 纯行为涌现，利用已有 AWC→AIA 抑制连接
- **行为**: 喂食时趋向学习后 Tc；饥饿时温度趋性中断（AWC-AIA 功能重配置）

---

## 当前系统状态

```
架构: 8 层 (环境/躯体/感知/神经元/连接组/神经调质/运动/行为)
神经元: 214 个 MVP 子集 (302 全集待扩展)
  感觉: 63 (ASE/AWC/AWA/ASH/ALM/PLM/NSM/ADE/PDE/AFD/ADF/ASJ/ASK/ASI/ADL/FLP/PHB/PHA/URX/BAG/PVD L/R + CEP 4×(DL/DR/VL/VR) + OLQ 4× + IL1 4× + IL2 4× + AVM + AQR + PQR)
  中间: 55 (AIA/AIB/AIY/AIZ/RIA/RIB/RIM/RIC/AVA/AVB/AVD/AVE/PVC/AVF/AUA/AVK/AVJ/AVH/PVP/AIN/LUA/I1/RIP L/R + RIS + RIH + RMG L/R + DVA + DVC + PVT + PVR + RIG)
  运动: 96 (SMD 4 + RMD 4 + SMB 4 + RIV 2 + RMED/RMEV 2 + AS01-11(11) + DA01-09(9) + DB01-07(7) + VA01-12(12) + VB01-11(11) + DD01-06(6) + VD01-13(13) + MC 2 + M3 2 + M4 + HSN 2 + VC4/VC5 + AVL + DVB)
突触: 513 化学 + 185 间隙连接 (全部带 Tsodyks-Markram STP, 支持分数 sections)
  Step 42: Cook 2019 校准 (+8 RIA↔RIV, -2 AVE→RIV) + RIV↔RIV gap
  Step 84-91: VNC MN 完整互连 (交叉抑制/本体感觉波/后退波 全部完成)
神经调质: 7 种 (5-HT, DA, OA, TA, NLP-12, PDF, FLP-11) — volume transmission + 饱食度(泵驱动)
  5-HT 源: NSM(食物) + HSN(产卵) — 4个源神经元 (Step 43: ADF 移除)
  5-HT 靶标 (20个, 5种受体): MOD-1→AIY/AIB/AIZ/PVC(抑制) + SER-4→RIC(抑制)+speed(-0.60)+reversal(-0.50) + SER-1→RIA/RIC(兴奋) + SER-5→ASH(增敏) + LGC-50→RIA(SYNAPSE_GAIN)
  DA 源: CEP(4)+ADE(2)+PDE(2) = 8个 (完整), 27个靶标: DOP-3→DB01-07/VB01-11(-3pA)/AVA(-3)/AVB(-2) + DOP-1→DVA(+4)/RIA(+2) + DOP-2→CEP(-3, 自受体)
  TA 源: RIM (逃逸协调) — LGC-55→SMD/AVB/RIV抑制 + TYRA-3→ASH增敏 + SER-2→AIY抑制
  NLP-12 源: DVA (本体感觉) — CKR-1→SMD(+5pA, 头摆ARS) + CKR-2→AVA(+2pA) + DA→DOP-1→DVA(+4pA)
  FLP-11 源: RIS (睡眠) — DMSR-1→AVA/AVB(-20)/MC(-18)/head_MN(-28)/body_MN(-42, 胆碱能only)/SPEED(-0.95) + FRPR-8→RIS(-8, 自抑制)
离子通道: 14 种 (EGL-19/UNC-2/CCA-1/SHL-1/KQT-3/SLO-1/NCA/MEC + EGL-36/IRK/TWK/SLO-2/OSM-9/EXP-2)
神经元模型: 单隔室 HH 分级电位 (L2) + 多隔室 (RIA) + 钙动力学
身体: 2D 弹性杆 48 段, 75 个运动神经元-肌肉映射, 体节间曲率扩散(弹性耦合)
环境: 50×50 mm, 4化学场(food_odor+soluble+repellent+pheromone) + 线性温度梯度 (0.5°C/mm) + O₂场(food派生) + 光场(高斯σ=8mm)
内部状态: satiety_(泵驱动), sickness_(有毒食物), food_memory_(双通路ARS), fatigue_(睡眠驱动)
学习: 盐学习(ASER w_mod) + 病原体学习(AWC翻转+WV反向+厌食) + 温度学习(Tc适应+AWC饥饿中断) + STP习惯化 + 睡眠巩固(Step 62) + INS-1厌食(Step 63)
仿真: dt=0.5ms, CPU 实时 (10000步 < 1s)
性能: cache_neuron_ids_and_synapses() 一次性缓存 10 ID + 6 typed 指针 + 3 组突触索引
计算: CPU (默认) + OpenCL GPU 后端 (>500突触自动启用, AMD RX 6950 XT 就绪)
构建: CMake + MSVC 19.44 + C++20 + vcpkg (OpenCL/ImGui/ImPlot/GLFW)
工具: CLI 运行时参数覆盖 (--as_factor/--pulse_amp/--duration/--seed/--light/--seeds N -j M 等)
      --fitness 模式: 4 seeds × 3 scenarios 自动评估, 输出标量 fitness score
      --seeds N -j M: N 种子 M 线程并行, 自动聚合 mean±std (Step 99)
可视化: Dear ImGui + ImPlot + GLFW, 3列布局, 实时调参+信号链诊断
P0/P1 违规全部修复:
  P0-1.1: Pirouette Poisson 移除 → reversal 从 AVA 涌现 (Step 66)
  P0-1.2: curvature_bias 旁路移除 → weathervane 从 SMD 涌现 (Step 65)
  P1-1.3: food edge 概率公式移除 → 从 AVA-AVB 平衡涌现 (Step 70)
  P1-1.4: basal_slow 直接乘法移除 → DA→DOP-3→B-class MN 涌现 (Step 68)
  P1-1.5: set_locomotion_state 覆盖移除 → 完全神经回路驱动 (Step 66)
  P0-5: DMP speed_factor 移除 → AVL/DVB GABA→B-class MN 涌现减速 (Step 71)
  P0-6: FLP-11 直接注入移除 → NeuromodulationManager DMSR-1 框架 (Step 71)
行为指标 (300s): CI≈0.44 (naive), CI≈-0.01 (sickness=1, 病原体回避生效), omega/reversal≈57%, reversal_rate≈0.14/s, speed≈0.15mm/s
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

核心回路 (默认连接组, 513 化学突触 + 185 间隙连接):
  趋化性: ASE/AWC/AWA → AIA/AIB/AIY/AIZ → RIA → SMD (头部转向)
  关键: AIA AND-gate (Kakaria 2019): AWA→AIA(gj兴奋) + AWC/ASE⊣AIA(Cl⁻去抑制)
  关键: AIA ⊣ AIB (抑制性, Chalasani 2007), AIY → AVB (Gray 2005)
  ASE→AIB 直接 klinokinesis: ASER→AIB(GLR-1兴奋) + ASEL⊣AIB(GLC-3抑制) (Kuramochi 2018)
  触觉: ALM → AVD (前触) / PLM → AVA (后触)
  前进: 感觉→AIY→AVB→DB/VB → 背/腹侧体壁肌肉
  后退: AWC→AIB→AVA → DA/VA → 背/腹侧体壁肌肉
  Omega: RIA→RIV(兴奋) + RIV→RIA(抑制) 负反馈环路, TA门控 post-inhibitory rebound
  交叉抑制: DD ↔ VD (背腹交替), SMD dorsal↔ventral (头部半中心)
  VNC 完整互连: VB→VD + DB→DD + VA→DD + DA→VD (4条交叉抑制通路)
  本体感觉波: B类 DB↔DB/VB↔VB + A类 VA↔VA/DA↔DA + DA↔AS (相邻间隙连接)
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
  Basal slowing: DA→DOP-3(-3pA)→18 B-class MN 涌现减速 (Step 68+100, 移除直接乘法)
  速度: ~0.20 mm/s (文献值 ~0.15-0.2 mm/s)

文件结构:
  src/core/            — 7 文件 (types/config/fast_math/logger .h/.cpp)
  src/neuron/          — 10 文件 (ion_channel/calcium/single_compartment/multi_compartment/factory .h/.cpp)
  src/connectome/      — 10 文件 (chemical_synapse/gap_junction/connectome/connectome_builder/connectome_loader .h/.cpp)
  src/body/            — 4 文件 (body_model/muscle_system .h/.cpp)
  src/motor/           — 2 文件 (motor_controller .h/.cpp)
  src/environment/     — 5 文件 (environment/chemical_field .h/.cpp + sensory_transducer.h)
  src/pharynx/         — 1 文件 (pharyngeal_pump.h)
  src/neuromodulation/ — 2 文件 (neuromodulation .h/.cpp)
  src/compute/         — 5 文件 (compute_backend.h + cpu_backend.h + opencl_backend .h/.cpp + kernels.cl)
  src/simulation/      — 12 文件 (simulation_engine .h/.cpp + 7 拆分cpp + main.cpp + diag_main.cpp + regression_test.cpp)
  src/visualization/   — 3 文件 (vis_app .h/.cpp + vis_main.cpp)
  docs/                — blueprint.md + progress.md + steps/ + tools/
  总计: 62 文件 (CMakeLists.txt + 60 .h/.cpp + kernels.cl + 文档)

参考项目对标:
  OpenWorm: Sibernetic (SPH 物理) + c302 (NeuroML 神经元) + Geppetto (可视化)
  BAAIWorm: 多隔室 HH + 3D 物理 + 全脑钙成像拟合 (Nature Comput Sci 2024)
  本项目: 单隔室 HH (MVP) → 多隔室 (Phase 3) + 弹性杆 (MVP) → SPH (Phase 4)
```

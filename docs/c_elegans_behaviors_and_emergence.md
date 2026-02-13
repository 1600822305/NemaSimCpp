# 秀丽隐杆线虫 (C. elegans) 302 神经元的完整行为与涌现现象清单

> 基于多篇学术综述和数据库的网络研究汇总  
> 主要参考: WormBook, WormAtlas, Flavell Lab (MIT) Behavioral States Review,  
> Yemini et al. 2013 (行为表型数据库), de Bono & Maricq 2005, Hobert 2003 等

---

## 一、神经系统基本事实

| 指标 | 数值 |
|------|------|
| 雌雄同体成体神经元总数 | **302** |
| 雄性成体神经元总数 | **385** (额外83个用于交配) |
| 神经元类别 (形态学) | **118 类** |
| 化学突触数 | ~**6,400** |
| 间隙连接 (电突触) 数 | ~**900** |
| 咽部独立神经系统神经元 | **20** |
| 运动神经元 | **113** |
| 感觉神经元 | ~**60** |
| 中间神经元 | ~**70+** |

---

## 二、完整行为清单 (约 40+ 种独立行为)

### A. 基础运动行为 (Locomotion)

| # | 行为 | 描述 | 关键神经元/回路 |
|---|------|------|----------------|
| 1 | **前进爬行 (Forward crawling)** | 体壁肌肉产生后→前传播的正弦波，在琼脂表面蠕动前进 | AVB, PVC → B-class MN → 体壁肌 |
| 2 | **后退 (Reversal)** | 前→后传播波，反向运动；通常持续1-5秒 | AVA, AVD, AVE → A-class MN |
| 3 | **游泳 (Swimming)** | 在液体中频率更高（~2Hz vs 爬行~0.3Hz）、振幅更小的运动 | 同一回路，不同参数化 |
| 4 | **Ω转弯 (Omega turn)** | 身体弯曲成Ω形的深度转弯，用于急剧改变方向 | RIV, SMD; 由AVA/AVE触发 |
| 5 | **浅转弯 (Shallow turn)** | 运动方向的微小调整 | SMD, RIV |
| 6 | **蜷缩 (Coiling)** | 身体紧密卷曲接触自身 | 深度Ω弯的极端形式 |
| 7 | **停顿 (Quiescence/Pause)** | 短暂停止运动 | RIS (诱导静止) |
| 8 | **头部摇摆 (Head foraging/oscillation)** | 头部独立于身体的侧向摆动，用于探测环境 | RME, SMD, RMD |

### B. 趋化性与化学感觉行为 (Chemosensory)

| # | 行为 | 描述 | 关键神经元 |
|---|------|------|-----------|
| 9 | **趋化性 (Chemotaxis to attractants)** | 朝向食物来源（细菌代谢物、NaCl等）的定向运动 | AWC, AWA, ASE → AIY, AIZ, AIA |
| 10 | **化学回避 (Chemical avoidance)** | 逃离有害化学物质（如铜离子、SDS、奎宁、D-色氨酸） | ASH, ADL, AWB → AVA (后退) |
| 11 | **渗透压回避 (Osmotic avoidance)** | 对高渗透压溶液的急性回避反应 | ASH (多模态伤害感受器) |
| 12 | **趋嗅性 (Olfaction/Olfactory chemotaxis)** | 对挥发性吸引物（如二乙酰、苯并噻唑、丁酮）的趋向 | AWC (ON/OFF亚型), AWA |
| 13 | **嗅觉回避 (Olfactory avoidance)** | 对挥发性厌恶气味（如苯甲醛高浓度、辛醇等）的逃避 | AWB, ASH |
| 14 | **气体趋向 (Aerotaxis/O₂ preference)** | 偏好特定O₂浓度（通常5-12%），回避高浓度O₂ | URX, AQR, PQR (高O₂); BAG (低O₂) |
| 15 | **CO₂回避 (CO₂ avoidance)** | 对CO₂的急性回避 | BAG, AFD |
| 16 | **趋盐性 / 盐趋化 (Salt chemotaxis)** | 对NaCl等盐类的趋向（可通过经验反转） | ASE(L/R 不对称功能) |
| 17 | **食物边缘徘徊 (Food-edge dwelling)** | 在食物边界来回巡逻 | 多感觉整合; ASE, AWC |
| 18 | **信息素感知 (Pheromone sensing)** | 对ascaroside信息素的响应（调节dauer、社交、交配） | ASI, ASK, ADL, ASJ |

### C. 温度感觉行为 (Thermosensory)

| # | 行为 | 描述 | 关键神经元 |
|---|------|------|-----------|
| 19 | **趋温性 (Thermotaxis)** | 朝向培养温度的定向运动 | AFD → AIY/AIZ |
| 20 | **等温线追踪 (Isothermal tracking)** | 沿着与培养温度匹配的等温线精确移动 | AFD, AIY |
| 21 | **温度回避 (Negative thermotaxis)** | 远离有害温度 | AFD, FLP |

### D. 机械感觉行为 (Mechanosensory)

| # | 行为 | 描述 | 关键神经元 |
|---|------|------|-----------|
| 22 | **轻触回避（前触）** | 前方轻触引发后退 | ALM, AVM → AVD, AVA |
| 23 | **轻触回避（后触）** | 后方轻触引发加速前进 | PLM → PVC, AVB |
| 24 | **鼻触反射 (Nose touch reflex)** | 鼻尖碰到障碍物引发快速后退 | ASH, OLQ, FLP |
| 25 | **刺触回避 (Harsh touch)** | 对较强机械刺激的反应（由不同感觉神经元介导） | PVD (尾部), FLP (头部) |
| 26 | **本体感觉 (Proprioception)** | 感知自身身体弯曲以协调运动波传播 | B-class MN, DVA; TRPN通道 |

### E. 光感觉行为 (Photosensory)

| # | 行为 | 描述 | 关键神经元 |
|---|------|------|-----------|
| 27 | **光回避 (Phototaxis/Light avoidance)** | 对紫外光和短波蓝光的急性回避 | ASJ, ASK 通过 LITE-1 受体 |

### F. 摄食与消化行为 (Feeding & Digestion)

| # | 行为 | 描述 | 关键神经元 |
|---|------|------|-----------|
| 28 | **咽泵 (Pharyngeal pumping)** | 节律性咽部肌肉收缩吸入细菌（约4Hz有食物时） | MC, M3, M4 (咽部神经系统) |
| 29 | **排便运动程序 (Defecation Motor Program, DMP)** | ~45秒周期的三步排便：pBoc→aBoc→Exp | DVB, AVL; 由肠道Ca²⁺振荡器驱动 |
| 30 | **基础减速反应 (Basal slowing)** | 在食物上减速（相对无食物区域） | 多巴胺神经元: CEP, ADE, PDE |
| 31 | **增强减速反应 (Enhanced slowing)** | 饥饿后重新遇到食物时更显著减速 | 5-HT + 多巴胺系统 |

### G. 觅食行为策略 (Foraging Strategies)

| # | 行为 | 描述 | 关键神经元/调控 |
|---|------|------|----------------|
| 32 | **漫游状态 (Roaming)** | 快速移动、少转弯的探索性运动 | 5-HT, PDF-1 |
| 33 | **停留状态 (Dwelling)** | 缓慢移动、频繁转弯的局部搜索 | 低5-HT, 神经肽 |
| 34 | **局部搜索 (Local search)** | 食物移除后短时间内的高频转弯模式 | AIB, RIM |
| 35 | **全局搜索 (Global/Dispersal search)** | 食物移除较长时间后的直线运动为主模式 | 状态转换，AIY活性增加 |
| 36 | **离食搜索 (Off-food area-restricted search)** | 根据先前食物经验调节搜索范围 | 多神经肽调控 |

### H. 产卵行为 (Egg-laying)

| # | 行为 | 描述 | 关键神经元 |
|---|------|------|-----------|
| 37 | **产卵 (Egg-laying)** | 通过外阴肌肉收缩排出胚胎，具有活跃/静息两种状态 | HSN, VC; 5-HT调控 |

### I. 交配行为 (Male Mating) — 仅限雄性

| # | 行为 | 描述 | 关键神经元 |
|---|------|------|-----------|
| 38 | **交配伙伴定位 (Mate finding/location)** | 雄虫主动搜索雌雄同体个体 | 信息素感知; CEM, 特有雄性神经元 |
| 39 | **接触后退行追踪 (Contact & backing)** | 用尾部沿伴侣身体滑行定位外阴 | 尾部感觉射线 (rays) |
| 40 | **外阴定位与转弯 (Vulva location & turning)** | 到达外阴位置后精确定位 | hook神经元, PCA, PCB |
| 41 | **交接刺插入 (Spicule insertion)** | 将交接刺插入对方外阴进行授精 | SPC, SPV 运动神经元 |
| 42 | **射精 (Sperm transfer)** | 精子传输 | 雄性特有回路 |

### J. 发育与生存策略

| # | 行为 | 描述 | 关键调控 |
|---|------|------|---------|
| 43 | **Dauer形成 (Dauer entry)** | 不利环境下进入耐受性幼虫阶段 | Dauer信息素; DAF-2/DAF-16通路 |
| 44 | **Nictation (直立摇摆)** | Dauer幼虫在突起物上直立摇摆以附着传播宿主 | Dauer特异行为 |
| 45 | **睡眠/静息 (Lethargus/Sleep)** | 每次蜕皮前的静息期，具有睡眠特征 | RIS; EGF/LIN-3, ALA |
| 46 | **应激诱导睡眠 (Stress-induced quiescence)** | 热休克、UV辐射、细菌毒素后的睡眠 | ALA → FLP-13 神经肽 |

### K. 社交行为 (Social Behaviors)

| # | 行为 | 描述 | 关键调控 |
|---|------|------|---------|
| 47 | **群聚/社交觅食 (Social feeding/aggregation)** | N2野生型为"独居型"，某些品系为"群聚型" | NPR-1 受体; RMG hub |
| 48 | **bordering (边界聚集)** | 在食物边缘聚集 | O₂感知; URX, NPR-1 |

---

## 三、学习与记忆行为 (Learning & Memory)

| # | 行为 | 描述 | 类型 |
|---|------|------|------|
| 49 | **习惯化 (Habituation)** | 对重复无害刺激响应减弱（如反复触碰） | 非联想学习 |
| 50 | **去习惯化 (Dishabituation)** | 新刺激恢复已习惯化的反应 | 非联想学习 |
| 51 | **敏感化 (Sensitization)** | 强刺激后对后续刺激反应增强 | 非联想学习 |
| 52 | **趋盐性学习 (Salt learning/Gustatory plasticity)** | 在无食物条件下暴露于盐后，趋盐性反转为避盐 | ASE → 可塑性 |
| 53 | **病原体回避学习 (Pathogen avoidance learning)** | 经历致病菌后学会回避该菌 | AWB, AWC → 5-HT调控 |
| 54 | **热适应 (Thermotactic learning)** | 记住培养温度并追踪该温度 | AFD → AIY |
| 55 | **联想性条件反射 (Associative conditioning)** | 将中性刺激与正/负强化配对后改变偏好 | 多回路; 长达数小时 |
| 56 | **嗅觉适应 (Olfactory adaptation)** | 长时间暴露于气味后反应下降 | AWC; 需要特定激酶 |
| 57 | **食物经验依赖嗅觉可塑性** | 食物经验改变对特定气味的趋向性 | AIA, AIY |
| 58 | **伤害性启动 (Nociceptive priming)** | 组织损伤后痛觉通路敏感化 | ASH → 谷氨酸信号 |

---

## 四、涌现现象 (Emergent Properties)

以下是从302个简单神经元的连接和相互作用中"涌现"出的复杂行为和系统特性——这些现象无法从单个神经元的属性直接预测，而是网络级别的集体性质：

### 1. 全脑动态状态编码 (Brain-wide dynamical states)

**来源**: Kato et al. 2015 (Cell); Flavell Lab

- 全脑成像显示，大多数活跃神经元参与**协调的低维网络动态**
- 仅用**3-8个主成分**即可解释大部分神经活动方差
- 神经群体在低维流形上展示**环形吸引子（cyclic attractor）**动态
- **运动命令序列**嵌入在这些全局脑动态中——前进→停止→后退→转弯的序列是涌现的

### 2. 行为状态切换 (Behavioral state switching)

**来源**: Flavell et al. 2013, Behavioral States Review (Genetics 2020)

- **漫游-停留二态性 (Roaming-Dwelling bistability)**：线虫在两种离散行为状态间切换，每个状态持续数分钟
- 这不是简单的感觉→运动映射，而是**内源性状态动态**
- 5-HT和PDF神经肽创造了**正反馈回路**，使状态稳定
- 涌现出的"**决策**"现象——个体线虫在相同环境中展示不同选择

### 3. 时钟样钙振荡器 (Ultradian Ca²⁺ oscillator)

**来源**: DMP排便文献; dal Santo et al.

- 排便行为由**肠道细胞中的Ca²⁺振荡器**驱动，周期~45秒
- 这是一个非神经的**自主生物钟**，展现出钟表级别的精确性（标准差<3秒）
- 神经系统对这个时钟进行调制但不是其来源——涌现自肠道上皮细胞的细胞自主属性

### 4. 导航策略 (Navigation strategies)

**来源**: Chemotaxis文献; Luo et al. 2014 (Neuron)

- **偏向随机游走 (Biased random walk / Klinokinesis)**：通过调节转弯概率实现趋化——非直接趋向
- **风向标机制 (Weathervane / Klinotaxis)**：连续调整运动方向
- 这两种策略的**组合和动态切换**是涌现的——单个感觉神经元编码**感知、记忆和运动决策**

### 5. 趋化性中的感觉编码复杂性

**来源**: Luo et al. 2014; Chalasani et al. 2007

- 单个AWC神经元同时编码：
  - **即时浓度变化** (感知)
  - **适应历史** (短期记忆)
  - **运动状态上下文** (自我感知)
- 这种多重编码是从神经元的离子通道和突触可塑性中**涌现**的

### 6. 左右不对称的感觉编码

**来源**: Hobert Lab; ASE不对称

- ASEL和ASER虽然形态相似，但功能完全不对称：
  - ASEL → 对盐浓度**上升**响应 (ON cell)
  - ASER → 对盐浓度**下降**响应 (OFF cell)
- AWC也有ON/OFF不对称
- 这种不对称性**扩展了编码容量**，从302个神经元中挤出更多信息处理能力

### 7. 跨模态感觉整合 (Multisensory integration)

- 线虫整合化学、温度、机械、光等多种模态信息做出统一行动决策
- **AIA作为"与门" (AND-gate)**：需要来自ASE和AIB的同时输入才激活
- 多模态整合是从简单的突触连接规则中涌现的复杂计算

### 8. 内源性行为变异性 (Behavioral individuality)

**来源**: Stern et al., Harel et al. 2024 (Cell Reports)

- 基因型完全相同的个体展现出**持久的行为差异**
- 这些差异可以持续数天——不是简单噪声
- 暗示存在**表观遗传或发育噪声驱动的稳定内部状态**
- 这是从确定性连接组中涌现的**个性 (individuality)**

### 9. 群体涌现行为 (Collective emergent behavior)

**来源**: Antonic & Vellinger 2025 (Frontiers in Neurorobotics)

- 尽管每条线虫独立决策，群体展现出**集体动力学**：
  - **聚集模式** (aggregation patterns)
  - **同步运动** (coordinated movement)
  - **群体觅食效率优化**
- 这些群体行为是从个体间信息素信号中**涌现**的

### 10. 记忆巩固与睡眠

- 睡眠/静息期间发生**突触重塑**
- Dauer恢复后保留在dauer前获得的记忆
- 简单神经系统中涌现出的**离线记忆处理**

### 11. 上下文依赖的行为调制

- 相同的感觉输入在不同内部状态下产生不同行为输出：
  - **饱食 vs 饥饿**改变趋化偏好
  - **食物存在 vs 缺失**改变运动参数
  - **社交 vs 独居状态**改变O₂偏好
- 这种**上下文相关的计算**涌现自神经调质（5-HT、多巴胺、章鱼胺、酪胺、神经肽）对回路的调制

### 12. 运动波的本体感觉协调

- 身体弯曲波的前后传播不完全依赖中枢模式发生器
- **局部本体感觉反馈**在相邻体节间创造协调——涌现出流畅的正弦运动
- 这是一种**分布式计算**，类似于"涌现"的模式形成

---

## 五、统计总结

| 类别 | 行为数量 |
|------|---------|
| 基础运动 | 8 |
| 化学感觉 | 10 |
| 温度感觉 | 3 |
| 机械感觉 | 5 |
| 光感觉 | 1 |
| 摄食与消化 | 4 |
| 觅食策略 | 5 |
| 产卵 | 1 |
| 交配 (雄性) | 5 |
| 发育/生存策略 | 4 |
| 社交行为 | 2 |
| 学习与记忆 | 10 |
| **行为总计** | **~58 种** |
| **涌现现象** | **12+ 类** |

> **注意**: 行为的计数取决于粒度。如果将每种化学物质的趋化性算作独立行为，仅嗅觉就响应**数十种**不同化合物。如果将行为定义为离散的运动模式，Yemini et al. 2013 的行为数据库提取了 **702 种定量测量特征**。上表采用了文献中通常讨论的独立行为类别。

---

## 六、关键参考文献

1. **White et al. (1986)** - "The structure of the nervous system of C. elegans" - 原始连接组
2. **Cook et al. (2019)** - 两性完整连接组 (Nature)
3. **de Bono & Maricq (2005)** - "Neuronal substrates of complex behaviors in C. elegans" (Ann Rev Neurosci)
4. **Yemini et al. (2013)** - "A database of C. elegans behavioral phenotypes" (Nature Methods)
5. **Flavell et al. (2020)** - "Behavioral States" (Genetics) - 行为状态综述
6. **Kato et al. (2015)** - "Global Brain Dynamics Embed the Motor Command Sequence" (Cell)
7. **Bargmann (2006)** - "Chemosensation in C. elegans" (WormBook)
8. **Chalfie et al. (1985)** - 机械感觉突变体
9. **Rankin (2002)** - 习惯化学习
10. **Harel et al. (2024)** - 行为空间发育图谱 (Cell Reports)
11. **Antonic & Vellinger (2025)** - 群体涌现行为 (Frontiers in Neurorobotics)
12. **Luo et al. (2014)** - "Dynamic Encoding of Perception, Memory, and Movement" (Neuron)
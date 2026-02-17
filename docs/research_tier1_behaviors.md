# Tier 1 缺失行为 — 文献调研汇总

> 目标: 为 6 个教科书级缺失行为整理回路、机制、关键参数，指导实现。

---

## 1. 渗透压回避 (Osmotic Avoidance)

### 两种模式

| 模式 | 刺激 | 感觉神经元 | 通道 | 行为 | 时间尺度 |
|------|------|-----------|------|------|----------|
| **急性回避** | 极高渗 ≥1 Osm (4M 甘油) | **ASH** | OSM-9 (TRPV) | 接触后立即反转 | 秒级 |
| **渐进厌恶** | 温和高渗 150→400 mOsm | **AQR/PQR/URX** (体腔) | TAX-2 (cGMP-gated) | 转向率逐渐增加 | 分钟级 |

REF: Colbert 1997 (ASH/OSM-9), Kunitomo 2017 eNeuro (渐进厌恶)

### 回路

**急性模式** (我们优先实现):
```
高渗溶液 → ASH (OSM-9/TRPV) → [AIB + AIY 层1中间神经元] → AVA → 反转
                               └→ RIM (TA释放) → omega
```
- ASH 已有, OSM-9 已有 (用于斥力回避), AIB/AIY/AVA 已有
- **实现: 只需加渗透压场 + ASH 渗透压传导映射**

**渐进模式**:
```
体液渗透压↑ → URX (TAX-2) → [AIB+AIY] → 转向率↑
                AQR/PQR (TAX-2) ─┘
```
- URX/AQR/PQR 已有, TAX-2 可复用 cGMP 通路

### 关键参数
- ASH Ca²⁺ 响应阈值: ~200 mOsm 变化 (Hilliard 2005)
- 急性反转延迟: <1s (Colbert 1997)
- 渐进厌恶: 2-3min 建立 (Kunitomo 2017)
- OSM-9 → ASH 传导增益: 与斥力回避共用同一通道

### 实现难度: ★☆☆☆☆ (最简单)
- ASH + OSM-9 + AIB/AVA 回路全部已有
- 只需: (1) 环境加渗透压场 (2) ASH 加渗透压传导器

---

## 2. CO₂ 回避 (CO₂ Avoidance)

### 回路

```
CO₂ ≥0.5% → BAG (GCY-9 受体鸟苷酸环化酶) → [TAX-2/TAX-4 cGMP 通道] → 反转+转向
                                                                    ↑
              URX (O₂感知) ─── NPR-1 (N2品系) 抑制 ──→ 解除对CO₂回路的抑制
```

REF: Hallem & Sternberg 2008, Bretscher 2008, Carrillo 2013 (J Neurosci)

### 关键发现
- **BAG 是 CO₂ 主感觉神经元**: GCY-9 (受体鸟苷酸环化酶) → cGMP → TAX-2/TAX-4 通道
- **NPR-1 品系依赖**: N2 品系 (npr-1 高活性) → URX 被抑制 → CO₂ 回避正常
  Hawaiian 品系 (npr-1 低活性) → URX 活跃 → **抑制** CO₂ 回路 → 对 CO₂ 不敏感
- **O₂ 调制**: 低 O₂ 环境下 URX 不活跃 → Hawaiian 也能回避 CO₂
- **下游回路未完全鉴定**: BAG 和 URX 的共同下游中间神经元待定

### 我们的情况
- BAG 已有 (用于 O₂ 感知)
- URX 已有, NPR-1 已有 (RMG hub-and-spoke, social_analyzer)
- **需加**: CO₂ 化学场 + BAG 的 CO₂ 传导映射 (GCY-9/TAX-2/TAX-4)

### 关键参数
- CO₂ 回避阈值: ~0.5% (Bretscher 2008)
- BAG Ca²⁺ 响应: CO₂ 上升时激活 (Hallem 2011)
- NPR-1 在 URX 的作用: 降低 URX 活性 → 解除对 CO₂ 回路的抑制
- N2 品系仿真 (我们的默认): NPR-1 活跃 → CO₂ 回避应该正常

### 实现难度: ★★☆☆☆
- BAG/URX/NPR-1 已有
- 需: (1) CO₂ 化学场 (2) BAG CO₂ 传导 (3) BAG→下游连接 (AIB/RIG?)

---

## 3. 鼻触回避 (Nose Touch Avoidance)

### 回路 (Chatzigeorgiou & Schafer 2011, Neuron)

**关键发现: hub-and-spoke 间隙连接网络**

```
鼻触刺激 → OLQ (TRPA-1, 轻触) ─┐
           CEP (TRP-4, 轻触)  ─┤── gap junctions ──→ RIH (hub) ── gap junction ──→ FLP
           ASH (OSM-9, 多模态) ─┘                                                   │
                                                                                      ↓
                                                                          [command interneurons]
                                                                                      ↓
                                                                                   反转
```

### 双层机制
1. **ASH 直接通路**: ASH (OSM-9/TRPV) 直接驱动反转 (独立于网络)
2. **OLQ/CEP→RIH→FLP 侧向促进**: OLQ+CEP 通过 RIH hub 间隙连接促进 FLP 的鼻触响应
   - FLP 本身是高阈值伤害感受器 (粗触: MEC-10/DEG-ENaC)
   - 轻触时 FLP 需要 OLQ/CEP 的电耦合促进才能响应
   - **巧合检测器**: 只有多个输入同时活跃时 FLP 才响应

### 我们的情况
- ASH, OLQ(4x), FLP(L/R), CEP(4x) 全部已有
- RIH 已有
- **需加**: (1) 鼻触刺激检测 (碰壁/碰物体) (2) OLQ/CEP/ASH 机械感觉传导
  (3) OLQ→RIH, CEP→RIH, RIH→FLP 间隙连接 (4) FLP→command 化学突触

### 关键参数
- OLQ: TRPA-1 通道, 轻触阈值 ~8μm 位移
- CEP: TRP-4 (TRPN) 通道, 轻触
- FLP: MEC-10 (DEG/ENaC), 粗触阈值 ~20μm; OSM-9 非自主需求
- ASH: OSM-9 (TRPV), 多模态 (化学+渗透+机械)
- 正常反转率: ~65% 鼻触反转 (wild-type)

### 实现难度: ★★★☆☆
- 神经元全有, 但需要加壁碰检测 + 机械感觉传导 + RIH hub 间隙连接网络

---

## 4. PVD 粗触/伤害感受 (Harsh Body Touch / Nociception)

### 回路 (Way & Chalfie 1989, Albeg 2011, Chatzigeorgiou 2010)

```
粗触 (≥20μm 位移) → PVD (MEC-10/DEG-ENaC + MEC-3 TF) → 逃逸反应:
                                                          - 速度增加
                                                          - 暂停减少
                                                          - 反转减少
                                                          - 产卵抑制
```

### 关键发现
- **PVD + FLP 共同覆盖全身**: PVD 覆盖体部, FLP 覆盖头部 (无重叠, 主动修剪)
- **与轻触区分**: ALM/AVM 轻触 → 简单反转; PVD 粗触 → **逃逸反应** (加速+少暂停)
- **PVD 也是本体感觉器**: 响应体弯曲 (Albeg 2011) — 我们已有 PVD 用于本体感觉
- **PVD 消融表型**: 速度下降 (0.19→0.11 mm/s), 暂停增多, 更多 dwelling
- **产卵抑制**: PVD/FLP 激活 → 通过某机制抑制 HSN → 产卵减少

### 我们的情况
- PVD(L/R) 已有 (用于本体感觉)
- **需加**: (1) 粗触机械刺激模式 (2) PVD 粗触传导 (高阈值) 
  (3) PVD→下游逃逸回路 (PVC? AVA?) (4) 区分本体感觉 vs 粗触

### 关键参数
- 粗触阈值: ≥20μm 位移 (vs 轻触 ~8μm)
- MEC-10/DEG-ENaC 通道 (与 ALM 的 MEC-4 不同)
- PVD 消融后速度: 0.19→0.11 mm/s (Albeg 2011)
- 逃逸反应: 速度↑, 暂停↓, 反转↓

### 实现难度: ★★★☆☆
- PVD 已有, 但需区分本体感觉和粗触输入, 加逃逸回路

---

## 5. 排泄运动程序 (Defecation Motor Program, DMP)

### 机制 (Jiang 2022 Nature Comms, Dal Santo 1999, Teramoto 2005)

**三步运动序列, ~45s 周期:**
1. **pBoc** (posterior body contraction) — 后体肌肉收缩, 由肠道 Ca²⁺ 波直接驱动
2. **aBoc** (anterior body contraction) — 前体肌肉收缩, 由 AVL 驱动 (GABA)
3. **Exp** (expulsion) — 排出, 由 AVL + DVB 同步放电驱动 (GABA → EXP-1 兴奋性 GABA 受体)

### 核心回路

```
肠道 Ca²⁺ 振荡 (IP3R, ~45s 周期)
    │
    ├──→ 后体肌肉直接激活 (pBoc, 质子释放)
    │
    └──→ NLP-40 神经肽释放 → AEX-2/GPCR 在 AVL/DVB
                                │
                                ├──→ AVL 动作电位 (UNC-2 Ca²⁺ + EXP-2 K⁺ 复合尖峰)
                                │         │
                                │         └──→ INX-1 间隙连接 ──→ DVB 动作电位
                                │
                                └──→ AVL+DVB 同步 GABA 释放 → EXP-1 → 肠肌收缩 (Exp)
```

### 关键发现 (Jiang 2022)
- **AVL 和 DVB 是尖峰神经元!** (全或无动作电位, 非分级电位)
- AVL: UNC-2 (CaV2) → Ca²⁺ 正尖峰 + EXP-2 → K⁺ 负尖峰 (复合 AP)
- DVB: 大 Ca²⁺ 尖峰
- INX-1 间隙连接同步 AVL↔DVB 放电
- AVL 负尖峰防止每个周期多次排出 (afterhyperpolarization)

### 我们的情况
- AVL, DVB 已有 (作为运动神经元)
- EXP-2 离子通道已有 (在通道列表中)
- INX-1 间隙连接概念已有
- **缺失**: (1) 肠道 Ca²⁺ 振荡器 (~45s IP3R) (2) NLP-40 神经肽信号
  (3) AVL/DVB 的尖峰动力学 (非分级!) (4) 肠肌收缩输出 (5) pBoc 后体肌肉驱动

### 关键参数
- 周期: 45-50s (temperature-dependent)
- AVL AP: UNC-2 depolarization → ~0mV, EXP-2 negative spike → ~-90mV
- DVB AP: 大 Ca²⁺ 尖峰
- INX-1 gap junction 同步延迟: <100ms
- GABA → EXP-1 (兴奋性, 非抑制性 GABA 受体!)

### 实现难度: ★★★★★ (最难)
- 需要: 肠道 Ca²⁺ 振荡器 (新子系统) + NLP-40 神经肽 + AVL/DVB 尖峰模型 + EXP-1 兴奋性 GABA

---

## 6. 产卵运动程序 (Egg-laying Motor Program)

### 回路 (Kopchock 2021 J Neurosci, Collins 2016 eLife)

```
HSN (5-HT + NLP-3 释放) ──→ VC 增敏 + vm2 阴门肌增敏
                              │
                              ↓
                         VC (ACh 释放) ──→ vm2 阴门肌收缩 ──→ 产卵
                              ↑                    │
                              └── 机械反馈 ─────────┘ (正反馈环)
                              
VC ──(ACh 抑制)──→ HSN (负反馈, GAR-2 mAChR)
VA/VB/VD ──(ACh/GABA)──→ vm1 阴门肌 → 电耦合 → vm2
```

### 双状态模型 (Collins 2016 eLife)
- **安静态** (~20min): HSN 静默, 无产卵
- **活跃态** (~2min): HSN 释放 5-HT → VC 和阴门肌增敏 → 连续产卵 (3-5个)
- 每个活跃期包含多次产卵事件
- 活跃态由 HSN Ca²⁺ burst 驱动

### 关键发现 (Kopchock 2021)
- **VC 是机械激活运动神经元**: 阴门肌收缩 → 机械激活 VC → VC 释放更多 ACh → 正反馈
- **5-HT 增敏是前提**: 无 5-HT 时 VC 激活不足以产卵
- VC→HSN 抑制 (ACh/GAR-2): 产卵后负反馈终止活跃态
- vm1/vm2 阴门肌: vm1 由 VA/VB 运动神经元驱动, vm1→vm2 电耦合

### 我们的情况
- HSN(L/R), VC1-6 已有
- 5-HT 系统已有 (NSM 源 + HSN 源)
- VA/VB 运动神经元已有
- **缺失**: (1) 阴门肌模型 (vm1/vm2) (2) HSN burst 周期 (~20min)
  (3) VC 机械感觉 (4) 产卵事件输出 (5) HSN→VC/vm2 5-HT 增敏

### 关键参数
- 安静态: ~20min, HSN 静默
- 活跃态: ~2min, 3-5 个卵
- HSN 5-HT 释放: SER-1/SER-7 受体 on VC/vm2
- VC ACh: EAT-2 nAChR on vm2 (兴奋), GAR-2 mAChR on HSN (抑制)
- 产卵频率 on food: ~5 eggs/hour

### 实现难度: ★★★★☆
- 需要: 阴门肌模型 + HSN burst 周期 + 5-HT 增敏回路 + 产卵事件

---

## 实现优先级排序

| 排名 | 行为 | 难度 | 新组件需求 | 建议 |
|------|------|------|-----------|------|
| 1 | 渗透压回避 | ★☆ | 渗透压场 + ASH 传导 | 立即可做, ~50行代码 |
| 2 | CO₂ 回避 | ★★ | CO₂ 场 + BAG CO₂ 传导 | 类似渗透压, ~80行 |
| 3 | 鼻触回避 | ★★★ | 壁碰检测 + RIH hub 网络 | 需加间隙连接 + 碰撞物理 |
| 4 | PVD 粗触 | ★★★ | 粗触传导 + 逃逸回路 | PVD 已有, 加传导模式 |
| 5 | 产卵程序 | ★★★★ | 阴门肌 + HSN burst + 5-HT 增敏 | 新子系统 |
| 6 | DMP | ★★★★★ | 肠道振荡器 + AVL AP + NLP-40 | 最复杂, 需新振荡器 |

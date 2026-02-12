# Step 36-38: 全身感觉与产卵

> 本文档为中文档，合并 Step 36-38 的完整一级内容。

---

## Step 36: DVA/PVD — 全身本体感觉

- **DVA**: 单个无配对谷氨酸能中间神经元，TRP-4 TRPN 拉伸受体通道
- **PVD L/R**: 谷氨酸能感觉神经元，树突铺满全身体壁，双模态 (harsh touch + 本体感觉)
- **DVA 转导**: 全身平均|曲率| × 15pA gain → 调制 B 类运动神经元增益
- **PVD 转导**: harsh touch (壁距<1mm, 60pA) + 后体曲率 (8pA gain)
- **连接**: DVA→DB/VB (1 sec), DVA→AVA (0.5 sec), PVD→AVA (2 sec), PVD↔DVA (gap)
- **修复**: PVD harsh touch 阈值 3→1mm, 电流 100→60pA (防止 arena 逃逸)
- **结果**: regtest 17 pass; DVA S=0.327, PVDL S=0.169, omega/reversal 0.94, wave GOOD
- **REF**: Li 2006 Nature, Way & Chalfie 1989, Yeon 2018 PLoS Biology
- **文档**: [step36_proprioception.md](step36_proprioception.md)

---

## Step 37: AVE 后退指令 — reversal 分级 + omega 门控

- **问题**: omega/reversal=100% (AIB→RIV 基线太高, BAG+AWC 提升 AIB)
- **方案**: 删除 AIB→RIV, 改为 AVE→RIV (只有强 reversal 时 AVE 激活才触发 omega)
- **AVE 连接**: AIB→AVE (1 sec, 弱), ASH→AVE (2 sec), AVE→DA (1 sec), AVE→RIV (1 sec)
- **AVA↔AVE gap**: 3 sections (紧密耦合, Kawano 2011)
- **涌现**: 弱刺激→AVA only→短 reversal→不 omega; 强刺激→AVA+AVE→长 reversal→omega
- **结果**: regtest 17 pass; omega/reversal 100%→85%, CI=-0.303, D/V ratio=1.00
- **REF**: Chalfie 1985, Piggott 2011, Kawano 2011, Gray 2005
- **文档**: [step37_ave_omega_grading.md](step37_ave_omega_grading.md)

---

## Step 38: HSN/VC — 产卵系统

- **HSN L/R**: 5-HT 能指令运动神经元，驱动阴门肌肉收缩
- **VC4/VC5**: ACh 能运动神经元，正反馈促进产卵
- **egg_pressure**: 缓慢累积 (tau=120s)，超过 0.7 阈值 → HSN burst → 产卵
- **连接**: PLM⊣HSN (touch 抑制), VC→VB (减速), HSN↔VC (gap), HSN 为 5-HT 源
- **tyramine 反馈**: TA→LGC-55→HSN 超极化 (终止 active state)
- **修复**: wall_dist clamp 防止 arena 外 PVD 饱和
- **结果**: regtest 17 pass; eggs=2, HSNL S=0.210, VC4 S=0.424, 5-HT sources 4→6
- **REF**: Collins 2016 eLife, Waggoner 1998 Neuron, 2021 J Neurosci
- **文档**: [step38_egg_laying.md](step38_egg_laying.md)

---

## 本组总结

| 指标 | 之前 | 之后 |
|------|------|------|
| 本体感觉 | B类MN本体感觉(Step 29) | +**DVA全身** +**PVD双模态** |
| omega门控 | AS背侧(Step 32) | +**AVE强度分级** |
| 行为模式 | — | +**产卵系统** (HSN/VC, egg_pressure) |
| 5-HT源 | NSM×2 | +**HSN×2** (4→6) |

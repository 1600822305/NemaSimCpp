# Step 27-28: 睡眠与多隔室模型

> 本文档为中文档，合并 Step 27-28 的完整一级内容。

---

## Step 27: 睡眠/静止 (Lethargus)

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
- **文档**: [step27_sleep_lethargus.md](step27_sleep_lethargus.md)

---

## Step 28: 多隔室神经元模型 (RIA)

- **生物学**: Hendricks 2012 Nature — RIA轴突nrV/nrD域独立Ca2+编码头部运动
- **新增类**: MultiCompartmentNeuron (Compartment结构体, 轴向耦合, IP3 Ca2+ store release)
- **RIA 3隔室**: soma(感觉谷氨酸) + nrV(SMDVL ACh/GAR-3) + nrD(SMDDL ACh/GAR-3)
- **新增突触**: SMDDL->RIAL nrD(1), SMDDR->RIAR nrD(1), SMDVL->RIAL nrV(1), SMDVR->RIAR nrV(1)
- **klinotaxis**: Ca2+ nrV-nrD差异(DC移除+300ms滤波) -> curvature_bias, 替代Step 19 AC/DC近似
- **参数**: store_release=0.0003, gain=3000, max_bias=0.5, mod_gain=5, axial=0.15nS
- **结果**: regtest 14 pass; heading 16.9 deg/s; CI avg +0.20; sleep 20%
- **REF**: Hendricks 2012 Nature, Ouellette 2018 eNeuro, Iino & Yoshida 2009
- **文档**: [step28_multi_compartment.md](step28_multi_compartment.md)

---

## 本组总结

| 指标 | 之前 | 之后 |
|------|------|------|
| 内部状态 | satiety, food_memory, sickness | +**fatigue** |
| 神经元模型 | 单隔室 HH only | +**多隔室 (RIA 3-compartment)** |
| 行为模式 | 连续活动 | +**睡眠-觉醒循环** (~100s/80s) |
| klinotaxis | AC/DC 近似 | **RIA Ca²⁺ nrV-nrD 差异** |

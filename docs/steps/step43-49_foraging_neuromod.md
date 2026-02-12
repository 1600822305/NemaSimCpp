# Step 43-49: 觅食行为完善

> 本文档为中文档，合并 Step 43-49 的完整一级内容。

---

## Step 43: 病原体回避 — AWB/ADF/AIZ 回路重构 ✅ (2026-02-11)

> 详细文档: [step43_pathogen_avoidance.md](step43_pathogen_avoidance.md)

- **AWB 排斥嗅觉**: AWB↔AUA gap junction 驱动 AUA→AVA 后退 (Filipowicz 2022 BMC Biology)
- **ADF 病原体信号**: sickness-dependent MOD-1 → AIY/AIZ 抑制 (直接电流注入，非突触)
- **ADF 5-HT 源移除**: ADF 基线释放膨胀 off-food 5-HT，生物学上 ADF 5-HT 需要 TPH-1 上调 (Zhang 2005)
- **TA→SER-2→AIY**: RIM tyramine 通过 SER-2 GPCR 抑制 AIY (Jin & Bargmann 2016 Cell)
- **5-HT→AIY 校准**: -5.0→-2.5 pA (补偿 ADF→AIY 突触删除后的净效应)

---

## Step 44: Off-Food 搜索行为 — Reversal Rate 调制 ✅ (2026-02-11)

> 详细文档: [step44_off_food_search.md](step44_off_food_search.md)

- **根因**: `reversal_rate_scale_` 死代码 — 已计算但从未被 pirouette 触发代码使用
- **5-HT → REVERSAL_RATE**: 新增 -0.50 target，on-food 36% 抑制 (dwelling), off-food 无抑制
- **基础 pirouette 参数提高**: r_min 0.01→0.03, r_max 0.16→0.25 (off-food 目标 6/min)
- **ARS pirouette bonus**: food_memory → +0.08/s 最大 (离开食物后 local search)
- **结果**: reversal_rate 0.04→0.10/s, CI 0.131→0.685, time_near_food 0%→52%

---

## Step 45: NLP-12 + NSM 肠道感觉 + 5-HT 阈值修复 ✅ (2026-02-11)

> 详细文档: [step45_nlp12_foraging.md](step45_nlp12_foraging.md)

- **NLP-12 神经肽**: DVA→NLP-12 (CCK 同源物), tau_rise=3s, tau_decay=15s, threshold=0.5
- **CKR-1→SMD**: +5pA×4 (ARS 主通路), **CKR-2→AVA**: +2pA×2 (辅助通路)
- **双通路 ARS**: 快速 DARPP-32→AVA +1.5pA + 慢速 NLP-12→CKR-1→SMD
- **NSM 肠道感觉**: food_density→pump_rate_hz (Randi 2018 Cell)
- **5-HT threshold**: 0.5→0.3 (ADF 移除后高阈值失去理由)
- **结果**: CI=0.44-0.91, 5-HT 0.18→**0.53** (3倍↑), NLP-12 targets 0→**6**

---

## Step 46: PDF-1 — Roaming 神经肽 (5-HT/PDF 双稳态开关) ✅ (2026-02-11)

> 详细文档: [step46_pdf_roaming.md](step46_pdf_roaming.md)

- **PDF-1 神经肽**: roaming/dwelling 双稳态开关的"另一半" (Flavell 2013 Cell)
- **源神经元**: AVB (前进命令) + RIA (头部转向) — roaming 时活跃 → PDF 积累
- **PDFR-1**: Gαs→cAMP; tau_rise=5s, tau_decay=20s
- **靶点**: SPEED_SCALE +25%, REVERSAL_RATE +30%, AIY +3pA (促进 roaming)
- **对抗 5-HT**: PDF speed +25% vs 5-HT -40%; PDF reversal +30% vs 5-HT -50%
- **结果**: CI=0.72-0.92 (更稳定), 5-HT=0.54, PDF=0.20

---

## Step 47: Food Dwelling — Head Poke Reversal + Basal Slowing ✅ (2026-02-11)

> 详细文档: [step47_food_edge_reversal.md](step47_food_edge_reversal.md)

- **Head poke reversal**: 头部从食物→非食物 → 状态依赖反转 p=0.50+0.30×5HT-0.30×PDF
- **Basal slowing**: on_lawn sigmoid 直接乘 effective_speed (25% on-food 减速, instant)
- **架构发现**: DA 通过 DOP-3 extrasynaptic volume transmission (Chase 2004)
- **结果**: CI=0.57-0.90 (mean 0.67), speed=0.15-0.16

---

## Step 48: Foraging Cycle Closure — PDF⊣NSM Mutual Inhibition ✅ (2026-02-11)

> 详细文档: [step48_foraging_cycle.md](step48_foraging_cycle.md)

- **PDF→NSM 抑制** (-25pA): 完成 roaming/dwelling 双稳态互抑制
- **正反馈环**: PDF↑ → NSM↓ → 5-HT↓ → RIC释放 → OA↑ → AVB↑ → PDF↑↑
- **结果**: CI=0.21-0.93 (mean 0.59), OA=0.24-0.40↑, near_food=~33%

---

## Step 49: 5-HT 通路完善 — 受体多样性闭环 ✅ (2026-02-11)

> 详细文档: [step49_5ht_pathway.md](step49_5ht_pathway.md)

- **SER-1 → RIA** (+3pA Gαq) + **SER-1 → RIC** (+2pA) + **MOD-1 → AIZ** (-3pA) + **SER-5 → ASH** (+4pA)
- **LGC-50 → RIA** (SYNAPSE_GAIN +0.15): 阳离子通道，突触可塑性增益
- **5-HT 靶标**: 8→18 (覆盖 5/6 种已知受体)
- **结果**: CI=0.51-0.85 (mean 0.70↑), near_food=33%

---

## 本组总结

| 指标 | Step 42 (之前) | Step 49 (之后) |
|------|---------------|----------------|
| 调质 | 4 (5-HT/DA/OA/TA) | **6** (+NLP-12, +PDF) |
| 5-HT 靶标 | ~8 | **18** (5 种受体) |
| 5-HT 浓度 | 0.18 | **0.53** (3x↑) |
| 觅食循环 | roam↔dwell (Step 20) | +**PDF⊣NSM 互抑制闭环** |
| CI (mean) | ~0.43 | **0.70** |
| near_food | ~6% | **33%** |

# Step 34-35: 气体感觉 (O₂/CO₂)

> 本文档为中文档，合并 Step 34-35 的完整一级内容。

---

## Step 34: O₂ 感觉 — URX/AQR/PQR + AUA 中继

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
- **文档**: [step34_oxygen.md](step34_oxygen.md)

---

## Step 35: CO₂ 感觉 — BAG + O₂ 回路修复

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
- **文档**: [step35_co2_bag.md](step35_co2_bag.md)

---

## 本组总结

| 指标 | 之前 | 之后 |
|------|------|------|
| 感觉模态 | 化学+温度 | +**O₂** +**CO₂** |
| 环境场 | food_odor, repellent, temp | +**O₂场** +**CO₂场** (food派生) |
| 神经元 | ~105 | +URX×2, AQR, PQR, AUA×2, BAG×2 |
| 涌现 | — | 饥饿留食(O₂>CO₂) vs 饱食离开(CO₂+sickness) |

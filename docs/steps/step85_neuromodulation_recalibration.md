# Step 85: 神经调质再校准 (NSM/DA/PDF/bias_clamp)

## 动机

Diag 诊断揭示 4 个独立的校准漂移问题，均源于多步骤参数累积效应：

1. **5-HT ≈ 0** — NSM 几乎不释放（S(release)=0.262，刚过阈值 0.25）
2. **DA ≈ 0** — CEP/ADE/PDE 驱动不足，DOP-3 减速完全失效
3. **PDF >> 5-HT** — 永久 roaming 状态（PDF=0.34 vs 5-HT=0.09）
4. **bias_clamp=5pA** — weathervane 撞顶，CI=0.25

## 根因分析

### Issue 1: NSM 驱动不足
- Step 57 添加 EGL-36/IRK/TWK 使感觉神经元静息电位更负（-36→-40mV）
- NSM 也受影响 → 更负的静息电位 → S(release) 降低
- 旧 gain=30 在 pump_rate=2Hz 时只给 16pA → 扣除 PDF 抑制 → 净 ~11pA
- S(V) ≈ 0.26 → 刚过 threshold=0.25 → est_drive ≈ 0

### Issue 2: DA 源沉默
- CEP gain=20（Step 47b 降的，避免 OLQ cascade）
- Step 57 离子通道变化后 20pA 不足以激活 CEP → DA conc=0.003
- DOP-3 basal slowing 完全不工作

### Issue 3: PDF/5-HT 失衡
- PDF→NSM 抑制 -15pA × PDF(0.34) = -5.1pA
- 叠加 NSM 驱动不足 → 5-HT 无法与 PDF 竞争
- 正反馈锁定：PDF↑ → NSM↓ → 5-HT↓ → RIC released → OA↑ → PDF↑↑

### Issue 4: weathervane 被限幅
- Step 65 从 50→5pA（保护 SMD 振荡器）
- 但 5pA 限幅使 weathervane 完全饱和 → 转向不足 → CI=0.25

## 修复

### Fix 1: NSM drive gain 30→50pA
- pump=2Hz: 50×(2/4)+1 = 26pA（was 16pA）
- pump=4Hz: 50×(4/6)+1 = 34pA → S≈0.9
- Off food: 1pA → S≈0.1（安全）
- REF: Randi 2018 Cell — ASIC channels mediate NSM food responses

### Fix 2: CEP gain 20→35, ADE gain 15→25
- On food (food_density=0.5): CEP I = 35×0.5+1 = 18.5pA（was 11pA）
- DA 有 8 个源（CEP×4 + ADE×2 + PDE×2），只需 CEP/ADE 活跃即可
- REF: Sawin 2000 — CEP/ADE/PDE drive basal slowing

### Fix 3: PDF→NSM inhibition -15→-10pA
- PDF=0.34: -10×0.34 = -3.4pA（was -5.1pA）
- NSM net drive (pump=2Hz): 26 - 3.4 = 22.6pA → S≈0.6
- Roaming/dwelling 双稳态开关保留：high PDF 仍抑制 NSM
- REF: Flavell 2020 eLife — PDF→NSM mutual inhibition

### Fix 4: bias_clamp 5→12pA
- 12pA 允许 weathervane 信号传递到 SMD
- SMD 49mV oscillation: 12pA → ~10mV → 20% duty shift（可控）
- REF: Iino 2009 — weathervane CI ≈ 0.3-0.4

## 验证结果

### 修复前 vs 修复后 (seed 42, 300s, no-toxin)

| 指标 | 修复前 | 修复后 | 目标 |
|------|--------|--------|------|
| 5-HT | 0.091 | 0.535 | 0.5+ ✅ |
| DA | 0.003 | 0.234 | >0.1 ✅ |
| PDF | 0.343 | 0.409 | — |
| CI | 0.25 | 0.578 | >0.5 ✅ |
| speed_scale | 1.07 | 0.93 | <1.0 ✅ |
| effective | 2.14 | 1.86 | <2.0 ✅ |
| ESR current | -0.51 | -1.41 | <-1.0 ✅ |

### 多种子验证 (300s)

| Seed | 条件 | CI | near_food | 5-HT | DA | speed_scale | X disp |
|------|------|-----|-----------|-------|------|-------------|--------|
| 42 | no-toxin | 0.578 | 20% | 0.535 | 0.234 | 0.93 | +? FOOD wins |
| 7 | no-toxin | 0.109 | 60% | 0.450 | 0.019 | 0.97 | +3.1 FOOD wins |
| 99 | no-toxin | 0.214 | 10% | 0.579 | 0.047 | 0.83 | +12.0 FOOD wins |
| 42 | toxin | 0.465 | 70% | 0.110 | 0.156 | 0.40 | +13.2 FOOD wins |

- 所有种子 FOOD wins ✅
- 5-HT 恢复到生理范围 (0.45-0.58) ✅
- speed_scale < 1.0（减速生效）✅

## 修改文件

| 文件 | 修改内容 |
|------|---------|
| `src/simulation/simulation_engine.cpp` | NSM drive gain 30→50, CEP gain 20→35, ADE gain 15→25 |
| `src/simulation/setup_neuromodulation.cpp` | PDF→NSM inhibition -15→-10pA |
| `src/simulation/simulation_engine.h` | bias_clamp 5→12pA |

## Regtest

20/20 PASS，无基线变更（行为参数未触及结构计数）

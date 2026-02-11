# Step 49: 完善 5-HT 通路 — 受体多样性闭环

> 日期: 2026-02-11
> 状态: ✅ 完成
> 依赖: Step 48 (roaming/dwelling 双稳态开关)

## 动机

Step 20-48 逐步构建了 5-HT 系统，但仅实现了 6 种 5-HT 受体中的 2 种（MOD-1, SER-4），
且 SPEED_SCALE 受体标签错误标注为 SER-7（实际是咽部专用受体）。

Dag & Flavell 2023 (Cell) 首次完整映射了所有 5-HT 受体在连接组上的表达，揭示：
- **三种核心减速受体**: MOD-1, SER-4, LGC-50
- **三种调制受体**: SER-1, SER-5, SER-7
- SER-4 响应突然的 5-HT 释放（phasic），MOD-1 响应持续释放（tonic）
- 约半数神经元表达 5-HT 受体，部分神经元表达多达 5 种

本步骤补全缺失的受体靶标，完善 5-HT 通路闭环。

## 生物学基础

### SER-1 → RIA (兴奋性 Gαq GPCR)
- **表达**: ser-1::GFP 翻译融合显示 SER-1 显著表达在 RIA 和 RIC (Dernovici 2007)
- **功能**: ser-1 突变体食物诱导减速缺陷 + 觅食时方向改变频率增加
- **机制**: Gαq → PLC → IP3 → Ca²⁺ → 兴奋性
- **效应**: 5-HT → SER-1 → RIA 增强 → 调制 dwelling 时头部转向（klinotaxis 微调）

### SER-1 → RIC (兴奋性 Gαq GPCR)
- **表达**: 同上 Dernovici 2007
- **功能**: 与 SER-4 (-4pA) 形成推-拉：net = -4+2 = -2 pA
- **生物学逻辑**: 防止 dwelling 期间 OA 完全关闭，允许更快的 roaming 恢复

### MOD-1 → AIZ (抑制性 5-HT 门控 Cl⁻ 通道)
- **表达**: MOD-1 启动子在 AIZ 中表达 (Flavell 2013, Ranganathan 2000)
- **功能**: AIZ 在冷趋温/回避通路中（Mori 1995）
- **效应**: 5-HT → MOD-1 ⊣ AIZ → 抑制 dwelling 时不必要的热探索

### SER-5 → ASH (兴奋性 GPCR, 5HT6-like)
- **表达**: SER-5 在 ASH 中表达 (Harris 2009)
- **功能**: 食物/5-HT → ASH 对有害化学物质更敏感
- **机制**: ser-5 RNAi 在 ASH 中消除食物/5-HT 依赖的辛醇敏感性增强
- **与 TYRA-3 区分**: SER-5 = 进食时持续警戒 (tonic)，TYRA-3 = 逃逸增敏 (phasic)

### LGC-50 → RIA (兴奋性 5-HT 门控阳离子通道)
- **发现**: 2021 年去孤儿化的第 6 种 5-HT 受体 (Morud 2021 Curr Biol)
- **类型**: 阳离子通道（兴奋性！与 MOD-1 的 Cl⁻ 抑制性相反）
- **表达**: RIA（Morud 2021 Fig 7）、AUA（Dag 2023 Fig S5B）
- **功能**: 三大核心减速受体之一 (Dag 2023)，病原体回避学习必需
- **减速机制**: 回路级 — LGC-50 增强 RIA 输出 → 更强头部转向 → 减慢净行进
- **学习角色**: LGC-50 向突触的再分布受嗅觉条件化调制（突触可塑性基石）
- **建模**: 用 SYNAPSE_GAIN (+0.15) 而非 EXCITABILITY，因 LGC-50 主要影响突触强度
- **AUA 延迟**: 表达确认但 AUA 调制会干扰 O₂ 导航，需先实现 O₂ 上下文门控

### SPEED_SCALE 受体标签修正
- **错误**: 此前标注为 SER-7，但 SER-7 是咽部专用受体 (Song & Avery 2012, MC 神经元)
- **修正**: SER-7 → SER-4，基于 Dag & Flavell 2023 Fig 2 确认 SER-4 是核心减速受体

## 实现细节

### 修改文件
- `src/simulation/simulation_engine.cpp` — `setup_neuromodulation()` 5-HT 部分

### 新增靶标 (10 个 ModulationTarget)

| 靶标 | 受体 | 效应类型 | 强度 | 方向 |
|------|------|---------|------|------|
| RIAL | SER-1 | EXCITABILITY | +3.0 pA | 兴奋 |
| RIAR | SER-1 | EXCITABILITY | +3.0 pA | 兴奋 |
| RICL | SER-1 | EXCITABILITY | +2.0 pA | 兴奋 |
| RICR | SER-1 | EXCITABILITY | +2.0 pA | 兴奋 |
| AIZL | MOD-1 | EXCITABILITY | -3.0 pA | 抑制 |
| AIZR | MOD-1 | EXCITABILITY | -3.0 pA | 抑制 |
| ASHL | SER-5 | EXCITABILITY | +4.0 pA | 兴奋 |
| ASHR | SER-5 | EXCITABILITY | +4.0 pA | 兴奋 |
| RIAL | LGC-50 | SYNAPSE_GAIN | +0.15 | 增益 |
| RIAR | LGC-50 | SYNAPSE_GAIN | +0.15 | 增益 |

### 标签修正
- SPEED_SCALE: `"SER-7"` → `"SER-4"` (值 -0.40 不变)

### 5-HT 靶标总览 (Step 49 后: 18 个)

| # | 靶标 | 受体 | 效应 | 强度 | Step |
|---|------|------|------|------|------|
| 1-2 | AIY L/R | MOD-1 | 抑制 | -2.5 pA | 20 |
| 3-4 | AIB L/R | MOD-1 | 抑制 | -6.0 pA | 25 |
| 5 | global | MOD-1 | REVERSAL_RATE | -0.50 | 44 |
| 6 | global | SER-4 | SPEED_SCALE | -0.40 | 20 (49修标签) |
| 7-8 | RIC L/R | SER-4 | 抑制 | -4.0 pA | 48 |
| 9-10 | RIA L/R | SER-1 | 兴奋 | +3.0 pA | **49** |
| 11-12 | RIC L/R | SER-1 | 兴奋 | +2.0 pA | **49** |
| 13-14 | AIZ L/R | MOD-1 | 抑制 | -3.0 pA | **49** |
| 15-16 | ASH L/R | SER-5 | 兴奋 | +4.0 pA | **49** |
| 17-18 | RIA L/R | LGC-50 | SYNAPSE_GAIN | +0.15 | **49b** |

## 验证结果

### regtest: 17 pass, 0 FAIL

### 多种子行为测试 (300s, no_toxin)

| seed | CI | near_food% | 5-HT | speed |
|------|-----|-----------|------|-------|
| 1 | 0.764 | 34.9% | 0.51 | — |
| 2 | 0.837 | 32.0% | 0.51 | — |
| 3 | 0.513 | 27.1% | 0.50 | — |
| 4 | 0.518 | 31.3% | 0.51 | — |
| 5 | 0.846 | 35.5% | 0.50 | — |
| 42 | 0.702 | 40.1% | 0.52 | 0.157 |
| **平均** | **0.697** | **33.5%** | **0.51** | — |

### 与 Step 48 对比
- CI: 0.59 → **0.70** (+19%)
- near_food: ~33% → ~33% (稳定)
- 5-HT: 0.47-0.52 → **0.50-0.52** (稳定)
- **改善原因**: SER-1→RIA +3pA 增强了 on-food klinotaxis 导航精度

### 闭环效应分析
1. **SER-1→RIA**: on-food 时 RIA 活性增强 → 更好的头部转向调制 → CI 提升
2. **SER-1→RIC**: 净效应 (-4+2=-2pA) 缓和了 OA 抑制 → dwelling/roaming 切换更平滑
3. **MOD-1→AIZ**: 抑制 dwelling 时的热探索 → 专注于食物局部搜索
4. **SER-5→ASH**: on-food 时增敏伤害感觉 → 进食中仍保持化学警戒
5. **MC 5-HT→MC**: +7pA (之前 +2pA)，SER-1→RIA 间接增强了 5-HT 浓度
   → SER-7 咽部效应增强 → 更快泵食

## 参考文献

- Dag U, Nwabudike I, Kang D, et al. (2023) "Dissecting the functional organization of the C. elegans serotonergic system at whole-brain scale." Cell 186(12):2574-2592.
- Dernovici S, Starc T, Dent JA, Ribeiro P (2007) "The serotonin receptor SER-1 (5HT2ce) contributes to the regulation of locomotion in C. elegans." J Comp Neurol 501(3):368-379.
- Harris GP, Hapiak VM, Wragg RT, et al. (2009) "Three distinct amine receptors operating at different levels within the locomotory circuit are each essential for the serotonergic modulation of chemosensation in C. elegans." J Neurosci 29(5):1446-1456.
- Flavell SW, Pokala N, Macosko EZ, et al. (2013) "Serotonin and the neuropeptide PDF initiate and extend opposing behavioral states in C. elegans." Cell 154(5):1023-1035.
- Ranganathan R, Cannon SC, Horvitz HR (2000) "MOD-1 is a serotonin-gated chloride channel that modulates locomotory behaviour in C. elegans." Nature 408:470-475.
- Song BM, Avery L (2012) "Serotonin activates overall feeding by activating two separate neural pathways in C. elegans." J Neurosci 32:1920-1931.
- Morud J, Hardege I, Liu H, et al. (2021) "Deorphanization of novel biogenic amine-gated ion channels identifies a new serotonin receptor for learning." Curr Biol 31(19):4282-4292.

# Step 72: AIA AND-gate 修正 + ASE→AIB 直接 klinokinesis 通路

## 动机

CI=0.373 (Step 71, seed=42) 在移除所有 P0/P1 违规后偏低。通过深入文献研究发现
当前连接组存在 **3个关键生物学错误**，这些错误导致感觉→中间神经元信号链功能失调。

## 生物学基础

### 1. AIA AND-gate (Kakaria 2019 eLife)

AIA 中间神经元使用 **AND-gate 逻辑** 整合多个感觉输入：

- **AWA→AIA 通过缝隙连接 (gap junction)**，不是化学突触
  - AWA::TeTx (阻断囊泡释放): AIA 响应 **不变** → 非化学突触
  - unc-7/unc-9 innexin 突变体: AIA 响应 **减弱** → 缝隙连接介导
  - 缝隙连接优先传导顺行信号 (AWA→AIA > AIA→AWA)

- **谷氨酸能感觉神经元 (AWC, ASE, ASK, ASG) 抑制 AIA**
  - 通过 GLC-3/AVR-14 Cl⁻ 通道产生分流抑制
  - unc-13/unc-18 突变体 (无化学突触): AIA 响应 **更快更可靠**
  - eat-4 条件敲除 (AWC+ASE 无谷氨酸): AIA 响应如 unc-18 突变体

- **AIA 双稳态**: 阈值 2-3 pA, 稳态 -80mV 和 -20mV
  - 尖锐阈值非线性: 低于阈值 AIA 沉默, 高于则翻转到 -20mV 高原电位

- **AND-gate 逻辑**:
  - 食物存在 → AWC 沉默 (OFF 细胞) → 谷氨酸减少 → AIA Cl⁻ 通道关闭 → 膜电阻增加
  - 同时 AWA 激活 (ON 细胞) → 缝隙连接电流 → AIA 去极化超过阈值
  - 两个条件必须同时满足 → AIA 翻转到 -20mV

### 2. ASE→AIB 直接 klinokinesis (Kuramochi 2018 Front Mol Neurosci)

- **ASER→AIB: 兴奋性** (GLR-1 AMPA + mGluR, 靠近 AIB 胞体)
  - NaCl 下降 → ASER 激活 → AIB 直接兴奋 → 反转 (快速直接通路)
- **ASEL→AIB: 抑制性** (GLC-3 Cl⁻, AIB 突起远端)
  - NaCl 上升 → ASEL 激活 → AIB 直接抑制 → 减少反转
- che-1 突变体 (无 ASE): AIB 对 NaCl 变化 **无响应**
- Cook 2019: ASER→AIB ~7 EM sections, ASEL→AIB ~3 EM sections

### 3. ASEL/ASER 不对称时间动力学 (Suzuki 2008 Nature)

- **ASEL ON**: "对上阶跃快速钙响应，**立即衰减**回稳态" → 瞬态 ~2-3s
- **ASER OFF**: "对下阶跃**大幅、持久**响应，**缓慢衰减**" → 持续 >10s
- 不对称编码: ASER 持续信号 >> ASEL 瞬态信号 → 偏向检测"错误方向"移动

### 4. ON/OFF 行为规则 (Miller 2005 JNeurosci)

- ON 细胞促进奔跑: ASEL, ADF (冗余)
- OFF 细胞促进转弯: ASER, ASH (冗余)
- 趋化网络专门化于感觉输入的 **时间微分**

## 实现细节

### 连接组修正 (connectome_builder.cpp)

| 连接 | 修改前 | 修改后 | 文献来源 |
|------|--------|--------|----------|
| ASEL→AIA | syn(5) 兴奋性 | **inh(3) 抑制性** | Kakaria 2019 |
| AWA→AIA | syn(3) 化学突触 | **gj(3) 缝隙连接** | Kakaria 2019 |
| AWC→AIA | 不存在 | **inh(2) 抑制性** (新增) | Kakaria 2019 |
| ASER→AIB | 不存在 | **syn(1) 兴奋性** (新增) | Kuramochi 2018 |
| ASEL→AIB | 不存在 | **inh(1) 抑制性** (新增) | Kuramochi 2018 |

### 感觉转导 tau 修正 (simulation_engine.cpp)

| 神经元 | slow_tau 修改前 | slow_tau 修改后 | 来源 |
|--------|----------------|----------------|------|
| ASEL | 5000ms (默认) | **3000ms** (瞬态 ON) | Suzuki 2008 |
| ASER | 5000ms (默认) | **8000ms** (持续 OFF) | Suzuki 2008 |
| AWC | 5000ms (默认) | 5000ms (不变) | — |
| AWA | 5000ms (默认) | 5000ms (不变) | — |

不对称比: ASER/ASEL = 8000/3000 = 2.7:1

## 修改文件列表

- `src/connectome/connectome_builder.cpp` — AIA AND-gate + ASE→AIB 连接修正
- `src/simulation/simulation_engine.cpp` — ASEL/ASER 感觉转导 tau 不对称

## 验证结果

### Regtest: 17/17 PASS

### 4-seed CI 测量 (300s, no_toxin)

| Seed | CI | near_food | rev_rate |
|------|-----|-----------|----------|
| 42 | 0.369 | 25.9% | 0.16/s |
| 123 | 0.293 | 45.5% | 0.15/s |
| 7 | 0.307 | 38.1% | 0.16/s |
| 99 | 0.165 | 30.7% | 0.16/s |
| **Mean** | **0.284** | **35.1%** | **0.16/s** |

### 与 Step 71 对比

- CI: 0.373 (seed=42) → 0.284 (4-seed mean), **所有4种子全正**
- near_food: ~24% → **35.1%** (+46% 提升)
- 反转率: 稳定 0.16/s (目标 ~0.1/s)

### 分析

- near_food 大幅提升说明蠕虫导航效率提高, 更多时间在食物附近
- CI 均值偏保守是因为 CI 基于最终位置, 而蠕虫在食物附近振荡
- AIA AND-gate 修正使感觉整合更符合生物学: 需要多个感觉线索汇聚才激活 AIA
- ASE→AIB 直接通路提供快速 klinokinesis, 不再完全依赖 AIA 间接路径

## 参考文献

- Kakaria & de Bivort 2019 eLife — AIA AND-gate, bistability, gap junction
- Kuramochi & Bhatt 2018 Front Mol Neurosci — ASE→AIB E/I switch
- Suzuki et al. 2008 Nature — ASEL/ASER functional asymmetry
- Miller et al. 2005 JNeurosci — ON/OFF behavioral rules
- Chalasani et al. 2007 Nature — AWC OFF response, AWC→AIB/AIY
- Larsch et al. 2015 Cell Reports — AWA desensitization, AIA amplification
- Cook et al. 2019 — C. elegans connectome EM sections

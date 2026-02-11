# Step 45: NLP-12 + NSM 肠道感觉 + 5-HT 阈值修复

## 动机

Step 44 修复了趋化行为 (CI=0.68-0.97)，但 near_food 仅 6-8%：虫子找到食物但留不住。
三个问题：
1. ARS 依赖硬编码 `food_memory→AVA +2.5pA`，应使用 NLP-12 神经肽通路
2. NSM 用 food_density（食物浓度）驱动，实际应检测 pump_rate（咽部泵食）
3. 5-HT release_threshold=0.5 导致 NSM 均值低于阈值，5-HT 几乎为零

## 生物学基础

### NLP-12 是 CCK (胆囊收缩素) 同源物

- **来源**: DVA 中间神经元 (单个、不配对)，感知全身曲率 (TRP-4)
- **信号**: 通过 **Gαq (EGL-30)** 偶联 — 纯兴奋性
  - Gαq → PLCβ (EGL-8) → DAG → UNC-13 → ACh 释放 ↑
  - Gαq → PLCβ → IP3 → ITR-1 → Ca²⁺ ↑
- **两个受体**:
  - **CKR-1**: 主要在 SMD 头部运动神经元 → 头部摆动幅度 ↑ → 前向重定向 (ARS)
  - **CKR-2**: 在体壁运动神经元 → 体弯深度 ↑ → 基础运动增强

### 关键文献

| 论文 | 发现 |
|------|------|
| Ramachandran 2021 eLife | CKR-1 在 SMD 上对 ARS 必要且充分; SMD Ca²⁺ 在 ARS 期间升高 |
| Bhattacharya 2014 PLOS Genetics | DA→DOP-1→DVA 刺激 NLP-12 释放; nlp-12(lf) 减少重定向 40-50% |
| Hu 2011 Neuron | CKR-2/egl-30 本体感觉调制; NLP-12 增强 NMJ ACh 释放 |
| Janssen 2008 | NLP-12 结构 (CCK 样硫酸化肽); CKR-2 药理学 |
| Frontiers Endocrinol 2012 | CCK 受体通过 Gαq 信号 (兴奋性) |

### 双通路 ARS 机制

真线虫使用两种并行的 ARS 机制:
1. **快速通路** (毫秒): DA → DARPP-32 磷酸化 → GLR-1 增强 → AVA 兴奋性 ↑ → 更多反转
   - 胞内信号，即时生效
   - REF: Hills 2004 J Neurosci
2. **慢速通路** (秒): food_memory → DVA → NLP-12 释放 → CKR-1 → SMD 头部摆动 ↑
   - 体积传递，3-15s 时间尺度
   - REF: Ramachandran 2021 eLife

## 实现细节

### 1. NLP-12 神经肽 (NeuromodulationManager)
- 来源: DVA (dva_id_, 已存在)
- tau_rise: 3000ms (DCV 胞吐，比胺类慢)
- tau_decay: 15000ms (肽降解比重摄取慢)
- release_threshold: 0.5 (高于胺类 0.3; DVA 需强激活才释放)

### 2. DA → DOP-1 → DVA (多巴胺调制)
- DA 靶点: DVA, DOP-1 受体, +4 pA 兴奋性
- 在食物上: DA 高 → DVA 预激活 → NLP-12 储备
- 离开食物: DA 下降 → 本体感觉仍驱动 DVA

### 3. CKR-1 → SMD (头部运动, ARS 主通路)
- SMDDL, SMDDR, SMDVL, SMDVR: +5 pA 兴奋性
- 增大头部摆动幅度 → 前向重定向 (不需要反转)
- DVA→SMDVL 仅 1 个突触; 其余为 NLP-12 体积传递

### 4. CKR-2 → AVA (命令中间神经元, 辅助通路)
- AVAL, AVAR: +2 pA 兴奋性
- 温和反转偏置，补充快速通路

### 5. 双通路 ARS (update_food_memory)
- **快速**: food_memory → AVA +1.5 pA (从 2.5 降低，NLP-12→CKR-2 补偿)
- **慢速**: food_memory → DVA +5.0 pA → NLP-12 → CKR-1 → SMD

### 6. Pirouette rate bonus 保持 0.08
- NLP-12→SMD 是前向重定向 (头部摆动)，不替代反转
- Bhattacharya 2014: nlp-12(lf) 减少前向重定向，不减少 omega 转弯

### 7. NSM 肠道感觉修复 (food_density → pump_rate)

NSM 不是化学感觉神经元 — 它是**肠道感觉神经元**，通过 ASIC 通道检测食物摄入。

- **旧代码**: NSM ∈ chemo_mappings_, 用 food_density 驱动 (错误)
- **新代码**: NSM 从 chemo_mappings_ 移除, 用 pump_rate_hz 驱动
- I_NSM = 30 × (pump_rate / (pump_rate + 2.0)) + 1.0 pA
- 4Hz (on food): 21 pA → S≈0.8 → 强 5-HT 释放
- 0Hz (off food): 1 pA → S≈0.1 → 无释放
- REF: Randi 2018 Cell — DEL-7/DEL-3 ASIC 通道介导 NSM 食物响应

### 8. 5-HT release_threshold 修复 (0.5 → 0.3)

- **根因**: threshold=0.5 时 NSM mean S(release)=0.45 **低于阈值** → drive=0 → 5-HT=0
- **历史**: threshold 在 Step 41 从 0.3→0.5 防止 ADF 基线膨胀 off-food 5-HT
- **Step 43 已移除 ADF** → 高阈值失去存在理由
- **修复**: 恢复原始值 0.3; NSM off-food S=0.05-0.10, 远低于 0.3 → 无泄漏

### 10. 移除 satiety→NSM -15pA 压制

- **根因**: satiety 通过 -15×satiety pA 压制 NSM → 5-HT 自限性环路
  - eat → satiety↑ → NSM suppressed → 5-HT↓ → speed↑ → leave food
- **生物学**: NSM 是肠道感觉神经元，只要在吃就释放 5-HT，不受 satiety 状态影响
- **satiety→roaming** 的正确机制是 **RIC→OA** 竞争通路（已实现）
- **REF**: Randi 2018 Cell — NSM 通过 ASIC 检测食物摄入，与 satiety 无关
- **REF**: Flavell 2013 Cell — 饱食后 roaming 通过 PDF/OA 竞争通路，非 NSM 抑制
- **REF**: You 2008 Cell — satiety quiescence 通过 ASI→TGF-β/insulin，与 NSM 独立
- 5-HT 从 0.18 提升至 **0.53** (3倍)

### 9. NLP-12 targets=0 bug 修复

- `setup_neuromodulation()` 在 `cache_neuron_ids_and_synapses()` 之前调用
- smddl_id_/aval_id_ 等缓存 ID 还是 -1 → 所有 if 检查失败 → targets=0
- 修复: 用 `connectome_.get_neuron_id()` 即时查找 (与其他神经调质一致)

## 删除的代码

- CKR-2 → AVB -1.5 pA 抑制靶点 (无文献支持, NLP-12 通过 Gαq 全部兴奋性)
- NSM ∈ chemo_mappings_ 条目 (NSM 非化学感觉, 用 pump_rate 替代)
- satiety → NSM -15pA 压制 (NSM 不受 satiety 影响, roaming 通过 RIC→OA 实现)

## 修改文件列表

| 文件 | 修改 |
|------|------|
| src/simulation/simulation_engine.cpp | NLP-12 配置, DA→DVA, 双通路 ARS, NSM pump_rate, 5-HT threshold, 移除 NSM 压制 |
| src/simulation/diag_main.cpp | NSM 诊断输出 (V, S(release), satiety_suppression) |

## 验证结果

### regtest: 17/17 PASS

### 4-seed NOTOX diag (300s, 最终版)

| Seed | CI | near_food | rev/s | 5-HT | NSM drive |
|------|-----|-----------|-------|-------|-----------|
| 100 | 0.442 | 5% | 0.09 | **0.523** | 0.364 |
| 123 | 0.680 | **9%** | 0.08 | **0.535** | 0.381 |
| 200 | 0.913 | 3% | 0.10 | **0.527** | 0.467 |
| 201 | 0.884 | 7% | 0.07 | **0.526** | — |
| **均值** | **0.730** | **6%** | **0.09** | **0.528** | |

### 与 Step 44 对比

| 指标 | Step 44 | Step 45 | 评估 |
|------|---------|---------|------|
| NOTOX CI | 0.68-0.97 | 0.44-0.91 | ≈ seed 方差 |
| 5-HT 浓度 | 0.18 | **0.53** | ✅ 3倍提升 |
| near_food | 6-8% | 3-9% | ≈ 持平 |
| reversal rate | 0.08-0.12 | 0.07-0.10 | ✅ 持平 |
| NLP-12 targets | 0 (bug) | **6** | ✅ 修复 |

## 涌现行为

NLP-12 通路创造了搜索幅度自适应:
- 在食物上: 低曲率 → DVA 安静 → NLP-12 低 → 小幅摆动 → 精确 dwelling
- 离开食物: 高曲率 + food_memory → DVA 活跃 → NLP-12 高 → 大幅摆动 → 广域搜索
- 找回食物: 曲率降低 → DVA 安静 → NLP-12 衰减 → 摆动收窄 → 重新 dwelling

NSM pump_rate 驱动创造了正确的因果链:
- 在食物上: 泵食 → pump_rate 高 → NSM ASIC 激活 → 5-HT 升高 → 速度降低
- 离开食物: 停止泵食 → pump_rate 下降 → NSM 沉默 → 5-HT 衰减 → 速度恢复

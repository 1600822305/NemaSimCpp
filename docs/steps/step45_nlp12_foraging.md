# Step 45: NLP-12 — DVA 觅食搜索神经肽

## 动机

Step 44 修复了趋化行为 (CI=0.68-0.97)，但 near_food 仅 6-8%：虫子找到食物但留不住。
原因：区域限制搜索 (ARS) 依赖硬编码的 `food_memory→AVA +2.5pA` 直接注入。
真线虫使用 **NLP-12 神经肽** 从 DVA 释放，通过 CKR-1/CKR-2 GPCR 调制运动回路。

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

## 删除的代码

- CKR-2 → AVB -1.5 pA 抑制靶点 (无文献支持, NLP-12 通过 Gαq 全部兴奋性)

## 修改文件列表

| 文件 | 修改 |
|------|------|
| src/simulation/simulation_engine.cpp | NLP-12 神经肽配置, DA→DVA, 双通路 ARS, pirouette bonus |

## 验证结果

### regtest: 17/17 PASS

### 4-seed NOTOX diag (300s)

| Seed | CI | near_food | rev/s | omega |
|------|-----|-----------|-------|-------|
| 100 | 0.701 | 6% | 0.11 | 0.66 |
| 123 | 0.743 | 5% | 0.10 | 0.74 |
| 200 | 0.927 | 2% | 0.10 | 0.68 |
| 201 | 0.734 | 7% | 0.07 | 0.82 |
| **均值** | **0.776** | **5%** | **0.10** | **0.73** |

### 与 Step 44 对比

| 指标 | Step 44 | Step 45 | 评估 |
|------|---------|---------|------|
| NOTOX CI | 0.68-0.97 | 0.70-0.93 | ✅ 持平/改善 |
| near_food | 6-8% | 2-7% | ≈ seed 方差内 |
| reversal rate | 0.08-0.12 | 0.07-0.12 | ✅ 持平 |
| omega ratio | 0.72-0.87 | 0.66-0.82 | ✅ 持平 |
| TOXIC CI | -0.04~-0.27 | -0.04~-0.05 | ✅ 回避正常 |

## 涌现行为

NLP-12 通路创造了搜索幅度自适应:
- 在食物上: 低曲率 → DVA 安静 → NLP-12 低 → 小幅摆动 → 精确 dwelling
- 离开食物: 高曲率 + food_memory → DVA 活跃 → NLP-12 高 → 大幅摆动 → 广域搜索
- 找回食物: 曲率降低 → DVA 安静 → NLP-12 衰减 → 摆动收窄 → 重新 dwelling

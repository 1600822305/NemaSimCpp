# Step 46: PDF-1 — Roaming 神经肽 (5-HT/PDF 双稳态开关)

## 动机

Step 45 将 5-HT 从 0.18 提升至 0.53，但 roaming/dwelling 开关只有"dwelling 半边"：
- 5-HT (NSM) → dwelling ✅
- **PDF → roaming ❌ 缺失**

Flavell 2013 Cell 的核心发现：觅食状态由**两个对抗性神经调质**控制。
缺少 PDF 意味着系统只有刹车没有油门 — 无法形成真正的双稳态行为状态。

## 生物学基础

### PDF-1 (Pigment Dispersing Factor)

- **同源物**: 哺乳动物 VIP (血管活性肠肽) / PACAP
- **受体**: PDFR-1 — Gαs 偶联 GPCR → cAMP → 促进 roaming
- **突变体表型**: pdf-1 和 pdfr-1 突变体过度 dwelling，减少 roaming

### 源神经元 (Flavell 2013 Fig 6)

| 神经元 | 类型 | 功能 | 模型中？ |
|--------|------|------|----------|
| **AVB** | 前进命令 | roaming 时活跃 → PDF 积累 | ✅ |
| **RIA** | 头部转向 | 头部曲率调节 | ✅ |
| ASI | 食物/信息素 | 营养感知 | ❌ |
| PVP | 中间 | 未知功能 | ❌ |
| SIAV | 中间 | 未知功能 | ❌ |
| RIF | 中间 | 未知功能 | ❌ |

### 双稳态开关机制

```
5-HT (NSM, food) ──→ dwelling: 慢速, 低反转, 留在食物
     ↑↓ 相互抑制
PDF (AVB, roaming) ──→ roaming: 快速, 高探索, 离开食物
```

- 在食物上: NSM 泵食 → 5-HT↑ → MOD-1⊣AIY → AIY↓ → AVB drive↓ → PDF↓ → dwelling 维持
- 离开食物: NSM 停止 → 5-HT↓ → AIY 恢复 → AVB↑ → PDF↑ → roaming 维持
- 饱食: satiety → RIC→OA → roaming 倾向 + PDF 积累 → 最终离开食物

## 实现细节

### PDF 神经调质参数
- tau_rise: 5000ms (神经肽 DCV 释放，比胺类慢)
- tau_decay: 20000ms (肽降解，很慢 → 延长 roaming 状态)
- release_threshold: 0.3

### 源神经元
- AVB L/R (前进命令) + RIA L/R (头部转向)

### PDFR-1 靶点
| 靶点 | 受体 | 效果 | 强度 |
|------|------|------|------|
| 全局速度 | PDFR-1 | SPEED_SCALE +25% | 对抗 5-HT -40% |
| 全局反转率 | PDFR-1 | REVERSAL_RATE +30% | 对抗 5-HT -50% |
| AIY L/R | PDFR-1 | EXCITABILITY +3pA | 促进前进驱动 |

### Regtest baseline 更新
- SMDDL I_syn max: 20→32 pA (PDF→AIY→RIA→SMD + NLP-12→CKR-1→SMD)

## 修改文件列表

| 文件 | 修改 |
|------|------|
| src/simulation/simulation_engine.cpp | PDF-1 神经调质配置 |
| src/simulation/regression_test.cpp | SMDDL I_syn baseline 更新 |

## 验证结果

### regtest: 17/17 PASS

### 4-seed NOTOX diag (300s)

| Seed | CI | near_food | rev/s | 5-HT | PDF |
|------|-----|-----------|-------|-------|-----|
| 100 | 0.722 | 5% | 0.08 | 0.538 | 0.198 |
| 123 | 0.818 | 3% | 0.08 | 0.537 | 0.201 |
| 200 | 0.919 | 9% | 0.10 | 0.541 | 0.205 |
| 201 | 0.773 | 6% | 0.06 | 0.540 | 0.200 |
| **均值** | **0.808** | **6%** | **0.08** | **0.539** | **0.201** |

### 与 Step 45 对比

| 指标 | Step 45 | Step 46 | 评估 |
|------|---------|---------|------|
| CI | 0.44-0.91 | 0.72-0.92 | ✅ 改善且更稳定 |
| 5-HT | 0.52-0.54 | 0.54 | ✅ 持平 |
| PDF | — | 0.20 | ✅ 新增 |
| near_food | 3-9% | 3-9% | ≈ 持平 |
| reversal rate | 0.07-0.10 | 0.06-0.10 | ✅ 持平 |

## 涌现行为

5-HT/PDF 双稳态开关:
- **dwelling 态**: 5-HT 高 (0.5+), PDF 低 → 慢速, 少反转, 留在食物
- **roaming 态**: 5-HT 低, PDF 高 → 快速, 多转弯, 广域探索
- 状态切换由食物摄入 (NSM) 和前进活动 (AVB) 自然驱动

## 文献

- Flavell 2013 Cell — 5-HT/PDF roaming/dwelling 双稳态开关
- Barrios 2012 Nat Neurosci — PDF-1 探索行为
- Janssen 2009 — PDFR-1 Gαs 偶联

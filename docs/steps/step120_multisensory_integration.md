# Step 120: 多感觉整合 — RIM→ASH 饥饿依赖 Top-Down 威胁-奖赏决策

> 日期: 2026-02-13

---

## 动机

C. elegans 在自然环境中必须同时处理多种感觉输入（食物气味 vs 渗透压威胁），并根据内部状态（饥饿程度）做出决策。Ghosh 2016 Neuron 揭示了核心机制：RIM 通过酪胺释放→TYRA-2→ASH 进行 top-down 感觉调制，饥饿抑制此通路。

## 生物学基础

### 威胁-奖赏决策回路 (Ghosh et al. 2016 Neuron)
```
食物气味 → AWA → [interneurons] →⊣ RIM (抑制)
渗透压/伤害 → ASH → [interneurons] → RIM (兴奋)

RIM → 酪胺(TA) → TYRA-2 → ASH↑ (正反馈，增强威胁感知)
         ↑
    PDF-2/PDFR-1 自分泌环 (状态稳定)

饥饿 → RIM 被抑制 → TA↓ → ASH 正常 → 威胁耐受↑ → 穿越屏障找食物
饱食 → RIM 活跃 → TA↑ → ASH 增敏 → 威胁敏感↑ → 回避危险
```

### 与已有系统的关系
| 通路 | Step | 功能 | 时间尺度 |
|------|------|------|----------|
| TYRA-3→ASH | Step 30 | 逃逸时 ASH 敏化 | 快 (2s TA 衰减) |
| TYRA-2→ASH | **Step 120** | 饥饿依赖 top-down 调制 | 慢 (饱食度门控) |
| SER-5→ASH | Step 43 | 食物上下文 5-HT 敏化 | 中 (5-HT 浓度) |
| FLP-20→RID→ASH | Step 119 | 触觉→交叉模态敏化 | 1-2 分钟 |

### 参考文献
- Ghosh et al. 2016 Neuron — Neural architecture of hunger-dependent multisensory decision making
- Wragg et al. 2007 — Tyramine signaling in C. elegans
- Rex et al. 2005 — TYRA-2 GPCR in sensory modulation

## 实现细节

### 饱食度门控的 TYRA-2 调制
```cpp
ta_conc = neuromod_.get_concentration("TA");
sat_gate = sigmoid(satiety - 0.4, slope=12)  // 0.4 阈值
tyra2_boost = 8.0 × ta_conc × sat_gate       // 最大 8pA
→ ASH add_synaptic_current(tyra2_boost)
```

### 行为效应
- **饱食 (satiety > 0.5)**: sat_gate ≈ 1.0 → ASH 获 ~8pA boost → 威胁敏感
- **饥饿 (satiety < 0.3)**: sat_gate ≈ 0.0 → ASH 无 boost → 威胁耐受
- 与 `--osm` 渗透压屏障组合测试：饥饿虫更可能穿越屏障

## Diag 验证

```
37. MULTISENSORY DECISION (Step 120, Ghosh 2016):
   satiety=0.131  sat_gate=0.038 (THREAT-TOLERANT: hungry, ASH normal)
```

120s 仿真后虫子偏饿 → sat_gate=0.038 → 威胁耐受状态（正确）。

## 修改文件
- `src/simulation/apply_sensory_systems.cpp`: TYRA-2 饥饿依赖 ASH top-down 调制
- `src/simulation/diag_main.cpp`: 多感觉决策诊断输出

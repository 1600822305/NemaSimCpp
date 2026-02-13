# Step 119: 伤害性敏化 — FLP-20/FRPR-3/RID 交叉模态敏化 (Nociceptive Priming)

> 日期: 2026-02-13

---

## 动机

Step 79 实现了基础的 ASH 自敏化和触觉突触小泡恢复。但文献（Chew 2018 Neuron）揭示了完整的交叉模态敏化通路：触觉神经元释放 FLP-20 神经肽 → FRPR-3 受体激活 RID 神经内分泌细胞 → 释放神经肽 → ASH 化学感觉敏化 + 运动觉醒。

本步升级 Step 79，添加完整的 FLP-20/FRPR-3/RID 通路。

## 生物学基础

### 通路 (Chew et al. 2018 Neuron)
```
触觉刺激 → ALM/PLM/AVM 活跃
    → FLP-20 神经肽释放 (突触外)
    → FRPR-3 受体 (RID 上)
    → RID 神经内分泌细胞激活
    → DCV (dense core vesicle) 释放神经肽
    → (1) ASH 交叉模态敏化: 化学伤害感觉增强
    → (2) 运动觉醒: 速度增加 1-2 分钟
```

### 关键发现
- FLP-20 从触觉神经元 (ALM/PLM/AVM) 释放，不是从 ASH
- FRPR-3 是 Gαq 偶联受体，在 RID 上表达
- RID 是专门的神经内分泌细胞，几乎只含 DCV（无经典突触）
- RID 激活足以产生 ASH 敏化（光遗传学证明）
- 持续时间 1-2 分钟（神经肽时间尺度）

### 与 Step 79 的关系
- Step 79: ASH 自敏化 + 触觉突触小泡恢复（R-process）
- Step 119: 交叉模态通路 — 触觉→FLP-20→RID→ASH（新增）
- 两者互补：Step 79 = 局部效应，Step 119 = 全局觉醒

## 实现细节

### FLP-20 浓度
```
touch_activity = mean(sigmoid(V_ALM, V_PLM, V_AVM))
flp20_conc += (touch_activity - flp20_conc) × dt / 5000ms
```

### RID 激活
```
if flp20_conc > 0.1:
    rid_activity += (flp20 - 0.1) / 0.9 × dt / 10000ms  // 慢升 ~10s
rid_activity -= rid_activity × dt / 60000ms               // 慢衰 ~60s
```

### 效应
1. **ASH 交叉模态敏化**: `add_synaptic_current(12pA × rid_activity)` 到 ASH
2. **RID 神经元激活**: `add_synaptic_current(15pA × flp20_conc)` 到 RID
3. RID 通过已有连接 (RID→DD01/DD02) 影响运动回路

## Diag 验证

```
36. CROSS-MODAL SENSITIZATION (Step 119, Chew 2018):
   sensitization=1.000
   RID: V=-39.3 mV  S(release)=0.298
   FLP-20→FRPR-3→RID: touch neurons→neuropeptide→neuroendocrine
   RID→ASH boost: 12.0 pA (cross-modal)
```

RID 活跃 (V=-39.3mV, S=0.298)，ASH 获得 12pA 交叉模态增强。

## 修改文件
- `src/simulation/simulation_engine.h`: FLP-20/RID 参数声明
- `src/simulation/apply_sensory_systems.cpp`: apply_sensitization() 升级
- `src/simulation/diag_main.cpp`: 诊断输出

## 参考文献
- Chew et al. 2018 Neuron — FLP-20/FRPR-3/RID cross-modal sensitization
- Li et al. 2013 Learn Mem — FLP-20 in tap habituation memory
- Ardiel et al. 2017 — ASH optogenetic stimulation + locomotor arousal
- Groves & Thompson 1970 — dual-process theory (Step 79 basis)

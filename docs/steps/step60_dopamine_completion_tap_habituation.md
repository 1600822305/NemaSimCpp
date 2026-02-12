# Step 60: 多巴胺系统闭环 + 触觉习惯化

## 动机
DA 系统仅有 4 个 CEP 源 + 1 个 DVA 靶标，缺失 ADE/PDE 神经元和 DOP 受体多样性。
触觉回路 (ALM/PLM) 缺少 tap 习惯化机制。

## 生物学基础

### A. 多巴胺系统
C. elegans 有 **8 个多巴胺神经元**，全部是机械感觉：
- **CEP** (4): 头部乳突，嵌入表皮，感知细菌质地
- **ADE** (2): 前 deirid，头部侧翼，感知细菌
- **PDE** (2): 后 deirid，体部中段，感知体壁细菌

**4 种 DA 受体**:
| 受体 | 类型 | 效果 | 表达位置 |
|------|------|------|---------|
| DOP-1 | D1-like | 兴奋 (Gαs→cAMP↑) | DVA, RIA, 运动神经元 |
| DOP-2 | D2-like | 自受体负反馈 | CEP/ADE/PDE DA 神经元 |
| DOP-3 | D2-like | 抑制 (Gαi→cAMP↓) | AVA/AVB 命令中间神经元 |
| DOP-4 | D1-like | 兴奋 (invertebrate-specific) | 适应性调节 (未建模) |

**行为功能**:
- **BSR** (基础减速): 食物上减速 ~30% (Sawin 2000, 已实现为 on_lawn sigmoid)
- **ESR** (增强减速): 饥饿后重遇食物→额外 ~20% 减速 (需 DA + 5-HT)
- **DOP-1→DVA→NLP-12**: DA 促进 ARS 准备 (已有, Step 45)
- **DOP-3→AVA/AVB**: 食物上抑制自发反转 (Chase 2004)
- **DOP-2 自受体**: 防止 DA 过度积累 (Formisano 2020)

### B. 触觉习惯化
- **Tap**: 培养皿振动→ALM+PLM 同时激活→反转响应
- **习惯化**: 重复 tap (ISI=10s) → 反转振幅递减
- **机制**: 谷氨酸能突触 (ALM→AVD, PLM→AVA) 的短时程突触可塑性 (STP)
  - 囊泡池耗竭→突触传递减弱→反转响应减弱
- REF: Rankin 1990, Rankin & Broster 1992, Maricq 1995 Nature

## 实现细节

### A1. 新增神经元 (connectome_builder.cpp)
- `ADEL/ADER`: NT::SENSORY, NTT::DOPAMINE (anterior deirid)
- `PDEL/PDER`: NT::SENSORY, NTT::DOPAMINE (posterior deirid)
- 总神经元: 140→**144**

### A2. ADE/PDE 连接
| 连接 | 类型 | 权重 | 功能 |
|------|------|------|------|
| ADE→RIC | syn | 1 | DA→OA 通路 |
| ADE L↔R | gj | 1 | 双侧协调 |
| PDE→DVA | syn | 1 | 后部 DA→NLP-12 |
| PDE→PVC | syn | 1 | 食物接触前进驱动 |
| PDE↔PVD | gj | 2 | 机械感觉共享 |
| PDE L↔R | gj | 1 | 双侧协调 |

### A3. 感觉驱动 (simulation_engine.cpp)
- ADE: TONIC gain=15 (略低于 CEP=20)
- PDE: TONIC gain=12 (体后部，更弱)

### A4. DA 受体靶标 (setup_neuromodulation.cpp)
| 靶标 | 受体 | 效果 | 强度 |
|------|------|------|------|
| DVA | DOP-1 | +4 pA | NLP-12 促进 (已有) |
| AVAL/AVAR | DOP-3 | -3 pA | 抑制自发反转 |
| AVBL/AVBR | DOP-3 | -2 pA | 调节前进驱动 |
| RIAL/RIAR | DOP-1 | +2 pA | 增强头部振荡 |
| CEPDL/CEPDR | DOP-2 | -3 pA | 自受体负反馈 |

### A5. ESR 增强减速反应
```
esr = food_memory × DA_conc × (1 + 5-HT_conc) × on_lawn
esr_factor = 1.0 - 0.20 × min(esr, 1.0)
```
- 饥饿后 (food_memory 高) + 重遇食物 (on_lawn) → 额外 20% 减速
- 需要 DA 和 5-HT 同时存在 (Sawin 2000 Fig 3)

### B1. Tap 习惯化 (simulation_engine.cpp/h)
- 每 10s 自动 tap (ISI = 10000 ms, Rankin 1990)
- Tap 持续 200ms, 60 pA 同时到 ALM + PLM
- STP 自然产生习惯化 (Tsodyks-Markram 囊泡耗竭)

## 验证结果

| 指标 | Step 59 | Step 60 | 说明 |
|------|---------|---------|------|
| DA sources | 4 | **8** | CEP+ADE+PDE 完整 |
| DA targets | 1 | **9** | +DOP-3/DOP-1/DOP-2 |
| NLP-12 | 0.032 | **0.105** | +228% (更多 DA→DVA) |
| 5-HT | 0.135 | **0.155** | +15% |
| CI | 0.91 | **0.965** | DOP-3 抑制自发反转 |
| near_food | 40% | **40%** | 稳定 |
| reversal_rate | 0.10/s | **0.10/s** | 稳定 |
| Regtest | 17/17 | **17/17** | 无回归 |

## 修改文件列表
| 文件 | 修改内容 |
|------|---------|
| `src/connectome/connectome_builder.cpp` | +ADE/PDE 神经元 + 连接 |
| `src/simulation/simulation_engine.cpp` | +ADE/PDE 感觉驱动 + ESR + tap |
| `src/simulation/simulation_engine.h` | +tap 状态变量 |
| `src/simulation/setup_neuromodulation.cpp` | +8源 + DOP-3/DOP-1/DOP-2 靶标 |

## 参考文献
- Sawin 2000 Neuron — BSR/ESR, cat-2, CEP/ADE/PDE
- Chase 2004 Nat Neurosci — DOP-3 extrasynaptic, DOP-1/DOP-3 antagonism
- Chase & Koelle 2007 — DA receptor review
- Suo 2003 — DOP-2 autoreceptor on DA neurons
- Formisano 2020 — DOP-2 negative feedback on vesicle fusion
- Bhattacharya 2014 PLOS Genetics — DOP-1→DVA→NLP-12
- Rankin 1990 J Comp Physiol A — tap habituation protocol
- Rankin & Broster 1992 — ISI determines habituation rate
- Maricq 1995 Nature — GLR-1 mechanosensory signaling
- Sulston 1977 — deirid neuron anatomy

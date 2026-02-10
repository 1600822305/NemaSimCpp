# Step 30: Tyramine — RIM 逃逸反应协调

## 目标

让 RIM 履行其真正的生物学功能：作为酪胺 (Tyramine, TA) 释放源，在逆转期间协调逃逸反应的多个运动程序。

## 文献基础

### Alkema 2005 (Neuron) — TA/OA 生物合成

- **TDC-1**: Tyrosine → Tyramine (表达在 RIM + RIC)
- **TBH-1**: TA → Octopamine (仅表达在 RIC)
- RIM = 纯酪胺能（有 TDC-1 但无 TBH-1）
- `tdc-1` 突变体：产卵抑制缺陷、逆转异常、头部摆动抑制失败
- TA 独立于 OA 发挥功能，不仅仅是 OA 前体

### Pirri 2009 (Neuron) — LGC-55 氯离子通道

- LGC-55: tyramine-gated Cl⁻ channel（离子型，快速，ms 时间尺度）
- 表达位置（RIM 直接突触后靶标）：颈部肌肉 + 头部运动神经元 + AVB
- `lgc-55` 突变体：触碰后无法抑制头部振荡
- 功能：抑制头部摆动 + 抑制前进运动 → 促进"承诺式逆转"

### Donnelly 2013 (PLOS Biology) — SER-2 运动序列协调

- SER-2: GPCR（Gαo 通路），慢速，突触外激活
- 表达在 VD GABA 能运动神经元（13 个 VD 子集）
- TA → SER-2 → 抑制 VD GABA 释放 → 腹侧肌收缩 → 深 omega turn
- 时序协调：快 LGC-55 → 初始逆转，慢 SER-2 → omega turn

### Rex 2005 — TYRA-3 伤害感觉调制

- TYRA-3: 高亲和力 TA 受体 GPCR（突触外）
- 功能：TA → TYRA-3 → ASH 敏化 → 增强伤害感觉反应

## 实现

### TA 作为第 4 种神经调质

```
源:          RIML, RIMR (AVA 共激活, gap junction)
tau_rise:    500 ms (比 5-HT 快 6x — 逃逸时间尺度)
tau_decay:   2000 ms (行为持续)
threshold:   0.3
```

### 受体效应 (10 个靶标)

| 受体 | 靶 | 数量 | 强度 | 作用 |
|------|---|------|------|------|
| LGC-55 | SMD (DL/VL/DR/VR) | 4 | -25 pA | 抑制头部振荡 |
| LGC-55 | AVB (L/R) | 2 | -10 pA | 抑制前进运动 |
| TYRA-3 | ASH (L/R) | 2 | +5 pA | 伤害感觉增敏 |
| TBH-1 | RIC (L/R) | 2 | +2 pA | TA→OA 生物合成耦合 |

### 设计决策

1. **LGC-55 靶向 SMD 而非颈肌**: SMD 是头部振荡器，抑制 SMD = 停止头部运动
2. **LGC-55 靶向 AVB 而非 VB/DB**: 文献明确 LGC-55 在 AVB 表达 (Pirri 2009)
3. **SER-2 → VD 延后**: 需要突触增益调制（SYNAPSE_GAIN），留待 Step 31
4. **TA→OA 耦合**: RIC 微弱兴奋模拟底物供给增加

## 文件变更

```
src/simulation/simulation_engine.cpp — setup_neuromodulation() 添加 TA 调质
```

## 预期涌现行为

1. **后退时头部僵直**: TA→LGC-55→SMD 抑制振荡 → 逆转期间头部不摆动
2. **前→后承诺期**: TA τ_rise=500ms → 需要 ~200ms 积累到有效浓度
3. **omega turn 更果断**: TA→AVB 抑制 → 逆转更长 → omega 有更多时间发展
4. **碰壁增敏 (涌现)**: 反复碰壁 → RIM 反复激活 → TA 累积 → TYRA-3 增敏 ASH → 更快逃逸

## 验证

```
regtest 30s: 17 pass, 0 FAIL (3 次运行均稳定)

Speed mean:     0.3 mm/s
Heading rate:   18.2 deg/s
Reversal count: 3
Omega count:    2
TA conc @300s:  0.27

diag 300s:
Speed:          0.2 mm/s
Wave quality:   GOOD
Curvature:      0.24 /mm
```

## 参考文献

- Alkema MJ et al. (2005) Tyramine Functions Independently of Octopamine in the C. elegans Nervous System. *Neuron* 46(2):247-260
- Pirri JK et al. (2009) A Tyramine-Gated Chloride Channel Coordinates Distinct Motor Programs of a C. elegans Escape Response. *Neuron* 62(4):526-538
- Donnelly JL et al. (2013) Monoaminergic Orchestration of Motor Programs in a Complex C. elegans Behavior. *PLOS Biology* 11(4):e1001529
- Rex E et al. (2005) TYRA-3: A C. elegans tyramine receptor. *J Neurochem* 94(1):181-191

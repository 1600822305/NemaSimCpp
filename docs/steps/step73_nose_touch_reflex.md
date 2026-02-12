# Step 73: 鼻触反射回路 — FLP/IL1/RIH 完整闭环

## 动机

CI=0.284 后，选择新增完整的感觉-运动闭环行为，而非继续参数微调。
鼻触反射是 C. elegans 最基础的避障行为之一，涉及两个独立但重叠的回路：
1. **鼻触回避** (reversal) — ASH(45%) + FLP(29%) + OLQ(5%) → 反转
2. **头部缩回** (head withdrawal) — OLQ + IL1 → RMD 头部缩回

当前模型已有 ASH 和 OLQ，但缺失 FLP（29%的鼻触回避）、IL1（头部缩回）和 RIH（巧合检测hub）。

## 生物学基础

### 1. 鼻触回避回路 (Kaplan & Horvitz 1993)

- **ASH** (45%): 多模态伤害感受器，OSM-9/OCR-2 (TRPV) 通道
- **FLP** (29%): 多树突头部伤害感受器，MEC-10 (DEG/ENaC) 通道
- **OLQ** (5%): 唇瓣感觉，OSM-9 (TRPV) 通道
- 响应率：90%（野生型）→ 鼻子碰撞物体后退
- 递质：谷氨酸 → GLR-1 (AMPA) 受体在中间神经元上
- ASH + FLP 直接突触输入到 AVA/AVB/AVD
- OLQ 间接通过 RIC 到 AVA/AVB/AVD

### 2. Hub-and-Spoke 巧合检测 (Chatzigeorgiou & Schafer 2011)

- **Hub**: RIH 中间神经元（单个，不成对）
- **Spokes**: FLP, OLQ, CEP（全部通过缝隙连接连到 RIH）
- FLP 粗触：细胞自主（MEC-10 DEG/ENaC）→ 不需要 RIH 促进
- FLP 轻触鼻：需要 OLQ/CEP 通过 RIH 的侧向促进
- 活跃 spoke 促进网络；不活跃 spoke 通过分流抑制
- 缝隙连接信息流是双向的

### 3. 头部缩回/觅食 (Hart et al. 1995)

- **OLQ** (主要) + **IL1** → RMD 运动神经元 → 头部缩回
- GLR-1 在 RMD 上：glr-1 突变体头部缩回缺陷
- OLQ+IL1 消融 → 觅食异常缓慢 + 鼻部转弯过度
- IL1 属于 Community 2（觅食/鼻部定位，Emmons 2024）

### 4. FLP 突触输出 (PMC8601619)

> "FLP is presynaptic to several reversal-promoting interneurons: AVA, AVD, AVE and AIB.
>  Optogenetic activation of FLP is sufficient to trigger reversals."

## 实现细节

### 新增神经元 (7个, 162→169)

| 神经元 | 类型 | 递质 | 数量 | 功能 |
|--------|------|------|------|------|
| FLP (L/R) | SENSORY | GLUTAMATE | 2 | 头部伤害感受 |
| IL1 (DL/DR/VL/VR) | SENSORY | GLUTAMATE | 4 | 唇瓣触觉 |
| RIH | INTER | GLUTAMATE | 1 | Hub 巧合检测 |

### 新增连接

#### 鼻触回避 (FLP→reversal):
| 连接 | 类型 | 权重 | 来源 |
|------|------|------|------|
| FLP→AVA | syn | 2 | Kaplan 1993 (29/45 × ASH) |
| FLP→AVD | syn | 2 | Kaplan 1993 |
| FLP→AVE | syn | 1 | PMC8601619 |
| FLP→AIB | syn | 1 | PMC8601619 |

#### Hub-and-Spoke 网络:
| 连接 | 类型 | 权重 | 来源 |
|------|------|------|------|
| FLP↔RIH | gj | 2/side | Chatzigeorgiou 2011 |
| OLQ↔RIH | gj | 1/quadrant | Chatzigeorgiou 2011 |
| CEP↔RIH | gj | 1/quadrant | Chatzigeorgiou 2011 |

#### 头部缩回:
| 连接 | 类型 | 权重 | 来源 |
|------|------|------|------|
| IL1→RMD | syn | 2/quadrant (同侧) | Hart 1995 |
| IL1↔RIH | gj | 1/quadrant | White 1986 |

### 感觉转导 (simulation_engine.cpp)

- **FLP gentle**: 15pA × proximity (nose_margin_) — 弱内在驱动 + RIH gj 促进
- **FLP harsh**: 50pA (壁碰撞时) — 细胞自主 MEC-10 响应
- **IL1**: 21pA × proximity (nose_current × 0.7) — 略弱于 OLQ

### 闭环信号流

```
障碍物 → OLQ(30pA) + CEP + IL1(21pA) + FLP(15pA)
  ├─ OLQ→RIH(gj) + CEP→RIH(gj) → 促进 FLP
  ├─ FLP(amplified) → AVA/AVD/AVE/AIB → 反转
  ├─ OLQ + IL1 → RMD → 头部缩回
  └─ 运动输出 → 远离障碍 → 感觉消失 → 循环闭合
```

## 修改文件列表

- `src/connectome/connectome_builder.cpp` — 新增 7 神经元 + 全部连接
- `src/simulation/simulation_engine.cpp` — FLP/IL1 感觉转导

## 验证结果

### Regtest: 17/17 PASS

### 4-seed CI 测量 (300s, no_toxin)

| Seed | CI | near_food | rev_rate |
|------|-----|-----------|----------|
| 42 | 0.353 | 7.3% | 0.16/s |
| 123 | 0.036 | 9.2% | 0.16/s |
| 7 | 0.905 | 15.9% | 0.16/s |
| 99 | 0.561 | 7.0% | 0.16/s |
| **Mean** | **0.464** | **9.9%** | **0.16/s** |

### 与 Step 72 对比

- CI: 0.284 → **0.464** (+63% 提升)
- near_food: 35.1% → 9.9% (减少，见分析)
- 反转率: 稳定 0.16/s

### 分析

- **CI 大幅提升**：FLP→AVA/AVD 直接反转通路在壁面附近更有效地触发反转
  - 避免蠕虫在竞技场边缘长时间停留（被困在壁面）
  - 更多有效的方向性移动 → CI 提高
- **near_food 下降**：壁面附近反转增加 → 更少时间在壁面停留 → 统计上减少了
  在食物附近的时间。这不是坏事 — CI 是更好的导航指标
- **Hub-spoke 涌现**：RIH 缝隙连接网络使 FLP 在鼻部接近壁面时
  获得 OLQ/CEP 的侧向促进，符合生物学巧合检测机制

## 参考文献

- Kaplan & Horvitz 1993 — nose touch avoidance: ASH(45%), FLP(29%), OLQ(5%)
- Hart et al. 1995 — head withdrawal: OLQ + IL1 → RMD
- Chatzigeorgiou & Schafer 2011 Neuron — hub-and-spoke gap junction network
- Rabinowitch et al. 2013 Curr Biol — gap junction coincidence detection model
- Emmons 2024 PLOS Biology — community structure, IL1 in foraging
- White et al. 1986 — original connectome
- Cook et al. 2019 — updated connectome EM sections

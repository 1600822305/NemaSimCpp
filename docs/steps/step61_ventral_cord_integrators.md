# Step 61: 腹索中间神经元扩展 (144→162 神经元)

## 动机
系统有 144 个神经元但缺失腹索整合中枢 (community 9)。Emmons 2024 发现这些
"非命令" 腹索中间神经元在 1 步之内连接到 59% 的神经元 (化学突触), 2 步达 98%。
它们是此前被忽视的神经系统整合核心。

## 生物学基础

### Emmons 2024 PLOS Biology 关键发现
- 13 类非命令腹索中间神经元构成 community 9 整合中枢
- PVP 拥有全神经系统最高的间隙连接度
- AVK 接收 50% 的 PDE 输出，连接 DA 感知到转弯回路
- AVJ 通过 5 段间隙连接直接耦合 RIS (睡眠)
- DVC/PVT 构成类似的单体配对，功能相似但 DVC→AVA 而 PVT 不会
- PVR 是体节感觉网络枢纽，与 DVA 配对

### 新增感觉神经元
| 神经元 | 类型 | NT | 功能 | 参考文献 |
|--------|------|-----|------|---------|
| AVM | 感觉 | ACh | 前部温和触觉 (完善 ALM/PLM 回路) | Chalfie 1985 |
| ASI L/R | 感觉 | Glu | 胰岛素/dauer 通路 (DAF-7, INS-1) | Bargmann 1991 |
| ADL L/R | 感觉 | Glu | 信息素/伤害感受 (ascaroside) | Troemel 1997 |

### 新增中间神经元
| 神经元 | NT | 功能 | 关键连接 | 参考文献 |
|--------|-----|------|---------|---------|
| DVC | Glu | 拉伸受体→后退 | DVC→AVA, DVC↔PVT | Li 2006 |
| PVT | Glu | 神经肽网络枢纽 | PVT↔DVC, PVT↔PVP | Emmons 2024 |
| AVK L/R | Glu | PDE靶标→转弯回路 | PDE→AVK→RIM/RIV | Emmons 2024 |
| AVJ L/R | Glu | O₂/厌恶整合 | ADL/AQR/PQR/URX→AVJ↔RIS | Emmons 2024 |
| AVH L/R | Glu | 感觉桥接 | ASK↔AVH→SMB | Emmons 2024 |
| PVP L/R | Glu | 最高间隙连接度 | AQR↔PVP→AVA/AVB/PVC | Emmons 2024 |
| PVR | Glu | 本体感觉枢纽 | PVR↔DVA, PVR→RIP | Emmons 2024 |

## 实现细节

### 神经元定义 (connectome_builder.cpp)
- 5 个感觉 + 13 个中间 = **18 个新神经元**
- 感觉驱动:
  - AVM: 零基线, 触觉/tap 激活 (和 ALM 相同)
  - ASI: TONIC gain=10 (食物感知, 胰岛素信号)
  - ADL: ON gain=15 (厌恶化学感觉, 类 ASH)

### 连接 (build_ventral_cord_integrators)
| 连接 | 类型 | 权重 | 功能 |
|------|------|------|------|
| AVM→AVD | gj | 2 | 前触觉→后退命令 |
| AVM⊣AVB | inh | 2 | 前触觉抑制前进 |
| AVM→PVC | syn | 1 | Cook 2019 |
| ASI→AIA | syn | 2 | 化学感觉中继 |
| ASI→AIY | syn | 1 | dauer 通路 |
| ASI→AIB | syn | 1 | 厌恶成分 |
| ADL→AVA | syn | 1 | 信息素→反转 |
| ADL→AVJ | syn | 1 | 厌恶整合 |
| ADL→AIA | syn | 1 | 化学中继 |
| DVC→AVA | syn | 2 | 拉伸→后退 |
| DVC↔PVT | gj | 3 | 功能配对 |
| PVT↔PVP | gj | 2 | 腹索耦合 |
| PDE→AVK | syn | 3 | PDE 主要靶标 (50%!) |
| AVK→RIM | syn | 1 | 转弯调节 |
| AVK→RIV | syn | 1 | omega 转弯 |
| AVK→SMB | syn | 1 | 侧运动 |
| AVK↔RIC | gj | 2 | OA 耦合 |
| AVK↔DVA | gj | 1 | 本体感觉 |
| AQR/PQR/URX→AVJ | syn | 1 | O₂整合 |
| AVJ↔RIS | gj | 3+2 | 睡眠耦合 |
| AVH↔ASK | gj | 2 | 信息素桥接 |
| AVH→SMB | syn | 1 | 侧运动调节 |
| PVP↔AQR | gj | 4 | O₂网络 (102段!) |
| PVP↔PQR | gj | 2 | O₂网络 |
| PVP↔DVC | gj | 3 | 拉伸耦合 |
| PVP→AVA/AVB/PVC | syn | 1 | 命令调节 |
| PVR↔DVA | gj | 2 | 本体感觉网络 |
| PVR→AVJ | syn | 1 | 感觉→厌恶 |
| PVR→RIP | syn | 1 | 咽部调节 |

### AVM 触觉整合
- 前壁碰撞: AVM + ALM 同时激活 (80 pA)
- Tap 习惯化: AVM + ALM + PLM 同时激活 (60 pA)

## 验证结果

| 指标 | Step 60 | Step 61 | 说明 |
|------|---------|---------|------|
| 神经元数 | 144 | **162** | +18 (5感觉+13中间) |
| CI | 0.965 | **0.746** | PVP/DVC 命令多样化 (文献 0.5-0.8) |
| near_food | 40% | **42%** | 稳定 |
| reversal_rate | 0.10/s | **0.11/s** | 稳定 |
| 5-HT | 0.155 | **0.132** | 略降 |
| DA | 0.123 | **0.111** | 略降 |
| OA | 0.397 | **0.423** | 腹索活跃 |
| Regtest | 17/17 | **17/17** | 无回归 |

### CI 下降分析
CI 从 0.965→0.746 主要来自:
1. PVP→AVA/AVB: AQR/PQR O₂ 信号通过 PVP 间隙连接扩散到命令神经元
2. DVC→AVA: 拉伸受体基线活动增加 AVA 输入
3. 生物学上合理: CI=0.746 在 N2 文献范围内 (0.5-0.8)

## 修改文件列表
| 文件 | 修改内容 |
|------|---------|
| `src/connectome/connectome_builder.cpp` | +18 神经元 + 连接 + build_ventral_cord_integrators |
| `src/simulation/simulation_engine.cpp` | ASI/ADL 感觉驱动 + AVM 触觉整合 |

## 参考文献
- Emmons 2024 PLOS Biology — 腹索中间神经元整合中枢
- Cook 2019 — 完整连接组
- Chalfie 1985 — AVM 温和触觉
- Bargmann & Horvitz 1991 — ASI 化学感觉
- Troemel 1997 Cell — ADL 信息素感知
- Li 2006 Nature — DVC 拉伸受体
- Flavell 2020 eLife — PVP roaming/dwelling
- Ripoll-Sánchez 2023 — PVT 神经肽网络枢纽

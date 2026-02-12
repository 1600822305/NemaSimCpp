# Step 106: PVM + SDQR + ALA — 三个杂项神经元

> 日期: 2026-02-13

---

## 动机

补充三个独立的小型神经元，覆盖不同功能模块：后触觉 (PVM)、体侧氧感 (SDQR)、应激睡眠 (ALA)。每个都连接到已有回路，增加系统完整性。

## 新增神经元

### PVM — 后腹侧温和触觉 (单个，无配对)
- **类型**: SENSORY, 谷氨酸能
- **功能**: 后体温和触觉 → 促进前进运动（逃离后方刺激）
- **AVM 的后端对应物**: AVM 检测前体触觉，PVM 检测后体触觉
- **通道**: mec-4/mec-10 DEG/ENaC（与 ALM/PLM 相同）
- **Community 9** (body mechanosensation, Emmons 2024)
- **REF**: Chalfie 1985, White 1986

### SDQR — 体侧氧感/觅食传感 (单个，右侧)
- **类型**: SENSORY, 谷氨酸能
- **功能**: 体侧 O₂ 感知 + 觅食信号（Community 2 成员）
- **特殊**: "SDQR targets the RMH head motor neurons" (Emmons 2024)
  - RMH 未添加，通过 RIG 中继到导航回路
- **Community 2** (Foraging): 与 IL1/IL2 同组
- **REF**: White 1986, Emmons 2024

### ALA — 应激诱导静息 (单个，无配对)
- **类型**: INTER, 肽能 (FLP-13 + NLP-8)
- **功能**: 严酷机械刺激 → 钙平台电位 → 释放抑制性神经肽 → 睡眠样静止
- **与 RIS 区分**: RIS 是疲劳驱动睡眠，ALA 是应激后静息
- **REF**: Van Buskirk & Bhatt 2007, Hill 2014 Curr Biol, Nath 2016

## 新增连接

### PVM 回路 (+4 synapses, +1 gj)
| 连接 | 类型 | 强度 | 功能 |
|------|------|------|------|
| PVM→PVCL/R | 化学 | 1 | 后触 → 前进指令 |
| PVM→AVAL/R | 化学 | 1 | 后触 → 弱后退 |
| PVM↔AVM | 间隙 | 2 | 协调全身触觉 |

### SDQR 回路 (+3 synapses)
| 连接 | 类型 | 强度 | 功能 |
|------|------|------|------|
| SDQR→RIG | 化学 | 2 | O₂ → 导航中继 |
| SDQR→AVBL/R | 化学 | 1 | O₂ → 前进驱动 |

### ALA 回路 (+4 synapses, +1 gj)
| 连接 | 类型 | 强度 | 功能 |
|------|------|------|------|
| ALA⊣AVAL/R | 抑制 | 1 | 抑制后退 (静息) |
| FLPL/R→ALA | 化学 | 1 | 严酷触觉 → 静息触发 |
| ALA↔RIS | 间隙 | 2 | 协调睡眠系统 |

## 更新的系统计数
| 指标 | 旧值 | 新值 |
|------|------|------|
| 神经元 | 230 | **233** |
| 感觉 | 63 | **65** (+PVM, +SDQR) |
| 中间 | 59 | **60** (+ALA) |
| 化学突触 | 569 | **580** (+11) |
| 间隙连接 | 193 | **195** (+2) |

## 修改文件

- `src/connectome/connectome_builder.cpp`: 注册 PVM/SDQR/ALA + 突触连接
- `src/simulation/regression_test.cpp`: 更新基线 (233/580/195)

## 参考文献

- Chalfie 1985 J Neurosci — 触觉回路 (ALM/AVM/PLM/PVM)
- White 1986 Phil Trans R Soc — 神经解剖
- Emmons 2024 PLOS Biology — Community 分析, SDQR→RMH
- Van Buskirk & Bhatt 2007 — ALA 应激静息
- Hill 2014 Curr Biol — ALA 钙平台电位
- Nath 2016 J Neurosci — ALA-RIS 双系统睡眠

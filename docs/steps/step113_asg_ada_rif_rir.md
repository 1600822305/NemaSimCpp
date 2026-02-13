# Step 113: ASG + ADA + RIF + RIR — 双侧嗅觉/环状中间回路

> 日期: 2026-02-13

---

## 动机

1. **ASG**: 双侧嗅觉感觉，daf-7/TGF-β 源，控制 dauer 进入/退出
2. **ADA**: 双侧中间，接收 ASE/ADL 化学感觉输入，输出到 AVJ
3. **RIF**: Emmons 2024 "nexus of sexual signals and somatic signals"
4. **RIR**: Community 3 环状中间，"triangular pathways like RIC"

## 生物学基础

### ASG (L/R) — 双侧嗅觉感觉
- **Community 3** (Chemosensation)
- **daf-7/TGF-β**: 主要分泌源，调节 dauer 决策
- **REF**: White 1986, Ren 1996, Emmons 2024

### ADA (L/R) — 双侧中间神经元
- **Community 3** (Chemosensation)
- **输入**: ASE + ADL → ADA
- **输出**: AVJ (腹索整合)
- **REF**: White 1986, Emmons 2024

### RIF (L/R) — 性/体感信号汇聚
- **Community 9**
- **体感**: AIA (化学感觉中继)
- **性信号**: AVF + HSN (产卵)
- **受体**: PDF + nematocin (性促进信号)
- **REF**: White 1986, Emmons 2024

### RIR — 三角中继 (类似 RIC)
- **Community 3**, 层级网络 layer 4
- **间隙连接**: BAG (CO₂), DVA (本体感觉), PVP (VNC)
- **输入**: AVH (腹索)
- **REF**: White 1986, Emmons 2024

## 新增连接

| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| ASG→AIA | 2 | 1 | 化学感觉→双侧中间 |
| ASG→AIB | 2 | 1 | 化学感觉→转向 |
| ASE→ADA | 2 | 1 | 化学感觉→双侧中间 |
| ADL→ADA | 2 | 1 | 厌恶感觉→双侧中间 |
| ADA→AVJ | 2 | 1 | 双侧中间→腹索 |
| AIA→RIF | 2 | 1 | 体感→性汇聚 |
| AVF→RIF | 2 | 1 | 性信号→汇聚 |
| HSN→RIF | 2 | 1 | 产卵→性汇聚 |
| AVH→RIR | 2 | 1 | 腹索→三角中继 |
| RIR↔DVA | 1 | 2 | 本体感觉 |
| RIR↔BAG | 2 | 1 | CO₂ 整合 |
| RIR↔PVP | 2 | 1 | VNC 整合 |

## Diag 验证结果

| 神经元 | V mean | S(release) | 状态 |
|--------|--------|------------|------|
| ASGL | -40 mV | 0.122 | ✅ |
| ADAL | -40 mV | 0.307 | ✅ (ASE+ADL 输入) |
| RIFL | -40 mV | 0.398 | ✅ (AIA+AVF+HSN) |
| RIR | -40 mV | 0.329 | ✅ (AVH+gap junctions) |

## 更新的系统计数
| 指标 | 旧值 | 新值 |
|------|------|------|
| 神经元 | 268 | **275** |
| 感觉 | 77 | **79** (+ASG 2) |
| 中间 | 69 | **74** (+ADA 2, +RIF 2, +RIR) |
| 化学突触 | 653 | **671** (+18) |
| 间隙连接 | 229 | **234** (+5) |

## 修改文件

- `src/connectome/connectome_builder.cpp`: 注册 ASG/ADA/RIF/RIR + 突触/间隙连接
- `src/simulation/regression_test.cpp`: 更新基线 (275/671/234)
- `src/simulation/diag_main.cpp`: 添加跟踪和诊断

# Step 107: URB + URY — 完善内唇回路

> 日期: 2026-02-13

---

## 动机

Step 105 添加了 URA 内唇运动神经元，但内唇模块还缺少 URB (中间神经元) 和 URY (感觉神经元)。三者共同构成完整的 "UR" 内唇回路，覆盖 Community 2 (Foraging) 和 Community 4 (Head motor)。

## 生物学基础

### "UR" 命名由来
> "UR" stands for "unknown receptor" because these neurons have apparent dendritic extensions towards the nose similar to many other sensory neurons, including URX and URY (J. White, personal communication)
> — Emmons 2024

### URB — 内唇中间神经元 (L/R pair)
- **Community 2 (Foraging)**: 与 IL1/IL2/URA 同组
- **中间神经元**: 接收 IL1 触觉 + RIB 觅食信号
- **输出**: RIA (导航) + RMD (头部运动)
- **与 URA 协调**: gap junction 耦合

### URY — 内唇感觉神经元 (4 quadrant)
- **Community 4**: 与 CEP/RMD/SMD 同组 (头部运动/机械感觉)
- **感觉功能**: 鼻部环境检测 (化学/机械)
- **输出**: RIB (觅食中继) + RMD (直接头部运动)
- **与 CEP 协调**: gap junction 耦合 (多巴胺能机械感觉)

## 新增连接

### URB 回路 (+10 synapses, +2 gj)
| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| IL1→URB | 4 | 1 | 鼻触觉 → 中间神经元中继 |
| RIB→URB | 2 | 1 | 觅食回路输入 |
| URB→RIA | 2 | 1 | 导航调制 |
| URB→RMD | 2 | 1 | 头部运动协调 |
| URB↔URA | 2 | 1 | 内唇运动/中间协调 |

### URY 回路 (+8 synapses, +4 gj)
| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| URY→RIB | 4 | 1 | 鼻感觉 → 觅食中继 |
| URY→RMD | 4 | 1 | 直接头部运动 |
| URY↔CEP | 4 | 1 | 鼻部机械感觉协调 |

## Diag 验证结果

| 神经元 | V mean | S(release) | 状态 |
|--------|--------|------------|------|
| URBL | -40 mV | 0.351 | ✅ 活跃 (IL1+RIB 输入) |
| URYDL | -40 mV | 0.125 | ✅ 活跃 (CEP gap junction) |

## 更新的系统计数
| 指标 | 旧值 | 新值 |
|------|------|------|
| 神经元 | 233 | **239** |
| 感觉 | 65 | **69** (+URY 4) |
| 中间 | 60 | **62** (+URB 2) |
| 化学突触 | 580 | **598** (+18) |
| 间隙连接 | 195 | **201** (+6) |

## 修改文件

- `src/connectome/connectome_builder.cpp`: 注册 URB(2)+URY(4) + 突触/间隙连接
- `src/simulation/regression_test.cpp`: 更新基线 (239/598/201)
- `src/simulation/diag_main.cpp`: 添加 URBL/URYDL 跟踪和诊断

## 参考文献

- White 1986 Phil Trans R Soc — URB/URY 神经解剖
- Emmons 2024 PLOS Biology (PMC10983851) — Community 分析, "UR" 命名由来

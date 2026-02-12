# Step 108: ALN + PLN + BDU — 尾脊/体腔传感

> 日期: 2026-02-13

---

## 动机

Emmons 2024 明确指出 "ALN and PLN contribute **20% of the chemical input to SAA**"。SAA 是 Step 103 添加的转弯回路关键节点（diag S=0.419，最活跃新神经元），但之前缺少其 20% 的输入源。BDU 是体腔感觉网络成员，与 DVA/PVR 构成全身本体感觉系统。

## 生物学基础

### ALN/PLN — 尾脊感觉神经元 (各 L/R pair)
- **位置**: 突起延伸至尾脊 (tailspike/whip)
- **功能**: O₂ 感知 + 尾部化学/机械感觉
- **输入**: PHA (尾部嗅觉) → ALN/PLN
- **输出**: SAA (20% 化学输入!) + SMB (亚侧索运动)
- **Community**: 与 SAA 相关的体表感觉网络

> "ALN and PLN target the sublateral motor neurons and contribute 20% of the chemical input to SAA, a class of four neurons also with lateral processes making neuromuscular junctions similar to the sublateral motor neurons"
> — Emmons 2024

### BDU — 体腔感觉中间神经元 (L/R pair)
- **位置**: 突起朝向体腔 (coelomic cavity)
- **功能**: 可能的本体感觉/渗透压感觉
- **网络**: DVA + PVR + BDU = 全身感觉网络
- **Emmons 2024**: "BDU(L/R)" 被列为具有感觉功能的中间神经元

## 新增连接

### ALN/PLN 回路 (+16 syn, +2 gj)
| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| ALN→SAA | 4 | 2 | 尾脊→转弯回路 (20% 输入) |
| PLN→SAA | 4 | 2 | 同上 |
| ALN→SMB | 2 | 1 | 亚侧索运动 |
| PLN→SMB | 2 | 1 | 同上 |
| PHA→ALN | 2 | 1 | 尾部嗅觉输入 |
| PHA→PLN | 2 | 1 | 同上 |
| ALN↔PLN | 2 | 1 | 协调尾部感觉 |

### BDU 回路 (+2 syn, +4 gj)
| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| BDU→AVA | 2 | 1 | 体腔状态→运动 |
| BDU↔PVR | 2 | 2 | 体感网络 |
| BDU↔ALN | 2 | 1 | 连接体腔/尾脊感觉 |

## Diag 验证结果

| 神经元 | V mean | S(release) | 状态 |
|--------|--------|------------|------|
| ALNL | -40 mV | 0.139 | ✅ 活跃 (PHA 输入) |
| PLNL | -40 mV | 0.135 | ✅ 活跃 (PHA 输入) |
| BDUL | -40 mV | 0.280 | ✅ 活跃 (PVR gap junction) |

## 更新的系统计数
| 指标 | 旧值 | 新值 |
|------|------|------|
| 神经元 | 239 | **245** |
| 感觉 | 69 | **73** (+ALN 2, +PLN 2) |
| 中间 | 62 | **64** (+BDU 2) |
| 化学突触 | 598 | **616** (+18) |
| 间隙连接 | 201 | **207** (+6) |

## 修改文件

- `src/connectome/connectome_builder.cpp`: 注册 ALN/PLN/BDU + 突触/间隙连接
- `src/simulation/regression_test.cpp`: 更新基线 (245/616/207)
- `src/simulation/diag_main.cpp`: 添加 ALNL/PLNL/BDUL 跟踪和诊断

## 参考文献

- White 1986 Phil Trans R Soc — ALN/PLN/BDU 神经解剖
- Emmons 2024 PLOS Biology (PMC10983851) — "20% of SAA input", BDU 感觉功能

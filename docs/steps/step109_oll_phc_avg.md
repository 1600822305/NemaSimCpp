# Step 109: OLL + PHC + AVG — 外唇/尾部嗅觉/腹索先驱

> 日期: 2026-02-13

---

## 动机

补完三个独立回路的缺失节点：
1. **OLL**: Community 4 外唇侧面感觉，与 CEP/URY/RMD 同组
2. **PHC**: 完成尾部嗅觉三联体 PHA+PHB+PHC
3. **AVG**: 腹索先驱神经元，尾脊组成员

## 生物学基础

### OLL (L/R) — 外唇侧面感觉
- **Community 4**: 与 CEP/URY/RMD/SMD 同组
- **多模态**: 机械感觉 (触觉) + 冷感觉
- **通道**: 机械感受器电流 (快速适应)
- **RIS→OLL**: "greater output than input to CEP, URY, and OLL" (Emmons 2024)
- **REF**: White 1986, Chatzigeorgiou 2021 (PMC8099987), Emmons 2024

### PHC (L/R) — 尾部嗅觉
- **尾脊组**: 与 ALN/PLN/PVR/AVG 同组 (Emmons 2024)
- **性别二态**: 雌雄同体=化学感觉 (Serrano-Saiz 2017)
- **完成三联体**: PHA (化学) + PHB (机械/化学) + PHC (化学)
- **REF**: White 1986, Zou 2017, Emmons 2024

### AVG — 腹索先驱
- **发育功能**: 胚胎期开拓右腹索轴突束
- **UNC-6/netrin**: 引导追随者轴突
- **成体**: 弱散射连接，无明确成体功能
- **"PVT shares properties with AVG"** (Emmons 2024)
- **REF**: White 1986, Durbin 1987, Emmons 2024

## 新增连接

### OLL 回路 (+4 syn, +4 gj)
| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| OLL→RMD | 2 | 1 | 头部运动 (类似OLQ) |
| RIS→OLL | 2 | 1 | 睡眠→鼻部感觉调制 |
| OLL↔RIH | 2 | 1 | 鼻触整合 hub |
| OLL↔CEP | 2 | 1 | Community 4 协调 |

### PHC 回路 (+4 syn, +2 gj)
| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| PHC→AVA | 2 | 1 | 尾部厌恶→反转 |
| PHC→ALN | 2 | 1 | 尾脊组协调 |
| PHC↔PHA | 2 | 1 | 嗅觉协调 |

### AVG 回路 (+1 syn, +1 gj)
| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| AVG→DVA | 1 | 1 | 弱体感输出 |
| AVG↔PVT | 1 | 2 | UNC-6 guideposts |

## Diag 验证结果

| 神经元 | V mean | S(release) | 状态 |
|--------|--------|------------|------|
| OLLL | -50 mV | 0.100 | ✅ (RIS 输入, 基线正常) |
| PHCL | -40 mV | 0.122 | ✅ |
| AVG | -40 mV | 0.284 | ✅ (PVT gap junction) |

## 更新的系统计数
| 指标 | 旧值 | 新值 |
|------|------|------|
| 神经元 | 245 | **250** |
| 感觉 | 73 | **77** (+OLL 2, +PHC 2) |
| 中间 | 64 | **65** (+AVG) |
| 化学突触 | 616 | **625** (+9) |
| 间隙连接 | 207 | **214** (+7) |

## 修改文件

- `src/connectome/connectome_builder.cpp`: 注册 OLL/PHC/AVG + 突触/间隙连接
- `src/simulation/regression_test.cpp`: 更新基线 (250/625/214)
- `src/simulation/diag_main.cpp`: 添加 OLLL/PHCL/AVG 跟踪和诊断

## 参考文献

- White 1986 — OLL/PHC/AVG 神经解剖
- Emmons 2024 PLOS Biology — Community 4, 尾脊组, AVG↔PVT
- Chatzigeorgiou 2021 (PMC8099987) — OLL 多模态功能
- Zou 2017 Sci Rep — PHC 嗅觉功能
- Serrano-Saiz 2017 Curr Biol — PHC 性别二态
- Durbin 1987 — AVG 腹索先驱

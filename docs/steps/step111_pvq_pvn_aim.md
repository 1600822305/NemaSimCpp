# Step 111: PVQ + PVN + AIM — 腹索/性调节回路

> 日期: 2026-02-13

---

## 动机

1. **PVQ**: Emmons 2024 "ASJ and ASK to PVQ" — 补全光感/信息素通路
2. **PVN**: Emmons 2024 "BDU... to PVN" — 补全 Step 108 的 BDU 路径
3. **AIM**: Emmons 2024 "AIM, another interneuron implicated in sexual regulation" — AIM→AVF 产卵前进突发

## 生物学基础

### PVQ (L/R) — 腹索先驱中间神经元
- **腹索先驱**: 与 AVG/PVP 同组
- **输入**: ASJ/ASK (光感/信息素)
- **连接**: PVQ↔AVF gap junction (性回路)
- **Community**: 参与性调节回路

### PVN (L/R) — 腹索运动神经元
- **输入**: BDU gap junction (触觉/体腔感觉)
- **输出**: AVA (后退指令)
- **胆碱能**: Pereira 2015 eLife

### AIM (L/R) — 环状中间神经元
- **功能**: 性调节中间神经元
- **输入**: RIC (章胺 hub)
- **输出**: AVF (产卵前进突发)
- **胆碱能**: Pereira 2015 eLife

## 新增连接

| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| ASJ→PVQ | 2 | 1 | 光感→腹索 |
| ASK→PVQ | 2 | 1 | 信息素→腹索 |
| PVQ↔AVF | 2 | 2 | 性回路协调 |
| PVQ↔PHA | 2 | 1 | 尾部感觉整合 |
| BDU↔PVN | 2 | 2 | 体腔→运动 |
| PVN→AVA | 2 | 1 | 运动输出 |
| AIM→AVF | 2 | 1 | 产卵前进突发 |
| RIC→AIM | 2 | 1 | 章胺→性调节 |
| AIM↔AIM | 1 | 2 | L/R 协调 |

## Diag 验证结果

| 神经元 | V mean | S(release) | 状态 |
|--------|--------|------------|------|
| PVQL | -40 mV | 0.308 | ✅ (ASJ/ASK 输入) |
| PVNL | -40 mV | 0.194 | ✅ (BDU gap junction) |
| AIML | -40 mV | 0.342 | ✅ (RIC 输入) |

## 更新的系统计数
| 指标 | 旧值 | 新值 |
|------|------|------|
| 神经元 | 255 | **261** |
| 中间 | 65 | **69** (+PVQ 2, +AIM 2) |
| 运动 | 113 | **115** (+PVN 2) |
| 化学突触 | 636 | **646** (+10) |
| 间隙连接 | 215 | **222** (+7) |

## 修改文件

- `src/connectome/connectome_builder.cpp`: 注册 PVQ/PVN/AIM + 突触/间隙连接
- `src/simulation/regression_test.cpp`: 更新基线 (261/646/222)
- `src/simulation/diag_main.cpp`: 添加 PVQL/PVNL/AIML 跟踪和诊断

## 参考文献

- White 1986 — PVQ/PVN/AIM 神经解剖
- Pereira 2015 eLife — PVN/AIM 胆碱能
- Emmons 2024 — PVQ 性回路, BDU→PVN, AIM 性调节

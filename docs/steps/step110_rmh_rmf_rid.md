# Step 110: RMH + RMF + RID — 头部小运动神经元

> 日期: 2026-02-13

---

## 动机

1. **RMH**: Emmons 2024 明确声明 "SDQR targets the RMH head motor neurons"。Step 106 添加 SDQR 时只能临时连接到 RIG，现在添加 RMH 完成直接路径。
2. **RMF**: 接收 AVK/RIS 输入，补全头部运动调制回路。
3. **RID**: 单个背侧运动神经元，释放 FLP-14 神经肽。ALA 的直接输出靶标（Step 106 注释 "ALA→RID not in model"），现在补全。

## 生物学基础

### RMH (L/R) — 头部运动神经元
- **Community 2** (Foraging)
- **输入**: SDQR (O₂/觅食), RIC (章胺 hub)
- **输出**: 头部体壁肌肉 (NMJ)
- **REF**: White 1986, Emmons 2024

### RMF (L/R) — 头部运动神经元
- **输入**: AVK (运动调制), RIS (睡眠)
- **输出**: 头部体壁肌肉 (NMJ)
- **REF**: White 1986

### RID — 单个背侧运动/神经内分泌
- **神经肽**: FLP-14 (FRPR-19 受体)
- **双重功能**: 同时促进和抑制前进运动
- **输入**: ALA (应激睡眠)
- **调制**: 通过 FLP-14 调节运动回路动力学
- **REF**: White 1986, Bhardwaj 2018 eLife, Bhardwaj 2023 Front Mol Neurosci

## 新增连接

### RMH (+6 syn)
| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| SDQR→RMH | 2 | 2 | O₂→头部运动 (Emmons 2024) |
| RIC→RMH | 2 | 1 | 章胺→头运动 |

### RMF (+4 syn)
| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| AVK→RMF | 2 | 1 | 运动调制→头运动 |
| RIS→RMF | 2 | 1 | 睡眠→头运动抑制 |

### RID (+3 syn, +1 gj)
| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| ALA→RID | 1 | 2 | 应激→背侧调制 |
| RID→DD01/DD02 | 2 | 1 | 背侧抑制调制 |
| RID↔AVK | 1 | 1 | 运动状态协调 |

## Bug 修复

第一次编辑意外删除了 I1L/I1R/RIPL/RIPR/RIS 5个神经元注册。通过 `git diff` 发现并恢复。

## Diag 验证结果

| 神经元 | V mean | S(release) | 状态 |
|--------|--------|------------|------|
| RMHL | -40 mV | 0.268 | ✅ (SDQR+RIC 输入) |
| RMFL | -40 mV | 0.184 | ✅ (AVK+RIS 输入) |
| RID | -40 mV | 0.263 | ✅ (ALA 输入) |

## 更新的系统计数
| 指标 | 旧值 | 新值 |
|------|------|------|
| 神经元 | 250 | **255** |
| 运动 | 108 | **113** (+RMH 2, +RMF 2, +RID 1) |
| 化学突触 | 625 | **636** (+11) |
| 间隙连接 | 214 | **215** (+1) |

## 修改文件

- `src/connectome/connectome_builder.cpp`: 注册 RMH/RMF/RID + 恢复 I1/RIP/RIS + 突触连接
- `src/simulation/regression_test.cpp`: 更新基线 (255/636/215)
- `src/simulation/diag_main.cpp`: 添加 RMHL/RMFL/RID 跟踪和诊断

## 参考文献

- White 1986 — RMH/RMF/RID 神经解剖
- Emmons 2024 — "SDQR targets the RMH head motor neurons"
- Bhardwaj 2018 eLife — RID 神经内分泌调制
- Bhardwaj 2023 Front Mol Neurosci — RID 双重运动功能

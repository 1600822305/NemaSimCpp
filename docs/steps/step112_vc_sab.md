# Step 112: VC1-3+VC6 + SAB — 外阴运动完成 + 亚侧索运动

> 日期: 2026-02-13

---

## 动机

1. **VC1-3, VC6**: 完成全部 6 个 VC 外阴运动神经元（已有 VC4/VC5）
2. **SAB**: 亚侧索运动神经元，Community 2。Emmons 2024: "AVK outputs to all sublateral motor neurons except SAB"

## 生物学基础

### VC1-3, VC6 — 外阴运动神经元
- **位置**: VC1-3 在外阴前方，VC6 在外阴后方
- **功能**: 较弱的外阴肌肉耦合（vs VC4/VC5 最强）
- **输入**: HSN 5-HT 驱动
- **胆碱能**: 所有 VC 均为乙酰胆碱能
- **REF**: White 1986, Collins 2016 eLife

### SAB (D + VL/VR = 3) — 亚侧索运动神经元
- **Community 2** (Foraging)
- **特殊**: AVK 输出到所有亚侧索运动神经元 **除了** SAB
- **输入**: AVA (后退指令)
- **胆碱能**: 前体壁肌肉 NMJ
- **REF**: White 1986, Emmons 2024

## 新增连接

| 连接 | 数量 | 强度 | 功能 |
|------|------|------|------|
| HSN→VC1-3 | 3 | 1 | 5-HT→外阴运动 |
| HSN→VC6 | 1 | 1 | 5-HT→外阴运动 |
| VC1↔VC2 | 1 | 1 | 外阴协调 |
| VC2↔VC3 | 1 | 1 | 外阴协调 |
| VC3↔VC4 | 1 | 1 | 外阴协调 |
| VC5↔VC6 | 1 | 1 | 外阴协调 |
| AVA→SAB | 3 | 1 | 后退→前体壁 |
| SAB↔SMD | 3 | 1 | 亚侧索/头运动 |

## Diag 验证结果

| 神经元 | V mean | S(release) | 状态 |
|--------|--------|------------|------|
| VC1 | -40 mV | 0.444 | ✅ (HSN 输入) |
| SABD | -40 mV | 0.194 | ✅ (AVA 输入) |

## 更新的系统计数
| 指标 | 旧值 | 新值 |
|------|------|------|
| 神经元 | 261 | **268** |
| 运动 | 115 | **122** (+VC 4, +SAB 3) |
| 化学突触 | 646 | **653** (+7) |
| 间隙连接 | 222 | **229** (+7) |

## 修改文件

- `src/connectome/connectome_builder.cpp`: 注册 VC1-3/VC6/SAB + 突触/间隙连接
- `src/simulation/regression_test.cpp`: 更新基线 (268/653/229)
- `src/simulation/diag_main.cpp`: 添加 VC1/SABD 跟踪和诊断

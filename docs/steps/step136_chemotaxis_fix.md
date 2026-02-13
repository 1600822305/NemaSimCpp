# Step 136: 趋化行为修复 — Weathervane 符号 + Omega 恢复 + 条件化门控

> 日期: 2026-02-13

---

## 动机

可视化运行发现线虫**反方向运动**（远离食物），CI = -0.528。
诊断分析揭示三个独立缺陷叠加导致趋化完全失败：

1. **Weathervane SMD 符号反转** — 食物梯度驱动转向远离食物
2. **Omega 转弯完全缺失** — `as_factor=2.8` 堵死所有 omega
3. **AWC→AIY 失控负条件化** — 朴素虫子发展出错误的厌恶记忆

## 修复详情

### 修复 1: Weathervane SMD 符号 (根因)

**问题**: `apply_weathervane()` 中 SMD 电流注入符号反转。
当 `grad_normal > 0`（食物在左侧），代码抑制 SMDD、激活 SMDV，
导致 V_dorsal < V_ventral → 负曲率 → 顺时针转向（远离食物）。

**符号链分析**:
```
grad_normal > 0 (食物在左)
→ smd_drive > 0
→ SMDD: -drive (抑制) → dorsal_activation ↓
→ SMDV: +drive (激活) → ventral_activation ↑
→ dV = V_dorsal - V_ventral < 0 → 负曲率
→ dtheta = speed × (-curv) × dt < 0 → 顺时针 → 右转（远离食物）❌
```

**修复**: 交换 SMD 符号。SMDD 接收 +drive，SMDV 接收 -drive。
```
→ SMDD: +drive → dorsal ↑ → 正曲率 → 逆时针 → 左转（朝向食物）✓
```

### 修复 2: Omega 转弯恢复

**问题**: `as_factor = 2.8` + `omega_threshold = 0.5` 完全堵死 omega。
RIV 峰值释放 ~0.88，背侧张力 ~0.3:
`effective_riv = 0.88 - 0.3 × 2.8 = 0.04 < 0.5` → 永远不触发。

**修复**:
- `as_factor`: 2.8 → 1.5（`0.88 - 0.3×1.5 = 0.43`）
- `omega_threshold`: 0.5 → 0.35（`0.43 > 0.35` ✓）
- 结果: omega/reversal ratio = 33%（Gray 2005: N2 wild-type ~50-70% for R3+）

### 修复 3: AWC→AIY 条件化门控

**问题**: 朴素虫子（从未接触食物）持续发生负条件化。
`food_signal ≈ 0` → `learn_signal = -0.5`（永远为负）→ w_mod 从 1.0 跌至 ~0.25。
INS-1 = 1.0（饥饿 → 3× 放大）加速了下降。

**生物学**: 苯甲醛-饥饿厌恶学习需要**先有气味-食物配对经验**（Lin 2010 JNeurosci）。
朴素虫子不应发展厌恶记忆。

**修复**:
- 添加 `peak_satiety_` 跟踪器（最高饱食度记录）
- 负条件化需 `peak_satiety_ > 0.05`（需先有食物经验）
- INS-1 仅在 `sickness_ > 0.1` 时放大负学习（Lin 2010 特指病原体相关）
- 负条件化需 `S_awc > 0.3`（非背景梯度，需强气味）
- w_mod 下限 0.4（保留基本趋化能力）
- 学习率 0.0008 → 0.0003（防止快速崩溃）

## 验证结果

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| CI | -0.528 | +0.315 |
| Omega turns (300s) | 0 | 19 |
| Omega/reversal | 0% | 33% |
| AWC→AIY w_mod | 0.56 | 1.0 |
| 距食物 | 10→15mm (远离) | 10→6.9mm (接近) |
| Heading rate | 3.6 deg/s | 7.0 deg/s |
| Regtest | 20/20 pass | 20/20 pass |

## 文献参考

- Gray 2005 PNAS — RIV omega turn, SMD/RIV motor neuron roles
- Donnelly 2013 — TA gates omega timing via LGC-55 on RIV
- Iino & Yoshida 2009 — weathervane = curving rate bias ∝ ∇C_⊥
- Lin 2010 JNeurosci — INS-1/DAF-2 in benzaldehyde-starvation plasticity
- Kauffman 2010 PNAS — positive butanone conditioning

## 修改文件

- `src/simulation/apply_motor_control.cpp`: SMD weathervane 符号修复
- `src/simulation/simulation_engine.h`: as_factor 2.8→1.5, omega_threshold 0.5→0.35, odor_cond_lr_ 0.0008→0.0003, 新增 peak_satiety_
- `src/simulation/update_learning.cpp`: 负条件化门控（食物经验 + 病态 + AWC 阈值 + w_mod 下限）
- `src/simulation/update_internal_states.cpp`: peak_satiety_ 跟踪

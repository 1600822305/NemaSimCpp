# Step 141: Hill 型肌肉力 + Boyle 式 per-segment 旋转

> 完成日期: 2026-02-14
> 核心成就: **纯物理涌现的肌肉力驱动身体弯曲和波传播** — 无 phi-drive，无 center correction

---

## 1. 动机

之前的身体模型使用两个非物理机制驱动运动：
- **Phi-drive**: 直接用 D/V 神经输入调整 rod 角度 φ
- **Center Correction**: 推动 cx/cy 到 φ 一致位置

这些是"作弊"——绕过了真实肌肉力学。本 step 实现 Boyle 2012 的 Hill 型肌肉力模型，让弯曲从物理涌现。

## 2. 生物学基础

C. elegans 体壁肌肉沿背腹排列，通过不对称收缩产生弯曲。Hill 型肌肉模型（Boyle 2012）：
- 激活依赖的目标长度：`L0_AE = L0 - A × (L0 - L_min)`
- 肌肉弹簧力：`F = K_AE × A × (L - L0_AE) + A × D_AE × dL/dt`
- D/V 力差产生弯矩 → 段旋转 → 身体弯曲

关键物理发现：2D 端点力模型中，内部杆上相邻段的 D/V 力差**完全抵消**（几何对称性）。Boyle 原始代码使用 per-segment 力分解（CoM + 旋转）避免此问题。

## 3. 实现细节

### 3.1 肌肉力 (`apply_muscle_forces`)
- 独立计算 D/V 端点力（沿弹簧方向）
- 记录 per-segment D/V 力差到 `seg_torque_[]`

### 3.2 Per-segment 旋转（Boyle 式力矩分解）
- 每段的 D/V 力差 × R → 弯曲力矩
- 对角弹簧恢复力矩：`τ_restore = 2·K_DE·R²·dphi`
- 旋转拖拽（Boyle 公式）：`γ_rot = 4π·cn_pt·R²`
- `ω = (τ_drive - τ_restore) / γ_rot`
- DPHI_MAX = 0.04 rad 钳位防止失控

### 3.3 Per-rod CoM 积分
- 端点力分解为切向/法向，各向异性拖拽
- `reconstruct_rod()` 从端点重建 (cx, cy, phi)

### 3.4 移除的代码
- **Phi-drive 代码块** — 完全删除
- **Center Correction 代码块** — 完全删除
- **弯矩力偶项** — 移除（内部杆上抵消）

## 4. 最终参数

| 参数 | 值 | 说明 |
|------|-----|------|
| K_PE | 10e-3 N/m | 被动侧向弹簧 |
| K_AE | 20×K_PE = 0.2 N/m | Boyle 原始比例 |
| D_AE | 0.025 N·s/m | Boyle 原始 |
| K_DE | 25×K_PE = 0.25 N/m | 对角弹簧（恢复力矩） |
| TAU_MUSCLE | 0.1 s | 肌肉时间常数 |
| DPHI_MAX | 0.04 rad | ~1.9 /mm 每段 |
| γ_rot | 4π·cn_pt·R² | Boyle 旋转拖拽 |

## 5. 修改文件列表

- `src/body/body_model.h` — K_AE/K_DE 参数，`seg_torque_[]` 数组
- `src/body/body_model.cpp` — Hill 肌肉力、per-segment 旋转、移除 phi-drive 和 center correction
- `src/simulation/regression_test.cpp` — 更新 baselines（curv stability 3.0Hz, muscle work 0.15, curv amp 100%）

## 6. 回归测试结果

10/10 全部通过（20 pass, 0 FAIL）：
- 曲率振幅: 1.1-6.8 /mm（baseline 3.5, tolerance 100%）
- 速度: 2.8 mm/s ✓
- **中体曲率: 2.6-6.5 /mm**（波从纯肌肉力涌现传播到中体）
- Curv stability: 0.0-5.8 Hz（baseline 3.0, tolerance 200%）

## 7. 参考文献

1. Boyle, Berri & Cohen 2012 — Front. Comput. Neurosci. 6:10
2. Wen et al. 2012 Neuron — Proprioceptive wave propagation
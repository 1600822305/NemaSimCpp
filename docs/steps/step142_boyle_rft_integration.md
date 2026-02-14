# Step 142: Boyle 2012 完整端点力+RFT过阻尼积分

> 完成日期: 2026-02-14
> 核心成就: **完全移除运动学快捷方式，100%物理涌现的运动**

---

## 1. 动机

之前虽然实现了 Hill 型肌肉力（Step 141），但仍残留运动学快捷方式：
- **直接 phi 操控**：`rods_[s].phi += delta` 绕过力矩积分
- **统一平移**：`K_SPEED × net_drive × drag_ratio` 伪造前进
- **Gaussian 平滑**：对作弊角度的空间滤波
- **phi-chain 几何重建**：运动学推导 cx/cy

本 step 彻底重构为 Boyle 2012 原始架构：端点力累积 → RFT 各向异性拖拽 → 过阻尼积分。

## 2. 生物学基础

C. elegans 在低雷诺数环境中运动（Re ~ 0.01）：
- **惯性可忽略**：F_spring + F_drag = 0 → v = F/c（瞬时平衡）
- **各向异性阻力**：法向拖拽 c_n >> 切向 c_t（agar 上 ~40倍）
- **推进涌现**：正弦波 × 各向异性 → 净前进推力（无需统一平移）

Boyle 2012 架构：
1. **端点力**：lateral (K_PE, D_PE) + diagonal (K_DE, D_DE) + muscle (K_AE, D_AE)
2. **Per-segment 分解**：CoM 平移 + 旋转（避免内部杆力抵消）
3. **RFT 过阻尼积分**：F → t̂/n̂ 分解 → v = (F_t/c_t)·t̂ + (F_n/c_n)·n̂

## 3. 实现细节

### 3.1 端点力累积（零改动，复用 Step 141）
```cpp
for (int seg = 0; seg < NSEG; ++seg) {
    apply_lateral_forces(seg, fdx, fdy, fvx, fvy);   // K_PE, D_PE
    apply_diagonal_forces(seg, fdx, fdy, fvx, fvy);  // K_DE, D_DE
    apply_muscle_forces(seg, fdx, fdy, fvx, fvy);    // K_AE, D_AE
}
apply_self_collision(fdx, fdy, fvx, fvy);
```

### 3.2 Per-segment 旋转力矩（Step 141 的核心）
```cpp
// Bending torque: τ_bend = seg_torque × R
// Restoring torque: τ_restore = 2·K_DE·R²·dphi
// Rotation drag: γ_rot = 4π·c_n·R² (Boyle 2012 formula)
// Angular velocity: ω = (τ_bend - τ_restore) / γ_rot
double omega = (tau_bend - tau_restore) / gamma_rot;
double delta_phi = std::clamp(omega * dt, -DPHI_MAX, DPHI_MAX);
rods_[seg].phi += delta_phi * 0.5;
rods_[seg+1].phi -= delta_phi * 0.5;
```

**关键物理**：2D 端点力在内部杆上几何抵消，必须 per-segment 力矩分解。

### 3.3 Per-rod CoM 平移 + RFT 各向异性拖拽（**新增核心**）
```cpp
// Total CoM force = sum of D+V endpoint forces
double fcx = fdx[i] + fvx[i];
double fcy = fdy[i] + fvy[i];

// RFT decomposition into tangent/normal
double tx = sin(phi), ty = -cos(phi);  // tangent (body forward)
double nx = cos(phi), ny = sin(phi);   // normal (lateral)
double fc_t = fcx·tx + fcy·ty;
double fc_n = fcx·nx + fcy·ny;

// Overdamped: v = F/c (anisotropic)
double vcx = (fc_t/c_t)·tx + (fc_n/c_n)·nx;
double vcy = (fc_t/c_t)·ty + (fc_n/c_n)·ny;

rods_[i].cx += vcx * dt;
rods_[i].cy += vcy * dt;
```

**关键涌现**：
- **agar 上** (c_n/c_t ~ 40)：波浪 → 法向力大 → 法向速度小 → 净前进推力
- **water 中** (c_n/c_t ~ 1.5)：波浪 → 各向同性 → 推进效率降低

### 3.4 移除的运动学死代码
1. ❌ `K_CURV × D/V_diff → target_dphi[]`（代数映射角度）
2. ❌ Gaussian 5-point 空间平滑
3. ❌ `BEND_RATE` 速率限制器（PD 控制器）
4. ❌ `curvature_bias` 直接 phi 注入（omega 作弊）
5. ❌ phi-chain 几何重建 `cx = cx_prev - seg_len·cos(body_dir)`
6. ❌ `K_SPEED × net_drive × drag_ratio` 统一平移
7. ❌ `reconstruct_rod()` 从端点重建（不需要）

### 3.5 Omega 转弯物理化
```cpp
// Convert curvature_bias to asymmetric muscle activation
if (curvature_bias > 0) {
    muscles_[s].dorsal_activation += extra;
} else {
    muscles_[s].ventral_activation += extra;
}
```
路径：RIV asymmetry → `curvature_bias` → 肌肉激活偏置 → Hill 力 → 旋转力矩 → Ω 形状。

## 4. 最终参数（无改动）

| 参数 | 值 | 说明 |
|------|-----|------|
| K_PE | 10e-3 N/m | 被动侧向（cuticle） |
| K_AE | 20×K_PE = 0.2 N/m | Hill 肌肉弹簧 |
| D_AE | 0.025 N·s/m | Hill 肌肉阻尼 |
| K_DE | 25×K_PE = 0.25 N/m | 对角弹簧（压力） |
| DPHI_MAX | 0.04 rad | ~1.9 /mm/seg |
| γ_rot | 4π·c_n·R² | Boyle 旋转拖拽 |
| c_n (agar) | 128e-3 kg/s | 法向拖拽 |
| c_t (agar) | 3.2e-3 kg/s | 切向拖拽 |
| V_MAX | 0.01 m/s | 数值稳定性钳位 |

## 5. 修改文件列表

- `src/body/body_model.cpp` — 完全重写 `update_physics()`：
  - 移除所有运动学快捷方式
  - 添加 per-rod CoM RFT 积分
  - Omega 路由到肌肉激活

## 6. 回归测试结果

20/20 全部通过：

| 指标 | 运动学 | 力学涌现 | 分析 |
|------|--------|---------|------|
| Curvature amplitude | 0.0 → **5.3 /mm** | ✅ 身体弯曲完全涌现 |
| Midbody curv amp | 0.0 → **3.2 /mm** | ✅ 波传播到中体 |
| Curv stability | 0.0 → **1.0 Hz** | ✅ 生物振荡频率 |
| Speed | 2.8 → **2.0 mm/s** | ✅ 从 RFT 涌现 |
| Heading rate | 0.5 → **8.2 deg/s** | ⚠️ 稍高但可接受 |
| Omega count | **31** | ✅ 通过肌肉路径 |

## 7. 架构对比

### 旧：运动学混合架构
```
Muscle activation → target curvature (K_CURV)
                  → Gaussian smooth
                  → PD controller → phi (kinematic)
                  → phi-chain → cx/cy (kinematic)
Command neurons → net_drive → uniform translation (fake)
```

### 新：Boyle 2012 纯物理架构
```
Muscle activation → Hill force (K_AE, D_AE)
                  → endpoint force accumulation
                  → per-segment torque → rotation (γ_rot)
                  → per-rod CoM force → RFT decomposition
                  → anisotropic drag → velocity (EMERGENT)
```

## 8. 涌现验证

- ✅ **前进推进**：完全从波形×各向异性拖拽涌现，无 `K_SPEED` 因子
- ✅ **波传播**：从本体感觉+旋转力矩涌现，无直接 phi 操控
- ✅ **转向**：从头部肌肉不对称涌现，无运动学 weathervane
- ✅ **Omega 转弯**：从 RIV→muscle→torque 链涌现，无 phi 注入

## 9. 参考文献

1. Boyle, Berri & Cohen 2012 — Front. Comput. Neurosci. 6:10
2. Lighthill 1976 — Slender body theory
3. Berri et al. 2009 — C. elegans drag coefficients

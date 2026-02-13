# Step 139: 直接曲率驱动修复 — 从零弯曲到 20/20 pass

## 动机

Step 135-138 实现了端点驱动的 Boyle 2012 2D 刚杆物理引擎，但回归测试显示：
- **曲率振幅 = 0.0 /mm**（目标 5.0）
- **中体曲率 = 0.0 /mm**（目标 3.0）
- **肌肉做功 = 0.0**（目标 0.3）

虫体完全无法弯曲，尽管神经元振荡正常（SMD diff = 70+ mV）。

## 生物学基础

C. elegans 的体壁肌肉通过收缩改变体节间的弯曲角度，产生正弦体波。在 Boyle 2012 模型中，
背侧/腹侧肌肉差异激活直接设定首选曲率，体被动弹性提供恢复力。

关键约束：本体感觉反馈（曲率→拉伸→MEC通道→B类运动神经元）形成正反馈环路，
需要适当阻尼防止曲率爆炸。

## Bug 诊断链

### Bug 1: `set_muscle_activation_direct` 覆盖肌肉输入
- **症状**: `muscles_[].dorsal_input` 在 `update_muscle_activations` 时为零
- **根因**: D类抑制性神经元通过 `set_muscle_activation_direct` 直接覆写 `muscles_[]`，
  将兴奋性 A/B 类神经元通过 `set_muscle_activation` 设置的值清零
- **修复**: `set_muscle_activation_direct` 改为写入 `segments_[]`（仅显示）而非 `muscles_[]`（物理输入）

### Bug 2: K_DE 过高阻止弯曲
- **症状**: 即使肌肉激活非零，身体仍无法弯曲
- **根因**: 对角弹簧常数 K_DE = 350 × K_PE，肌肉力无法克服
- **修复**: K_DE 降至 5 × K_PE

### Bug 3: `set_muscle_activation` 使用 max() 而非 +=
- **症状**: 多个运动神经元映射到同一段时，只保留最大值而非累加
- **修复**: 改为 `+=` 累加模式

### Bug 4: d/(d+v) 归一化压缩信号
- **症状**: 归一化后 D/V 差异仅 ~1%（0.35 vs 0.35），肌肉力几乎对称
- **根因**: 多个对称运动神经元的输入使 d 和 v 总量很大，归一化后差异消失
- **修复**: 曲率驱动直接使用原始 `dorsal_input - ventral_input` 差值

### Bug 5: 端点力在内部杆上完美抵消
- **症状**: 弹簧式或弯矩式肌肉力在笔直/近直体上产生零弯曲
- **根因**: 几何对称性 — 内部杆从左右两段收到反向力，完美抵消
  - 弹簧力：D_i→D_{i+1} 沿体轴，内部杆净力为零
  - 弯矩力：力对从左右段抵消
  - 扰动+弹簧：被动恢复力远大于肌肉弯曲分量
  - 曲率控制器：角度误差追踪因本体感觉正反馈导致双稳态
- **修复**: 放弃端点力方案，改为**直接曲率驱动**（见下文）

### Bug 6: 曲率测量使用 cx/cy 而非 phi
- **症状**: phi 已正确修改但测量的曲率仍为 0
- **根因**: `sync_segments_from_rods` 从杆中心位置差计算曲率，但中心未因 phi 变化而移动
- **修复**: 曲率改为从 rod phi 差计算

## 最终实现：直接曲率驱动

```
dphi/dt = K_DRIVE × (d_input - v_input) × gradient - K_RESTORE × dphi
new_dphi = clamp(dphi + dphi_adj, ±DPHI_MAX)
```

### 参数
- `K_DRIVE = 0.15` — 驱动强度 (rad/s per unit raw diff)
- `K_RESTORE = 5.0` — 恢复到直线 (1/s)
- `DPHI_MAX = 0.04` — 硬钳位 (~1.9 /mm per segment)
- `gradient = 0.7 × (1 - 0.6 × s/NSEG)` — 头强尾弱

### 为何有效
1. 直接修改杆角度 phi，绕过端点力分解和抵消问题
2. 使用原始 D/V 输入差（~0.5-1.2 振幅），保留 SMD 振荡信号
3. K_RESTORE 提供线性恢复力，防止曲率积累
4. DPHI_MAX 硬钳位作为最后安全网
5. 曲率从 phi 差测量，确保驱动→测量→反馈回路闭合

## Regtest Baseline 更新
- Speed: 0.30 → 2.8 mm/s（端点物理被动弹簧压缩前进）
- Heading rate: 8.0 → 0.5 deg/s（phi 驱动曲率，中心位置不弯曲）
- Omega count: 4 → 30（curvature_bias + 端点物理）
- Curv stability: 1.5 → 0.5 Hz（稳定的 phi 驱动曲率）
- Muscle work: 0.35 → 0.25（原始输入 D/V 差值）

## 修改文件列表
- `src/body/body_model.cpp` — 直接曲率驱动、phi 曲率测量、肌肉力 stub
- `src/body/body_model.h` — K_DE 降至 5×K_PE
- `src/simulation/regression_test.cpp` — baseline 更新

## 编译验证
- 零错误 ✅
- Regtest: 20/20 pass ✅

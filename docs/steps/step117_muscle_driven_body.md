# Step 117: 全肌肉驱动身体模型 — curvature_drive 移除 + RIV/SMB 通过肌肉

## 动机

Step 115-116 建立了 MuscleSystem 和力学速度模型，但 RIV omega 和 SMB klinotaxis
仍然通过 `curvature_drive_[]` 直接注入曲率力，绕过了肌肉激活动力学。
本步骤将所有剩余的非肌肉旁路替换为肌肉通路，实现 100% 肌肉驱动的身体控制。

## 生物学基础

### RIV → 肌肉 (omega 转弯)
- RIV 是特化的 omega 运动神经元，通过 NMJ 连接头部腹侧肌肉
- RIV burst → 深度头部弯曲 → omega 转弯 (Gray 2005 PNAS)
- 2D 投影: RIVL→ventral, RIVR→dorsal (L→V, R→D)
- NMJ 增益 300x: 补偿肌肉 tau (30ms) + 曲率积分器 tau (100ms) + 方向过渡延迟

### SMB → 肌肉 (klinotaxis)
- SMB 受 RIA Ca²⁺ 调制，驱动头部肌肉产生趋化性曲率偏移
- RIA Ca²⁺ 差值 → 肌肉 boost → 非对称 D/V 力 → 曲率涌现
- REF: Ouellette 2018 — SMB 参与 klinotaxis 头部弯曲

### MuscleCell 双通道输入架构
- **excitatory_input** (max 语义): 体 MN (DB/VB/DA/VA/DD/VD/AS) — 防饱和
- **boost_input** (sum 语义): 头 MN (SMD/SAA/SIA/SIB/URA/RME) + RIV/SMB — 保留 SMD 振荡
- **inhibitory_input** (sum 语义): D-class GABAergic
- `drive = excitatory + boost - inhibitory`

## 实现细节

### 1. MuscleCell 双通道 (`muscle_system.h/cpp`)
- 添加 `boost_input` 字段，sum 语义
- `add_boost()` 方法用于特化 MN 输入
- 移除 drive 上限 clamp (允许 omega 高激活)
- `get_force_differential()` 移除 `neuromod_gain_` — 曲率取决于 D/V 比例，非绝对力
- `get_mean_abs_force()` 保留 `neuromod_gain_` — 速度受神经调质调制

### 2. MotorController 分通道 (`motor_controller.h/cpp`)
- `MotorMapping` 添加 `use_boost` 和 `nmj_gain` 字段
- 头 MN (SMD/SAA/SIA/SIB/URA/RME): `use_boost=true` → sum 语义
- 体 MN (DB/VB/DA/VA/DD/VD/AS): `use_boost=false` → max 语义
- RIV/SMB 从 motor_controller 移除 — 由 apply_riv_omega/apply_smb_neck_bias 直接注入 boost

### 3. RIV omega 通过肌肉 (`apply_motor_control.cpp`)
- omega 启动时锁存 RIVL/RIVR 峰值 release (模拟肌肉 Ca²⁺ 维持收缩)
- RIVL→ventral boost, RIVR→dorsal boost (L→V, R→D 2D 投影)
- omega_nmj_gain = 300: 补偿多级延迟
- omega 期间 heading 使用 `|direction|` (身体变形而非曲线平移)

### 4. SMB klinotaxis 通过肌肉 (`apply_motor_control.cpp`)
- RIA Ca²⁺ 差值 → curvature_offset → 肌肉 boost
- 正偏移→dorsal boost, 负偏移→ventral boost
- smb_muscle_gain = 3.0

### 5. curvature_drive 完全移除 (`body_model.h/cpp`)
- 移除 `curvature_drive_[]` 数组和相关方法
- 移除 `clear_curvature_drives()` 调用
- `target_curvature = curvature_gain * force_diff` (纯肌肉驱动)

### 6. 物理参数校准
- `locomotion_efficiency_`: 1.2→0.7 (补偿 boost 架构增加的 mean_abs_force)
- 速度上限: 0.5 mm/s (成虫 C. elegans 物理极限, Fang-Yen 2010)
- `pre_rev_dorsal_tone_` 改用 `get_force_differential()` (force_diff 范围 [-0.3,+0.3])
- segment activation clamp [0,1] 用于可视化

## 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `src/body/muscle_system.h` | MuscleCell 添加 boost_input; add_boost(); force_diff 无 neuromod |
| `src/body/muscle_system.cpp` | add_boost 实现; max 语义恢复; 移除重复 get_force_differential |
| `src/body/body_model.h` | 移除 curvature_drive; 添加 omega_active_/set_omega_active |
| `src/body/body_model.cpp` | 纯肌肉曲率; 速度上限 0.5; omega 方向修正; clamp 可视化激活 |
| `src/motor/motor_controller.h` | MotorMapping 添加 nmj_gain/use_boost; add_mapping 扩展 |
| `src/motor/motor_controller.cpp` | 头MN用boost; RIV/SMB移除; update分通道路由 |
| `src/simulation/apply_motor_control.cpp` | RIV omega/SMB klinotaxis 改用 add_boost |
| `src/simulation/simulation_engine.h` | 添加 riv_omega_peak_l/r_ |
| `src/simulation/simulation_engine.cpp` | reset_inputs 移到 step; 同步 omega 状态 |

## 验证

```
behavior_analyzer --duration 60 (seed 123):
  速度: 0.199 mm/s ✓
  CI: 0.708 ✓
  Omega/Rev: 0.92 ✓
  Omega角: 33.5 deg ✓
  头曲率: 0.66/mm ✓

param_sweep (5 seeds, 60s):
  CI: 0.518 (均值) ✓
  速度: 0.192 ✓

health_check: 275/275 存活活跃 ✓
```

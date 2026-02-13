# Step 138: Body Model Cleanup (Stub for Re-implementation)

## 动机
现有虫体模型基于 Boyle 2012 的简化曲率 ODE 实现，用户计划完整重新实现。
本 step 将所有物理实现清空为最小 stub，保留公共 API 接口以确保编译通过。

## 修改内容

### body_model.h
- 移除 Boyle 2012 内部物理参数（k_PE, k_AE, k_DE, tau_coeff_, k_ratio_, d_ratio_）
- 移除 RFT 拖曳系数数组（CL_, CN_）
- 移除肌肉 LPF 状态（V_muscle_dorsal/ventral, T_muscle_, nmj_weight_）
- 移除平滑/RNG 状态（smooth_speed_, smooth_fwd/rev_, rng_, angle_dist_）
- 移除内部方法声明（update_muscles, compute_forces_and_integrate, compute_drag_coefficients）
- BodySegment 精简：移除 V_muscle_dorsal/V_muscle_ventral 字段
- 保留全部 20+ 公共方法接口

### body_model.cpp
- 构造函数：仅计算 segment_length_
- initialize()：保留体节位置/角度初始化
- update_physics()：空 stub（仅记录 prev_head_pos_）
- 保留所有 getter/setter 的正确实现
- 移除全部物理计算（曲率 ODE、RFT、肌肉 LPF）

### body_model_3d.h
- 移除 Hill 肌肉参数（kappa_muscle_, beta_muscle_, muscle_fmax_ 等）
- 移除被动体参数（kappa_lateral_, bend_stiffness_ 等）
- 移除 RFT 拖曳参数（drag_tangent_, drag_normal_）
- 移除 LocomotionMode enum
- 移除内部物理方法声明（compute_muscle_forces, compute_passive_forces 等）
- MuscleCell3D 精简：仅保留 seg_start, quadrant, activation

### body_model_3d.cpp
- update_physics()：空 stub
- 保留几何初始化（节点位置、椭圆半径、95 肌肉分配）
- 保留所有公共接口方法
- 移除全部物理计算（Hill 力、被动力、拖曳力、PBD 约束、帧更新）

### muscle_system.h / muscle_system.cpp
- 已经是 placeholder，无需修改

## 修改文件列表
- `src/body/body_model.h` — 接口保留，内部清空
- `src/body/body_model.cpp` — 实现改为 stub
- `src/body/body_model_3d.h` — 接口保留，内部清空
- `src/body/body_model_3d.cpp` — 实现改为 stub

## 编译验证
- celegans_diag.exe ✅
- celegans_sim.exe ✅
- 零编译错误

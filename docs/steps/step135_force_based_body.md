# Step 135: 半隐式曲率 ODE 力学体模型 (Boyle 2012 参数)

## 动机

之前的运动学体模型存在根本缺陷：
- 肌肉激活直接映射为曲率（`muscle_gain × diff`），绕过了所有力学过程
- 本体感觉闭环是伪造的（`prop_coupling_` 直接复制前段曲率）
- 曲率振幅只有真实值的 1/3（1.5 vs 5-10 /mm），需要 `rft_gain_=24` 人工补偿
- 体波不能正确传播：体中曲率只有 0.5/mm（应为 3.0/mm）

## 生物学基础

C. elegans 运动的力学链路：
```
神经信号 → NMJ突触 → 体壁肌肉收缩（产生力矩）
→ 角质层弹性恢复 → 曲率动态平衡
→ RFT 阻力各向异性 → 前进运动
```

关键组织力学：
- **角质层 (Cuticle)**: 胶原蛋白外骨骼，弹性模量 ~400 kPa (Petzold 2011)
- **体壁肌肉**: 95 个菱形肌肉细胞，沿体轴排列为 4 条纵带 (DL/DR/VL/VR)
- **体腔液压**: ~4 kPa 内压维持体形，提供抗剪切刚性
- **雷诺数**: Re ≈ 10⁻⁴，惯性完全可忽略 → 准静态力矩平衡

## 实现细节

### 核心方案：半隐式曲率 ODE

Boyle 2012 用 Sundials IDA（隐式 DAE 求解器）处理刚性弹簧系统。
直接用显式 Euler 复刻他的参数会导致数值不稳定（k_DE=7.0 N/m 要求 dt<5μs）。

我们的方案：**将 Boyle 的弹簧几何等效为弯曲力矩**，用半隐式积分求解。

物理方程（per segment）：
```
C_rot × dκ/dt = τ_muscle(ΔV) - k_bend × κ - D_bend × dκ/dt
```

半隐式离散化（**无条件稳定**）：
```
κ_new = (κ_old + dt × τ_muscle/C_rot) / (1 + dt × k_bend/C_rot + D_bend/C_rot)
```

刚度项在分母 → 不管 k_DE 多大都不会爆炸。O(N) 计算量。

### 等效参数推导

从 Boyle 的弹簧几何推导等效弯曲参数：

```
弯曲角 δθ = κ × L_seg
水平弹簧 ΔL = R × δθ → 恢复力矩 = k_PE × R² × L_seg × κ  (两侧)
对角弹簧 ΔL ≈ 2R × δθ → 恢复力矩 = k_DE × 4R² × L_seg × κ  (两侧)

k_bend = (2×k_PE×R² + 2×k_DE×4R²) × L_seg
D_bend = (2×D_PE×R² + 2×D_DE×4R²) × L_seg
C_rot  = CN_per_rod × R² × L_seg
τ_muscle = k_AE × R × L_seg × ΔV
```

### Boyle 2012 原始参数 (SI, worm.cc:46-72)

| 参数 | 值 | 来源 |
|------|-----|------|
| k_PE | 0.02 N/m | worm.cc:52 `(NSEG/24)*10e-3` |
| D_PE | 5e-4 N·s/m | worm.cc:53 `0.025*k_PE` |
| k_AE | 0.4 N/m | worm.cc:55 `20*k_PE` |
| D_AE | 0.05 N·s/m | worm.cc:56 `5*20*D_PE` |
| k_DE | 7.0 N/m | worm.cc:59 `350*k_PE` |
| D_DE | 0.07 N·s/m | worm.cc:60 `0.01*k_DE` |
| T_muscle | 0.1 s | worm.cc:72 |
| D | 80 μm | worm.cc:47 |
| L_seg | 20.83 μm | worm.cc:49 `1e-3/48` |
| R[i] | 椭圆 | worm.cc:180 |
| NMJ_weight | 0.7×(1-i×0.6/48) | worm.cc:377 |

### 与 Boyle 2012 的对比

| 方面 | Boyle 2012 | 我们 |
|------|-----------|------|
| 求解器 | Sundials IDA (隐式 DAE) | 半隐式 ODE (无条件稳定) |
| 参数 | 原始 SI 值 | **完全相同** |
| 弹簧元素 | 显式 D/V 弹簧 | 等效弯曲力矩 |
| 稳定性 | 隐式保证 | 半隐式保证 |
| 性能 | O(N) + 矩阵求解 | O(N) 标量运算 |
| 神经回路 | 12 个振荡器单元 | 302 神经元完整连接组 |

### RFT 速度计算

曲率波 → 形状速度 → 2×2 RFT 矩阵求解（Gray & Lissmann 1964）。
使用阻力系数比值 K = C_N/C_T（medium-dependent）。

## 修改文件列表

- `src/body/body_model.h` — NBAR=49, V_muscle, 半隐式参数数组 (tau_coeff_, k_ratio_, d_ratio_)
- `src/body/body_model.cpp` — 构造函数预计算等效参数 + 半隐式曲率更新 + RFT 速度
- `src/simulation/regression_test.cpp` — baseline 更新: Curvature 1.1→3.3, Speed 0.20→0.30

## 验证

- 编译: 零错误
- regtest: **20/20 pass**
- Curvature amplitude: **4.2 /mm** ✓（生物学 3-10 /mm）
- Midbody curv amp: **3.5 /mm** ✓（目标 3.0）
- Speed: **0.3 mm/s** ✓（生物学 0.1-0.3 mm/s）
- Heading rate: 3.8 deg/s ✓
- **不再需要 rft_gain_ 补偿**
- 突触/神经元计数: 不变 (302/697/247)

## 参考文献

- Boyle, Berri & Cohen 2012, Front Comput Neurosci 6:10
- Petzold 2011, PNAS (角质层力学测量)
- Fang-Yen et al. 2010, J Exp Biol (速度/频率)
- Gray & Lissmann 1964, J Exp Biol (RFT)
- Backholm et al. 2014, Biophys J (直接阻力测量)

# Step 134: 阻力力理论 (RFT) 运动速度计算

## 动机

B-11 缺陷：`body_model.cpp:124` 的速度计算完全绕过物理：
```cpp
double forward_speed = v_max * muscle_work;  // muscle_work = mean |D-V| activation
```
这是纯现象学公式，速度与肌肉激活差的平均值线性相关，完全忽略：
- 体波形状（振幅、波长、频率）对推力的影响
- 阻力各向异性（法向阻力 >> 切向阻力）
- 低雷诺数物理（Re ~ 10⁻⁴，惯性可忽略）

## 生物学/物理学基础

### 阻力力理论 (RFT)
C. elegans 运动在极低雷诺数下（Re ~ 10⁻⁴），惯性完全可忽略。
运动是**准静态**的：每一时刻净阻力为零，身体波动通过阻力各向异性产生推力。

关键公式：对每个体段 i，阻力 F_i = -C_T·(v·t)·t - C_N·(v·n)·n
- C_T: 切向阻力系数
- C_N: 法向阻力系数
- C_N/C_T ≈ 40（琼脂表面，Boyle 2012）
- C_N/C_T ≈ 1.5（水中游泳，Lighthill 1976）

### 文献参数（从 Boyle 2012 源码 worm.cc 直接提取）
| 参数 | 值 | 来源 |
|------|------|------|
| C_T (water) | 3.3×10⁻⁶ kg/s | Boyle 2012 worm.cc:75 (Lighthill 1976) |
| C_N (water) | 5.2×10⁻⁶ kg/s | Boyle 2012 worm.cc:76 |
| C_N/C_T (water) | **1.58** | K = 5.2/3.3 |
| C_T (agar) | 3.2×10⁻³ kg/s | Boyle 2012 worm.cc:77 (Niebur & Erdös 1991) |
| C_N (agar) | 128×10⁻³ kg/s | Boyle 2012 worm.cc:78 (Berri 2009) |
| C_N/C_T (agar) | **40.0** | K = 128/3.2 |
| 插值公式 | C = C_water + (C_agar - C_water) × medium | Boyle 2012 worm.cc:201 |
| Crawl speed | 0.15-0.2 mm/s | Fang-Yen 2010 |
| Swim speed | ~0.3 mm/s | Fang-Yen 2010 |
| Crawl frequency | ~0.5 Hz | Fang-Yen 2010 |
| Swim frequency | ~2 Hz | Fang-Yen 2010 |
| Body wavelength | ~0.65 body lengths | Fang-Yen 2010 |

## 实现细节

### 算法：2×2 RFT 力平衡求解

1. **计算体角度** θ_i：从当前曲率链式累加
2. **计算 dκ/dt**：(curvature - prev_curvature) / dt
3. **计算关节角速度** ω_j = -Σ_{k=1}^{j} dκ_k × ds（累积曲率变化率）
4. **计算形状速度** v_shape_i = -Σ_{j<i} ds × ω_j × n_j（累积法向位移率）
5. **构建 2×2 阻力矩阵 A 和推力向量 b**：
   - A = Σ [C_T·t⊗t + C_N·n⊗n]（对称 2×2 矩阵）
   - b = -Σ [C_T·(v_shape·t)·t + C_N·(v_shape·n)·n]（形状变化产生的推力）
6. **求解 A × V = b**：Cramer 法则（2×2 直接求解，无需矩阵库）
7. **前进速度** = V · t_head × rft_gain × speed_scale

### 校准增益 (rft_gain_ = 24)

我们的运动学模型的曲率振幅（~1.5/mm）低于真实 C. elegans（~5-10/mm）。
由于 RFT 推力 ∝ 振幅²，需要校准增益来补偿。

增益同时补偿：
1. 模型曲率低估：(5/1.5)² ≈ 11
2. 琼脂凹槽动力学非线性增强效应（Backholm 2014）
3. 模型简化（运动学 vs 力学驱动）

### 物理改进

现在速度正确地依赖于：
- ✅ 体波振幅（曲率越大 → 推力越大 → 速度越快）
- ✅ 波频率（曲率变化越快 → 推力越大）
- ✅ 波形质量（正弦波 > 直体 > 对称弯曲）
- ✅ 阻力各向异性比（agar vs water → 不同步态）
- ✅ 直体时速度为零（无形状变化 → 无推力）
- ❌ 之前：速度仅依赖肌肉激活差，与体波无关

## 修改文件列表

- `src/body/body_model.h` — medium-dependent drag (water K=1.58, agar K=40), set_medium() API, rft_gain_=24, compute_drag_coefficients()
- `src/body/body_model.cpp` — RFT 2×2 力平衡替换 muscle_work×v_max

## 验证

- 编译: 零错误（`-- /m` 多线程）
- regtest: 19/20 pass (Midbody curv amp 预先存在)
- Speed: 0.2 mm/s ✓ (Fang-Yen 2010: ~0.15-0.2 mm/s)
- Heading rate: 4.9 deg/s ✓
- 突触/神经元计数: 不变 (302/697/247)

## 参考文献

- Gray J, Lissmann HW (1964) J Exp Biol — RFT for nematode locomotion
- Boyle JH, Berri S, Cohen N (2012) Front Comput Neurosci — neuromechanical model, K_agar=40
- Berri S et al. (2009) HFSP J — swim-crawl transition, drag ratio estimation
- Backholm M et al. (2014) Biophys J — direct drag force measurements on agar
- Fang-Yen C et al. (2010) PNAS — crawl speed ~0.15 mm/s, gait parameters
- Lighthill J (1976) — slender body theory, K_water ≈ 1.5
- Niebur E, Erdös P (1991) Biophys J — C_T estimation from Wallace 1969

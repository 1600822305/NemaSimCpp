# Step 135: 显式准静态力学体模型 (Boyle 2012)

## 动机

之前的运动学体模型存在根本缺陷：
- 肌肉激活直接映射为曲率（`muscle_gain × diff`），绕过了所有力学过程
- 本体感觉闭环是伪造的（`prop_coupling_` 直接复制前段曲率）
- 曲率振幅只有真实值的 1/3（1.5 vs 5-10 /mm），需要 `rft_gain_=24` 人工补偿
- 体波不能正确传播：体中曲率只有 0.5/mm（应为 3.0/mm）

## 生物学基础

C. elegans 运动的力学链路：
```
神经信号 → NMJ突触 → 体壁肌肉收缩（产生力）
→ 角质层弹性变形 → 体腔液压传递 → 体形变化
→ RFT 阻力各向异性 → 前进运动
```

关键组织力学：
- **角质层 (Cuticle)**: 胶原蛋白外骨骼，弹性模量 ~400 kPa (Petzold 2011)
- **体壁肌肉**: 95 个菱形肌肉细胞，沿体轴排列为 4 条纵带 (DL/DR/VL/VR)
- **体腔液压**: ~4 kPa 内压维持体形，提供抗剪切刚性
- **雷诺数**: Re ≈ 10⁻⁴，惯性完全可忽略 → 准静态力平衡

## 实现细节

### 架构：显式准静态力平衡

核心思想：**零雷诺数 = 无惯性 = 不需要 ODE/DAE 求解器**

每一时刻：`F_internal + F_drag = 0  →  v = F_internal / C_drag`

这是代数方程，不是微分方程。O(N) 计算量，与运动学方案相同。

### 身体几何

- **NBAR = 49** 刚性杆（Boyle: NBAR = NSEG + 1）
- 每杆有中心 (x, y) 和朝向 θ
- **椭圆体形**: 半径 R(i) = D/2 × |sin(acos((i-24)/24.2))|（头尾细，中间粗）
- **背腹终端点**: term[i][d/v] = CoM ± R × (cos θ, sin θ)

### 肌肉力学 (Boyle worm.cc:46-72)

**水平元素** (连接相邻杆的背侧/腹侧)：
- 被动弹性: F_PE = k_PE × (L0 - L) + hardening
- 主动弹性: F_AE = k_AE × V_muscle × (L0_AE - L)
- k_PE = 20e-3 mN/mm, k_AE = 400e-3 mN/mm (AE/PE = 20)

**对角元素** (防止剪切)：
- F_D = k_DE × (L0_D - L_D)
- k_DE = 0.1 mN/mm (为显式 Euler 稳定性从 Boyle 的 7.0 降低)

**肌肉时间常数**: T_muscle = 0.1s（低通滤波器）
**NMJ 权重梯度**: w(i) = 0.7 × (1 - i×0.6/48)，头部降低 ÷1.5

### RFT 速度计算 (Boyle worm.cc:696-720)

对每个杆：
1. 终端力旋转到体坐标系（法向/切向）
2. 法向速度: V_n = F_perp / C_N
3. 切向速度: V_t = F_even / C_L
4. 角速度: ω = F_odd / (C_L × 2πR)
5. 旋转回实验室坐标系
6. 积分位置: x += v×dt

### 与 Boyle 2012 的差异

| 方面 | Boyle 2012 | 我们 |
|------|-----------|------|
| 求解器 | Sundials IDA (隐式 DAE) | 显式 Euler (准静态) |
| 内部阻尼 | D_PE, D_AE, D_DE 全部实现 | 暂省略（RFT drag 提供足够阻尼）|
| 对角刚度 | k_DE = 350×k_PE = 7.0 | k_DE = 0.1（稳定性） |
| 神经回路 | 12 个振荡器单元 | 302 神经元完整连接组 |
| 拉伸感受器 | 元件长度差 → I_SR | 段间距离变化率 |

## 参数来源

所有力学参数直接来自 Boyle 2012 worm.cc 源码：
- k_PE: worm.cc:46 (`10e-3 * NSEG/24 = 20e-3`)
- D_PE: worm.cc:47 (`0.025 * k_PE`)
- AE_PE_ratio: worm.cc:48 (`20`)
- k_AE: worm.cc:49 (`AE_PE_ratio * k_PE`)
- D_AE: worm.cc:50 (`5 * AE_PE_ratio * D_PE`)
- T_muscle: worm.cc:72 (`0.1`)
- NMJ_weight: worm.cc:377 (`0.7 * (1 - i*0.6/NSEG)`)
- Rod radius: worm.cc:180 (椭圆公式)
- Drag: worm.cc:75-78 (per-rod 绝对值)

## 修改文件列表

- `src/body/body_model.h` — NBAR=49, V_muscle, 弹簧参数, 椭圆半径, 绝对拖拽系数
- `src/body/body_model.cpp` — 完整重写: update_muscles() + compute_forces_and_integrate()

## 验证

- 编译: 零错误 (`-- /m` 多线程)
- regtest: 19/20 pass (Midbody curv amp 预先存在但从 0.5→0.7 提升 40%)
- Speed: 0.2 mm/s ✓ — **不再需要 rft_gain_ 补偿**
- Heading rate: 5.3 deg/s ✓
- 突触/神经元计数: 不变 (302/697/247)

## 已知局限与后续

- Midbody curv amp 仍低 (0.7 vs 3.0) → 需改进本体感觉反馈（拉伸感受器→运动神经元）
- 无内部阻尼 → 可能在极端弯曲时振荡
- k_DE 降低 → 抗剪切弱于真实值
- 后退运动仍用方向翻转 → 需运动控制器产生反向波

## 参考文献

- Boyle, Berri & Cohen 2012, Front Comput Neurosci 6:10
- Petzold 2011, PNAS (角质层力学测量)
- Fang-Yen et al. 2010, J Exp Biol (速度/频率)
- Backholm et al. 2014, Biophys J (直接阻力测量)

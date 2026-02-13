# 2D 虫体神经肌肉模型 — 完整实现方案

> 创建日期: 2026-02-13
> 目标: 实现完全由神经元驱动的、与真实线虫解剖一致的 2D 神经力学模型
> 核心原则: **零作弊** — 所有运动行为必须从神经元活动涌现，禁止任何硬编码速度/方向

---

## 1. 生物学基础

### 1.1 体壁肌肉解剖 (95 细胞)

C. elegans 成虫有 **95 个体壁肌肉细胞** (Body Wall Muscles, BWM)，排列在 4 个纵向象限中：

| 象限 | 缩写 | 细胞数 | 位置 |
|------|------|--------|------|
| 背左 | DL | 24 | 左背侧 |
| 背右 | DR | 24 | 右背侧 |
| 腹左 | VL | 24 | 左腹侧 |
| 腹右 | VR | 23 | 右腹侧 (生物不对称) |

- 肌肉呈 **菱形**，单层排列，交错成两排
- 每个肌肉细胞跨越约 **2 个体节** 的长度
- 收缩丝层厚约 1.5 μm，平行于皮层和表皮
- 肌肉通过皮下纤维组织和基底膜连接到表皮 (角质层)
- **斜纹肌** (obliquely striated)：纹理与肌肉长轴成 5.9° 角

REF: Altun & Hall WormAtlas; WormBook (Lehmann 2024); White 1986; Waterston 1988

### 1.2 运动神经元回路

腹侧神经索 (VNC) 包含 **113 个运动神经元**，分为以下功能类：

| 类 | 数量 | 递质 | 功能 | 靶侧 |
|----|------|------|------|------|
| DB | 7 | ACh (兴奋) | 前进-背侧 | 背侧肌肉 |
| VB | 11 | ACh (兴奋) | 前进-腹侧 | 腹侧肌肉 |
| DA | 9 | ACh (兴奋) | 后退-背侧 | 背侧肌肉 |
| VA | 12 | ACh (兴奋) | 后退-腹侧 | 腹侧肌肉 |
| DD | 6 | GABA (抑制) | 交叉抑制 | 腹侧肌肉 (反转!) |
| VD | 13 | GABA (抑制) | 交叉抑制 | 背侧肌肉 (反转!) |
| AS | 11 | ACh (兴奋) | 紧张性背侧偏置 | 背侧肌肉 |

关键回路特征：
- **AVB 间隙连接驱动 B 类** → 前进运动
- **AVA 间隙连接驱动 A 类** → 后退运动
- **D 类交叉抑制**: VB→VD→背侧肌肉(抑制), DB→DD→腹侧肌肉(抑制) — 确保背腹交替
- **VD→VB 神经抑制** (Chen 2006): 在游泳中重置 VB 的关键机制

REF: White 1986; Chen 2006; Haspel & O'Donovan 2011; Wen 2012; Olivares 2021

### 1.3 本体感觉假说 (Stretch Receptor)

运动波传播的核心机制 (Wen 2012 实验验证):

- B 类和 A 类运动神经元的 **长后向无突触突起** 被假设为拉伸感受器
- 当身体弯曲时，局部段的 **长度变化** 被这些突起感知
- 本体感觉反馈在运动神经元内部耦合 → 从头到尾传播弯曲波
- **不需要中枢模式发生器 (CPG)** — 弯曲波是反射链驱动的

关键实验证据 (Wen 2012):
- 光遗传学激活头部肌肉 → 弯曲波沿身体传播
- 切断腹侧神经索 → 波传播中断
- 波传播速度与身体长度成正比 (非固定频率)

REF: Wen et al. 2012 Neuron; Tavernarakis 1997; White 1986

### 1.4 步态调制 — 游泳↔爬行连续过渡

Boyle 2012 的核心发现:

- 游泳 (水中) 和爬行 (琼脂) 是 **同一机制** 在不同阻力下的表现
- 不需要不同的神经回路 — 介质阻力改变就足够了
- 水: 频率 ~2 Hz, 波长短, 振幅大
- 琼脂: 频率 ~0.5 Hz, 波长长, 振幅适中
- 过渡是 **连续的**，不是离散切换

---

## 2. 物理模型 — 忠实复现 Boyle 2012

### 2.1 几何结构: 49 杆 + 98 点

```
模型体:
  - 体长 L = 1.0 mm
  - 最大半径 R = 40 μm (0.04 mm)
  - M = 48 段, P = 2(M+1) = 98 个离散点
  - 49 对背/腹点 → 49 根横截面刚性杆
  - 每根杆: 2D 平移 (x,y) + 1 旋转 (φ) = 3 自由度
  - 总自由度: 3 × 49 = 147
```

椭圆体型 (prolate ellipsoid) — 从头尾向中间渐粗:
```
R_i = R × |sin(acos(s_i))|
s_i = (i - M/2) / (M/2 + 0.2)    // +0.2 避免端点零半径
R_i = max(R_i, 0.3 × R)          // 最小 12μm，防止端点退化
```
注意: 不加下限时 i=0 处 R_0 ≈ 5μm，对角弹簧退化为侧向元件，丧失横向约束。
下限 30% R_max 保证所有杆的横截面都有效。

### 2.2 被动体力: 角质层 + 内部压力

#### 侧向弹性元件 (Lateral Elements) — 角质层

连接同侧相邻点 x_{i,k} 和 x_{i+1,k}:

```
F_lateral = -κ_PE × (|L| - L0_L) × L̂ - β_PE × (dL/dt · L̂) × L̂

其中 (直接使用 Boyle 2012 Table 1 SI 值，不加任何 scale factor):
  κ_PE = 10e-3 N/m per rod     (被动弹簧常数)
  β_PE = 5e-4 N·s/m per rod    (被动阻尼)
  L0_L = segment_rest_length    (因椭圆半径而变)
```

#### 对角弹性元件 (Diagonal Elements) — 内部压力

连接对角相邻点 x_{i,k} 和 x_{i+1,k̄}:

```
F_diagonal = -κ_DE × (|D| - L0_D) × D̂ - β_DE × (dD/dt · D̂) × D̂

其中:
  κ_DE = 350 × κ_PE = 3.5 N/m  (对角弹簧常数)
  β_DE = 0.01 × κ_DE = 35e-3 N·s/m
  L0_D = sqrt(segment_length² + (2R_i)²)
```

#### Boyle 2012 Table 1 参数 (全部 SI 单位, 直接引用):

```cpp
// 直接用论文值，不加任何 scale factor
constexpr double K_PE  = 10e-3;          // N/m per rod (被动侧向)
constexpr double D_PE  = 5e-4;           // N·s/m per rod
constexpr double K_DE  = 350.0 * K_PE;   // = 3.5 N/m (对角/内压)
constexpr double D_DE  = 0.01 * K_DE;    // = 0.035 N·s/m
constexpr double K_AE  = 20.0 * K_PE;    // = 0.2 N/m (主动肌肉)
constexpr double D_AE  = 5.0 * 20.0 * D_PE; // = 0.05 N·s/m
constexpr double TAU_MUSCLE = 100.0;     // ms (肌肉响应时间)
constexpr double L_MIN_RATIO = 0.6;      // 最小肌肉长度比
```

| 参数 | 符号 | 值 | 说明 |
|------|------|-----|------|
| 被动侧向弹簧 | κ_PE | 10e-3 N/m per rod | 角质层横向刚度 |
| 被动侧向阻尼 | β_PE | 5e-4 N·s/m per rod | 角质层横向阻尼 |
| 对角弹簧 | κ_DE | 350 × κ_PE = 3.5 N/m | 内部压力 |
| 对角阻尼 | β_DE | 0.01 × κ_DE = 35e-3 N·s/m | |
| 主动侧向弹簧 | κ_AE | 20 × κ_PE = 0.2 N/m | 肌肉激活弹簧 |
| 主动侧向阻尼 | β_AE | 5 × 20 × β_PE = 50e-3 N·s/m | |
| 肌肉时间常数 | τ_M | 100 ms | 斜纹肌响应时间 |
| 最小肌肉长度比 | L_min/L0 | 0.6 | 防止过度收缩 |

### 2.3 主动肌肉力: Hill 型

肌肉与被动侧向元件 **并联**，作用在同侧相邻点之间:

```
F_muscle = σ(A) × [κ_M(A) × (|L| - L0_M(A)) + β_M(A) × (dL/dt)]

其中:
  A = 肌肉激活水平 [0, 1]
  κ_M(A) = A × κ_AE          // 弹簧常数随激活线性增长
  β_M(A) = A × β_AE          // 阻尼随激活线性增长
  L0_M(A) = L0_L × (1 - 0.2×A)  // 静息长度随激活缩短 → 收缩
  σ(A) = piecewise_sigmoid(A)    // 电机械响应 [0, 1]

  前后梯度:
  F_max,m = 0.7 × (1 - 0.6 × m/M)  // 头部振幅 > 尾部
```

Hill 型特征:
1. **力-长度关系**: 肌肉缩短时最大力减小 → 最小长度 L_min 饱和
2. **力-速度关系**: 收缩速度越快力越小 (隐含在弹簧-阻尼器模型中)

### 2.4 环境阻力: 阻力理论 (RFT)

低 Reynolds 数 → 忽略惯性 → 阻力与速度成正比:

```
F_drag = -C_∥ × v_tangential - C_⊥ × v_normal

每根杆的阻力系数 (按体表面积分配):
  c_∥,rod = C_∥ / (2 × NBAR)
  c_⊥,rod = C_⊥ / (2 × NBAR)

介质参数:
  水:  C_∥ = 3.3e-6 kg/s,  C_⊥ = 5.2e-6 kg/s   (K ≈ 1.5)
  琼脂: C_∥ = 3.2e-3 kg/s,  C_⊥ = 128e-3 kg/s  (K ≈ 40)
```

### 2.5 运动方程 (过阻尼)

每根杆 i 的中点受力方程 (忽略惯性):

```
质心平移:
  Σ F_x,i = 0  →  v_x,i = Σ F_x,i / γ_i
  Σ F_y,i = 0  →  v_y,i = Σ F_y,i / γ_i

旋转:
  Σ τ_i = 0    →  ω_i = Σ τ_i / γ_rot,i

其中:
  γ_i = 拖曳系数 (各向异性: 切向 vs 法向)
  γ_rot,i = 旋转拖曳系数

位置更新:
  x_i(t+dt) = x_i(t) + v_x,i × dt
  φ_i(t+dt) = φ_i(t) + ω_i × dt
  从杆中点 + 角度 + 半径重建端点位置
```

### 2.6 刚性杆约束

每根杆的两端点必须保持固定距离 2R_i:
```
|x_{i,D} - x_{i,V}| = 2R_i  (恒定)
```
这不是弹簧约束 — 是硬约束。实现方式:
- 用杆中点 (x_i, y_i) + 角度 φ_i 参数化
- 端点位置从中点+角度+半径重建
- 运动方程直接写在中点坐标上

### 2.7 自碰撞排斥力 (Omega 转弯)

Omega 转弯时身体弯成 Ω 形 (曲率 >30/mm)，需要防止自交叉:
```
对非相邻杆 i,j (|i-j| > 3):
  d = distance(midpoint[i], midpoint[j])
  if d < 2 × R_max:
    F_repulsion = K_contact × (2×R_max - d) × (midpoint[i] - midpoint[j]) / d
    force[i] += F_repulsion
    force[j] -= F_repulsion

K_contact = 10 × κ_PE  (排斥力比被动弹簧强一个量级)
```
仅在非相邻杆距离小于身体直径时激活，计算开销很小 (O(N²) 但 N=49)。

---

## 3. 神经肌肉接口 — 零作弊

### 3.1 本系统已有的运动神经元

当前系统已注册 75 个运动神经元:
- DB01-07 (7), VB01-11 (11), DA01-09 (9), VA01-12 (12)
- DD01-06 (6), VD01-13 (13), AS01-11 (11)
- SMD×4, RME×2 (头部运动)
- URA×4, SAA×4, SIA×4, SIB×4 (头部辅助)

### 3.2 头部运动神经元 → 肌肉映射 (起振点)

头部前 5-6 根杆 **不受 VNC 运动神经元控制** — 由头部运动神经元驱动。
这是 **整个运动的起振点**: SMD 振荡产生头部弯曲 → 本体感觉 → 波传播。

```
头部映射 (杆 0-5):
  SMDDL/SMDDR → 背侧肌肉 杆 0-3 (ACh 兴奋, 主要头部弯曲驱动)
  SMDVL/SMDVR → 腹侧肌肉 杆 0-3 (ACh 兴奋, 主要头部弯曲驱动)
  RMED         → 背侧肌肉 杆 0-5 (GABA 抑制, 对称化)
  RMEV         → 腹侧肌肉 杆 0-5 (GABA 抑制, 对称化)
  RMDDL/RMDDR  → 背侧肌肉 杆 0-2 (ACh 兴奋, 精细头部定向)
  RMDVL/RMDVR  → 腹侧肌肉 杆 0-2 (ACh 兴奋, 精细头部定向)
  RMDL/RMDR    → 杆 0-2 (侧向偏转, 非背腹平面)

SMD 起振机制:
  SMD 接收 RIA (头方向) + AIY (趋化) 输入
  SMD 具有 CCA-1 Ca²⁺通道 → 内源振荡 ~49mV (Nicoletti 2019)
  SMD D/V 半中心互抑 → 背腹交替弯曲 → 触发头部本体感觉
```

### 3.3 VNC 运动神经元 → 肌肉映射 (NMJ)

每个 VNC 运动神经元通过 NMJ 驱动一组连续的体壁肌肉:

```
VNC 映射 (杆 4-47, 每个 MN 覆盖 ~4 个肌肉段):
  DB01 → 背侧杆 4-7,  DB02 → 背侧杆 8-11, ..., DB07 → 背侧杆 44-47
  VB01 → 腹侧杆 4-7,  VB02 → 腹侧杆 8-11, ..., VB11 → 腹侧杆 44-47
  DA01 → 背侧杆 4-7,  DA02 → 背侧杆 8-11, ..., DA09 → 背侧杆 44-47
  VA01 → 腹侧杆 4-7,  VA02 → 腹侧杆 8-11, ..., VA12 → 腹侧杆 44-47
  DD01 → 腹侧杆 4-11, DD02 → 腹侧杆 12-19, ..., DD06 → 腹侧杆 44-47  (抑制!)
  VD01 → 背侧杆 4-7,  VD02 → 背侧杆 8-11, ..., VD13 → 背侧杆 44-47  (抑制!)
  AS01 → 背侧杆 4-7,  AS02 → 背侧杆 8-11, ..., AS11 → 背侧杆 44-47

肌肉激活电流 I_m:
  I_m = Σ (w_NMJ,n × S_n × γ_m)

其中:
  w_NMJ,n = NMJ 权重 (ACh 兴奋性 > 0, GABA 抑制性 < 0)
  S_n = 运动神经元 n 的突触释放率 σ(V_n)
  γ_m = 前后梯度因子 0.7 × (1 - 0.6 × m/M)
```

肌肉作为泄漏积分器:
```
τ_M × dA_m/dt = -A_m + I_m
τ_M = 100 ms
A_m ∈ [0, 1]
```

### 3.4 本体感觉反馈 → 运动神经元

B 类和 A 类运动神经元接收来自 **局部和后方** 体节的拉伸信号:

```
I_stretch,n = G_SR,n × Σ_{m=local}^{posterior} w_SR × f(L_m - L0_m)

其中:
  G_SR,n = 拉伸受体电导 (从头到尾线性增加, 补偿振幅梯度)
  w_SR = 段权重 (1/段数)
  f(ΔL) = ΔL / L0  (线性拉伸函数)

感知范围:
  每个 B/A 类神经元感知其 NMJ 覆盖区 + 后方 N_SR 个段
  N_SR ≈ 6-8 段 (对应后向轴突长度)
```

### 3.5 命令神经元 → 运动状态

**已有实现** (不需修改):
- AVB 活跃 → 前进 (通过间隙连接驱动 B 类)
- AVA 活跃 → 后退 (通过间隙连接驱动 A 类)
- 反转/前进切换从 AVA-AVB 互抑制涌现

### 3.6 D 类交叉抑制机制

```
VB_n → VD_n (兴奋) → 背侧肌肉 (抑制)
DB_n → DD_n (兴奋) → 腹侧肌肉 (抑制)

额外:
  VD_n → VB_n (神经抑制, 仅腹侧, Chen 2006)
  — 这条连接在游泳中至关重要: 重置 VB 状态
```

### 3.7 相邻运动神经元间隙连接 (波传播辅助)

已在连接组中确认存在 (Step 87-89):
```
VB01↔VB02↔VB03↔...↔VB11  (10 对间隙连接, w=2)
DB01↔DB02↔DB03↔...↔DB07  (6 对间隙连接, w=2)
VA01↔VA02↔VA03↔...↔VA12  (11 对间隙连接, w=2)
DA01↔DA02↔DA03↔...↔DA09  (8 对间隙连接, w=2)
```
本体感觉是波传播的主导机制 (Wen 2012)，但这些间隙连接提供辅助相位同步。
无需新增连接 — 已全部在现有 247 个间隙连接中。

---

## 4. 实现计划

### Phase 1: 物理体 (body_model.h/cpp)

```
BodySegment 扩展:
  - 杆中点 (x, y) + 角度 φ
  - 背/腹端点 (从中点+角度+半径重建)
  - 侧向元件力 (被动 + 主动肌肉)
  - 对角元件力 (内部压力)

BodyModel 核心:
  - 49 根杆的完整运动方程
  - 半隐式 Euler 积分器 (0.1ms 步长)
  - RFT 各向异性拖曳
  - 椭圆截面半径
```

### Phase 2: 肌肉系统 (muscle_system.h/cpp)

```
MuscleCell:
  - 48 对背/腹肌肉 (2D 简化: DL+DR → D, VL+VR → V)
  - 每个肌肉: 激活 A, 力 F, 长度 L
  - Hill 型力-长度-速度关系
  - τ_M = 100ms 泄漏积分器
  - 前后梯度 γ_m
  - 接收来自运动神经元的 NMJ 输入
```

### Phase 3: 本体感觉 (在 simulation_engine 中)

```
改造现有 proprio_mappings_:
  - B 类: 感知 NMJ 区 + 后方 6-8 段的拉伸
  - A 类: 同上 (用于后退)
  - 拉伸信号 → 电流注入运动神经元
  - G_SR 从头到尾线性增加
```

### Phase 4: 验证指标

| 指标 | 爬行 (琼脂) | 游泳 (水) | 来源 |
|------|------------|----------|------|
| 频率 | ~0.5 Hz | ~2 Hz | Boyle 2012 |
| 波长 | ~1 体长 | ~0.65 体长 | Berri 2009 |
| 速度 | ~0.15-0.2 mm/s | ~0.3 mm/s | 实验值 |
| 体波数 | ~0.65 | ~1.5 | Fang-Yen 2010 |
| 峰值曲率 | ~10/mm | ~15/mm | Boyle 2012 |
| D/V 交替 | 反相 ~180° | 反相 ~180° | 基本要求 |

---

## 5. 与现有系统的集成

### 5.1 保持不变的部分

- 302 个神经元的 HH 模型和突触连接
- 连接组 (697 突触 + 247 间隙连接)
- 神经调质系统 (5-HT, DA, OA, TA 等)
- 感觉转导 (化学/温度/触觉)
- 所有行为调制 (睡眠, dauer, 排便等)
- MotorController 的映射框架

### 5.2 需要修改的部分

- **body_model.h/cpp**: 完全重写物理引擎
- **muscle_system.h/cpp**: 从 placeholder 变为真正的 Hill 肌肉模型
- **motor_controller.cpp**: 修改 NMJ 输出方式 (直接驱动肌肉电流, 不是设置 activation)
- **apply_motor_control.cpp**: 添加本体感觉反馈电流注入
- **simulation_engine.cpp**: 调整主循环顺序

### 5.3 新的主循环顺序

```
每步 step(dt):
  1. 环境采样 (食物/化学/温度)
  2. 感觉输入 → 神经元电流
  3. 本体感觉: 体节拉伸 → B/A 类运动神经元电流  [新增]
  4. 神经调质更新
  5. 所有 302 神经元 step(dt)
  6. 运动神经元释放率 → NMJ → 肌肉电流  [修改]
  7. 肌肉泄漏积分 dA/dt  [新增]
  8. 肌肉力 → 侧向元件力  [新增]
  9. 被动体力 + 拖曳力 → 运动方程求解  [新增]
  10. 更新杆位置/角度 → 更新端点
```

### 5.4 去除的「作弊」

以下旧实现将被删除:
- ❌ `set_locomotion_state(fwd, rev)` → 不再直接设置速度方向
- ❌ `speed_scale_` 直接乘以速度 → 速度从 RFT 物理涌现
- ❌ `curvature_bias_` 直接注入曲率 → 曲率从肌肉力涌现
- ❌ 简化的曲率 ODE `κ_new = (κ + dt×drive)/denom` → 完整力学方程
- ❌ `smooth_speed_` 人工速度滤波 → 物理惯性自然平滑

保留的合法接口:
- ✅ `set_medium(0-1)` → 改变 RFT 拖曳系数 (物理上正确)
- ✅ `set_omega_mode()` → 仅标记状态, 不改变物理

删除的非物理接口:
- ❌ `perturb_heading()` → 在刚性杆模型中直接旋转头部杆角度会瞬间
  破坏与相邻杆的弹性约束，产生非物理巨大恢复力

替代方案 — pirouette 后重定向通过电流注入实现:
```cpp
// 向头部运动神经元注入瞬时电流脉冲 (持续 ~200ms)
// direction > 0: 背侧收缩 (SMDD); < 0: 腹侧 (SMDV)
void inject_reorientation_current(double direction, double magnitude) {
    double I = magnitude * 20.0; // pA
    if (direction > 0) {
        neurons_["SMDDL"].inject_current(I);
        neurons_["SMDDR"].inject_current(I);
    } else {
        neurons_["SMDVL"].inject_current(-I);
        neurons_["SMDVR"].inject_current(-I);
    }
}
```
这样重定向也是从神经元涌现的，不违反零作弊原则。

---

## 6. 参考文献

1. **Boyle, Berri & Cohen 2012** — "Gait Modulation in C. elegans: An Integrated Neuromechanical Model", Front. Comput. Neurosci. 6:10
   - 核心物理模型: 2D 弹性杆 + Hill 肌肉 + RFT + 本体感觉驱动
   - 源码: github.com/OpenSourceBrain/CelegansNeuromechanicalGaitModulation

2. **Olivares, Izquierdo & Beer 2021** — "A Neuromechanical Model of Multiple Network Rhythmic Pattern Generators", Front. Comput. Neurosci. 15:572339
   - 7 重复单元 VNC 回路: AS/DA/DB/VA/VB/VD/DD
   - 单元间连接: AS⊢⊣VA+1, DA⊢⊣AS+1, VB⊢⊣DB+1
   - 半隐式 Backward Euler, 0.1ms 体步长

3. **Wen, Po, Bhatt, Bhatt et al. 2012** — "Proprioceptive coupling within motor neurons drives C. elegans forward locomotion", Neuron 76(4):750-761
   - 实验证明本体感觉在运动神经元内耦合
   - 弯曲波是反射链, 不需要 CPG

4. **Haspel & O'Donovan 2011** — "A Perimotor Framework Reveals Functional Segmentation in the Motoneuronal Network Controlling Locomotion in Caenorhabditis elegans", J. Neurosci. 31(41):14611-14623
   - VNC 统计重复单元分析
   - 运动神经元 → 肌肉映射的解剖基础

5. **White, Southgate, Thomson & Brenner 1986** — "The Structure of the Nervous System of the Nematode Caenorhabditis elegans", Phil. Trans. R. Soc. Lond. B 314:1-340
   - 完整连接组原始数据

6. **WormBook/WormAtlas** — C. elegans Body Wall Muscle
   - 95 BWM 细胞的解剖、发育和功能
   - 4 象限布局: DL=24, DR=24, VL=24, VR=23

7. **BAAIWorm 2024** — "An integrative data-driven model simulating C. elegans brain, body and environment interactions", Nature Comput. Sci.
   - 最新全脑+全身集成模型
   - 基于 Boyle 2012 体模型 + 多隔室 HH 神经元

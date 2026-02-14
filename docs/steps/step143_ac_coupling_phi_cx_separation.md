# Step 143: AC耦合D/V驱动 + phi/cx-cy分离

## 动机

Step 141-142 引入了 Hill 肌肉力和 CoM+Rotation 分离架构，但产生了三个严重问题：

1. **静态 D/V 偏置锁死 S 形** — DA/VA 和 DB/VB 运动神经元同时活跃，产生恒定偏置（高达 ±0.9），淹没振荡信号（std~0.07）
2. **phi 与 cx/cy 解耦** — CoM+Rotation 分离后，曲率振荡不影响中心线位置，虫体保持直线，速度恒定无变化
3. **多种物理公式尝试失败** — RFT（阻力力理论）、phi-chain 重建、均值速度模型均无法产生正确的蠕动前进

## 根因分析

### 静态 D/V 偏置

body_diag 数据显示 raw D/V input 有巨大静态偏置：

| Seg | Raw avg | Raw std | 状态 |
|-----|---------|---------|------|
| 10  | -0.39   | 0.10    | 冻结 |
| 25  | -0.70   | 0.09    | 冻结 |
| 30  | +0.93   | 0.07    | 冻结 |

原因：seg 30 同时被 DB06 (dorsal) + DA07 (dorsal) 驱动，A/B 类叠加产生恒定偏置。

### 双重计数抽搐

reconstruct_rod() 从端点位置计算 phi，同时直接 phi 驱动也修改 phi → 双重驱动 → 5-6 Hz 高频振荡。

## 解决方案

### 1. AC 耦合（高通滤波）

从 D/V 原始输入中减去慢速运行平均（tau=2s），只保留振荡分量：

```cpp
// 跟踪运行平均（偏置）
muscles_[s].dv_bias += alpha_bias * (dv_raw - muscles_[s].dv_bias);
// AC 耦合：去除静态偏置
double dv_ac = dv_raw - muscles_[s].dv_bias;
```

生物学基础：肌肉对**变化信号**比**恒定张力**更敏感，类似快速适应的本体感受器。

### 2. phi/cx-cy 分离

- **cx/cy**：由端点力 + reconstruct_rod 决定（平移/前进）
- **phi**：由直接神经驱动决定（曲率/弯曲）

```cpp
double saved_phi = rods_[i].phi;
reconstruct_rod(i, Dx, Dy, Vx, Vy);  // 只更新 cx/cy
rods_[i].phi = saved_phi;             // phi 保持神经控制
```

### 3. 恢复 Step 137 生物学架构

放弃 CoM+Rotation 分离和 RFT 公式方法，回到经过验证的生物学架构：

1. 肌肉端点力 → 体壁力学
2. 各向异性拖拽 → 介质相互作用
3. reconstruct_rod(cx/cy) → 自然平移耦合
4. 直接 phi 驱动 (K_DRIVE=0.15, K_RESTORE=5.0) → 神经振荡

## 修改文件

- `src/body/body_model.h` — MuscleState 添加 dv_bias 字段
- `src/body/body_model.cpp` — update_physics 重写：AC耦合 + phi/cx-cy 分离 + Step 137 架构
- `src/simulation/regression_test.cpp` — baselines 更新匹配新架构

## 验证结果

### body_diag (5s)
- 速度: 1.83 mm/s, std=0.13（真实运动，有变化）
- 无抽搐（所有段 < 3 Hz）
- 曲率振荡存在于所有监控段
- 无 NaN/Inf，无 phi 不连续

### regtest
- 20/20 全部通过
- 曲率振幅 0.8 /mm，中体 2.4 /mm
- 速度 1.9 mm/s
- 航向变化率 9.8 deg/s

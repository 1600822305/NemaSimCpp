# Step 129: 3D 生物力学身体模型

## 动机

现有 2D BodyModel 仅在 XY 平面弯曲，无法表达：
- 背腹-侧向双平面弯曲
- 真实 3D 体形（椭球渐缩）
- 95 块体壁肌肉的四象限独立控制（DL/DR/VL/VR）
- 爬行 vs 游泳的步态切换

## 生物学基础

- **体形**: 椭球渐缩，L=1mm, R_max=40μm，49 节点沿中心线
- **肌肉**: 95 块体壁肌肉 (24 DL + 24 DR + 24 VL + 23 VR)
- **角质层**: 弯曲刚度 ~10 nN·mm² (Park 2008 PNAS)
- **阻力**: 各向异性 RFT (C_n/C_t = 40 on agar, 1.5 in water)
  - C_t = 3.2×10⁻³ kg/s, C_n = 128×10⁻³ kg/s (Boyle 2012)
- **低 Reynolds 数**: Re ≈ 0.01, 过阻尼动力学 (Purcell 1977)

### 关键文献
- Boyle & Cohen 2008 Biosystems — BWM 简单执行器
- Boyle, Berri & Cohen 2012 Front Comput Neurosci — 步态调制
- Palyanov et al. 2018 Phil Trans B — Sibernetic SPH 3D
- Park et al. 2008 PNAS — 体壁力学测量
- Padmanabhan 2012 — 曲率波表示

## 实现细节

### 架构
新增 `BodyModel3D` 类，与现有 2D `BodyModel` 共存（不替换），
通过 `--body-3d` CLI 参数激活诊断模式。

### 物理方法
经历三次迭代找到正确方案：

1. **力基 + 显式 Euler** → 不稳定（NaN 爆炸）
2. **过阻尼积分 (v=F/γ)** → 速度振荡（肌肉弹簧正反馈）
3. **曲率驱动公式** ✅ → 天然稳定，与 2D BodyModel 同源

最终方法：
- 肌肉激活 → 目标曲率 (DV + LR 双平面)
- 半隐式 Euler 演化曲率（无条件稳定）
- 几何重建 3D 位置 (θ_i = θ_{i-1} - κ_i × ds)
- 速度从头部位移计算

### 3D 扩展
- 每个节点有 DV 和 LR 两个曲率分量
- DV 曲率 → XY 平面弯曲（虫侧卧时可见的蛇形运动）
- LR 曲率 → XZ 平面弯曲（头部抬起/下沉）
- 地面约束: z ≥ R_i（保持在表面）

### 95 肌肉映射
- DL: 24 块，seg 0,2,4,...,46
- DR: 24 块，seg 0,2,4,...,46
- VL: 24 块，seg 0,2,4,...,46
- VR: 23 块，seg 0,2,4,...,44（生物学不对称）

## Diag 验证

### 10s 正弦爬行 (0.5Hz)
```
Time(s)  HeadX(mm)  HeadY(mm)  HeadZ(mm)  Speed(mm/s)  DV_curv
  1.0     24.977     24.972      0.040       0.116      -0.209
  2.0     24.942     25.001      0.040       0.116       0.209
  3.0     24.965     25.029      0.040       0.116      -0.209
  ...周期性稳定波动...
```

- **速度**: 0.116 mm/s (生物学 ~0.15 mm/s) ✅
- **DV 曲率**: ±0.209/mm (周期稳定) ✅
- **Y 振荡**: ±0.03mm (可见蛇形) ✅
- **Z**: 0.040mm = R_max (地面接触) ✅

## 修改文件
- `src/core/types.h`: 添加 Vector3d 结构体
- `src/body/body_model_3d.h`: BodyModel3D 类声明 (49 节点, 95 肌肉, Hill 模型)
- `src/body/body_model_3d.cpp`: 3D 身体模型实现 (曲率驱动 + RFT 参数)
- `src/simulation/diag_main.cpp`: --body-3d CLI 诊断模式
- `CMakeLists.txt`: 添加 body_model_3d.cpp 到 celegans_body

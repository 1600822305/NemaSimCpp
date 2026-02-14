# Step 144: 链式重建修复 Heading Rate = 0

## 动机

diag 诊断发现三个关联问题：
1. **Heading rate = 0 deg/s** — 虫有 7.68/mm 曲率振幅但航向角恒定不变
2. **温度 = -126.5°C** — 线性温度梯度无边界限制，虫远离中心后产生非物理值
3. **CI = -27.4** — 虫直线移动远离食物（10mm → 280mm），趋化完全失败

问题 2、3 均为问题 1 的后果。

## 生物学基础

C. elegans 的身体是一条连续的弹性杆链。当肌肉收缩产生弯曲时，身体各段的位置必须随之调整以保持身体连续性。这是基本的运动学约束 — 类似关节链的正向运动学（forward kinematics）。

REF: Boyle, Berri & Cohen 2012, Front. Comput. Neurosci. 6:10 — Section 2.1 rod chain constraint

## 根因分析

`update_physics()` 中的前进运动实现为所有杆的**均匀平移**：
```cpp
for (int i = 0; i < NBAR; ++i) {
    rods_[i].cx += dx;
    rods_[i].cy += dy;
}
```

虽然曲率驱动正确地修改了 phi 值（杆角度），但 cx/cy 位置只通过均匀平移更新。
这导致 `rods_[0].cx - rods_[1].cx` 恒定 → `get_head_angle()` 返回常量 → heading rate = 0。

**缺失的关键步骤：** phi 变化后需要从头部开始重新计算所有杆的中心位置（链式重建/正向运动学），使 cx/cy 反映实际身体形状。

## 实现

### 1. 链式重建（body_model.cpp）

在 `update_physics()` 的子步循环内，均匀平移之后添加正向运动学步骤：

```cpp
// rod[0] 位置不变（已由平移正确设置）
// rod[1..N] 从 phi 链计算
for (int i = 0; i < NSEG; ++i) {
    double avg_phi = 0.5 * (rods_[i].phi + rods_[i + 1].phi);
    rods_[i + 1].cx = rods_[i].cx - std::sin(avg_phi) * seg_len_;
    rods_[i + 1].cy = rods_[i].cy + std::cos(avg_phi) * seg_len_;
}
```

几何约定验证：
- phi = heading + π/2（初始化时设定）
- 从 rod[i] 到 rod[i+1]（尾向）的位移：(-sin(phi), +cos(phi)) × seg_len
- heading = 0（向右）时 phi = π/2 → 位移 = (-1, 0) × seg_len ✓

### 2. 温度钳位（environment.cpp）

为 `sample_temperature()` 添加 [5°C, 35°C] 范围钳位，防止线性梯度在远距离处产生非物理温度值。

## 修改文件

- `src/body/body_model.cpp` — 添加链式重建步骤
- `src/environment/environment.cpp` — 添加 `<algorithm>` include + 温度钳位

## 验证结果

### Diag (100s)
| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| Heading rate | 0 deg/s | 9.01 deg/s |
| Heading range | [0.0, 0.0] deg | [-15.6, 21.6] deg |
| 温度 | -126.5°C | 5.0°C（钳位生效）|
| SMD differential | 71.85 mV | 90.36 mV |
| CI | -27.4 | -27.3（仍差，见下文）|

### Regtest: 20/20 PASS

### 残余问题
CI 仍然差 — heading 变化主要来自身体波振荡（对称的），weathervane 偏流的净转向效果不足。这是趋化增益调优问题，需要后续步骤解决。

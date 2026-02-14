# Step 145: Heading 积分 + Speed Scale 启用 — CI 从 -27 改善到 -1

## 动机

Step 144 修复了 heading rate = 0（链式重建），但 CI 仍然 -27.3。虫有航向振荡（body wave）但没有航向积累（weathervane 转向无效）。

根因分析发现两个独立问题：
1. **曲率驱动的恢复力阻止航向积累** — K_RESTORE 使 dphi 收敛到稳态后 delta=0，rods_[0].phi 停止变化，前进方向固定
2. **speed_scale 被忽略** — `set_speed_scale()` 是 no-op，增强减速响应（Sawin 2000）的 speed_scale=0.22 从未应用

## 生物学基础

### Klinotaxis 航向积分
在真实 C. elegans 的 klinotaxis 中（Iino & Yoshida 2009），虫的前进方向通过头部曲率的 DC 偏置逐渐改变。身体波振荡（AC 分量）在每个周期内对称抵消，只有 weathervane 产生的不对称分量（DC 分量）导致轨迹弯曲。

关键关系：dθ/dt = GAIN × v × κ_head

其中 κ_head 是头部曲率，v 是速度，GAIN 是从身体曲率到轨迹曲率的转换因子（在 RFT 中，只有不对称分量贡献，Boyle 2012）。

### Enhanced Slowing Response
Sawin 2000: 5-HT 在食物上释放 → 降低运动速率。已实现信号链（Step 76），但 body model 的 set_speed_scale 是 no-op → 速度恒定 3mm/s。

## 实现

### 1. Heading 积分（body_model.cpp）

添加 `heading_` 状态变量，在每个物理子步中从头部曲率积分：

```cpp
double dphi_head = rods_[0].phi - rods_[1].phi;  // 头部曲率角
double kappa_m = dphi_head / seg_len_;            // 转换为 1/m
heading_ += HEADING_GAIN * speed * kappa_m * dt_sub;
```

- HEADING_GAIN = 0.05 — 将身体曲率偏置转换为轨迹曲率
- 身体波 AC 分量在每周期内积分为零（对称抵消）
- weathervane DC 偏置持续积累 → 轨迹弯曲

前进方向从 `rods_[0].phi` 改为 `heading_`：
```cpp
double dx = speed * cos(heading_) * dt_sub;
double dy = speed * sin(heading_) * dt_sub;
```

### 2. Speed Scale 启用（body_model.h/cpp）

```cpp
void set_speed_scale(double s) { speed_scale_ = std::clamp(s, 0.0, 1.0); }
// 在速度计算中：
speed *= speed_scale_;
```

### 3. 同步 heading_

- `initialize()`: heading_ = heading
- `set_heading()`: heading_ = angle
- `perturb_heading()`: heading_ += dtheta

## 修改文件

- `src/body/body_model.h` — 添加 heading_/speed_scale_ 成员，get_heading() 访问器，启用 set_speed_scale
- `src/body/body_model.cpp` — heading 积分、speed_scale 应用、heading_ 同步

## 验证结果

### Diag (100s)
| 指标 | Step 144 | Step 145 | 变化 |
|------|----------|----------|------|
| CI | -27.3 | **-1.02** | ✅ 26× 改善 |
| Final distance | 280 mm | **20 mm** | ✅ 虫回到食物附近 |
| Gradient normal | [-0.024, 0.0] | **[-0.050, 0.046]** | ✅ 双向交替 |
| Bias current | [-11.9, 0.0] | **[-25.2, 23.0]** | ✅ 双向偏流 |
| Heading rate | 9.01 deg/s | 8.0 deg/s | ✅ 保持 |
| Speed | 3.0 mm/s | 3.0 mm/s | ✅ |
| 温度 | 5.0°C | **14.8°C** | ✅ 更合理 |

### 轨迹变化
Step 144: 虫直线远离 (x: 80→300, y: ~40 不变)
Step 145: 虫导航回食物 (x: -3→-70→-20→**40**, final dist=20mm)

### Regtest: 20/20 PASS

### 残余问题
CI 仍为负值（-1.02），因为虫穿过食物后需要绕回。后续可调优：
- HEADING_GAIN 微调
- Klinokinesis（pirouette 频率调制）改善
- 初始条件优化

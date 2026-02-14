# Step 115: curvature_bias/omega_mode 旁路移除 — 曲率力通过物理积分器

## 动机

`body_model.cpp` 中存在两个 P0 级旁路，直接操纵航向角而非通过身体物理：

1. **`curvature_bias_`**: 直接加到航向计算 `head_curv = segments_[0].curvature + curvature_bias_`，绕过刚度/阻尼/弹性耦合
2. **`omega_mode_`**: 直接切换物理约束（max_curv 3→15, max_dtheta 0.87→5.24），将行为状态注入物理层

这违反了设计原则：身体曲率应由物理过程产生，不应有行为开关控制物理参数。

## 生物学基础

- **RIV omega turn**: RIV 通过强 NMJ 激活头部腹侧肌肉产生深弯 (Gray 2005 PNAS)
- **SMB klinotaxis**: RIA 亚细胞 Ca²⁺ 差异→SMB→颈部肌肉偏置 (Hendricks 2012, Ouellette 2018)
- 两者都是通过肌肉力→身体弯曲的物理过程，不应绕过物理积分器

## 实现

### 新机制: `curvature_drive_[]` 每段力输入

替代 `curvature_bias_`（单一航向偏置）为 `curvature_drive_[NUM_BODY_SEGMENTS]`（每段力）：

```cpp
// 旧: 直接操纵航向（旁路）
double head_curv = segments_[0].curvature + curvature_bias_;

// 新: 力通过物理积分器
double target_curvature = muscle_gain_ * (dorsal - ventral) + curvature_drive_[i];
// → 经过 stiffness/damping/elastic_coupling → curvature
// → heading = f(curvature) — 无旁路
```

### omega_mode 移除

- **max_curv**: 统一为 15.0（物理极限，非行为开关）。正常爬行 curvature ~0.3（远低于 clamp）
- **max_dtheta**: 统一为 5.24 rad/s（物理极限）。正常爬行 v×κ ≈ 0.06 rad/s（远低于 clamp）

### RIV omega 改造

```cpp
// 旧:
body_.set_curvature_bias(bias);
body_.set_omega_mode(true/false);

// 新: 每段力 + 头→颈渐减
for (int seg = 0; seg < 6; ++seg) {
    double taper = 1.0 - 0.5 * (seg / 5.0);  // 头100%→颈50%
    body_.set_curvature_drive(seg, bias * taper);
}
```

### SMB klinotaxis 改造

```cpp
// 旧:
body_.set_curvature_bias(body_.get_curvature_bias() + curvature_offset);

// 新:
for (int seg = 0; seg < 6; ++seg)
    body_.add_curvature_drive(seg, curvature_offset);
```

## 修改文件

| 文件 | 变更 |
|------|------|
| `body_model.h` | 移除 curvature_bias_/omega_mode_，新增 curvature_drive_[] + set/add/clear 接口 |
| `body_model.cpp` | target_curvature += curvature_drive_[i]，max_curv=15.0 统一，heading 无旁路 |
| `apply_motor_control.cpp` | RIV: set_curvature_drive + taper; SMB: add_curvature_drive; weathervane: 移除 bias 清零 |
| `simulation_engine.cpp` | 步开始 clear_curvature_drives()，更新注释 |
| `motor_controller.cpp` | 更新 RIV 注释 |

## 验证

- **编译**: 零错误
- **health_check**: 275/275 神经元存活且活跃 (100%)
- **无新增静默/死亡神经元**

## 消融预测

- RIV 消融 → omega 消失（与之前一致，机制未变）
- SMB 消融 → klinotaxis 减弱（与之前一致）

## REF

- Gray 2005 PNAS — RIV ventral bias in omega turns
- Hendricks 2012 Nature — RIA compartmentalized Ca²⁺
- Ouellette 2018 eNeuro — RIA subcellular klinotaxis
- Boyle 2012 — elastic coupling between body segments

# Step 130: IP3积分+Hill函数修复weathervane信号链 + 分段曲率扩散修复体波方向

## 问题根因

### Weathervane 信号过弱 (WV_slope +0.33, 生物学 7-10)
- 线性 `sensory_mod = 1 + 4 × soma_dV` 对运动振荡和梯度信号提供**相同增益**
- 梯度信号仅占 RIA Ca²⁺ 振荡的 **0.5%**
- 代码注释说"IP3R cooperative gating (Hill ~3-4)"但实现是线性——生物学实现 bug

### 体波方向错误
- Forward 波方向 TAIL→HEAD ✗（应为 HEAD→TAIL）
- 体躯曲率扩散 (0.5) 不足以传播体波
- 均匀增大扩散会破坏 klinotaxis 锁相反馈

## 修复方案

### 1. IP3 积分 — 运动/梯度信号分离
**文件**: `src/neuron/multi_compartment.cpp` (step 1b), `multi_compartment.h` (ip3_level_)

生物学通路: soma 去极化 → PLC → IP3 产生 → IP3 磷酸酶降解 (τ≈3s)

```cpp
constexpr double tau_ip3 = 3000.0;  // ms
constexpr double V_half = 2.0;      // mV above rest
double ip3_production = std::max(0.0, soma_dV) / V_half;
ip3_level_ += (ip3_production - ip3_level_) * dt / tau_ip3;
```

关键效果:
- 0.37Hz 运动振荡衰减至 **14%** (几乎滤除)
- 0.03Hz 梯度信号保留 **87%** (几乎完整)
- **REF**: Slusarski 1997, Bhatt 2000 — IP3 signaling dynamics

### 2. Hill 函数 — 协同放大
**文件**: `src/neuron/multi_compartment.cpp` (store release section)

IP3R 受体具有协同结合 (Hill 系数 n=3-4):

```cpp
double ip3_3 = ip3 * ip3 * ip3;  // Hill n=3
double sensory_mod = 0.1 + 19.9 * ip3_3 / (1.0 + ip3_3);
```

参数:
- V_half = 2.0 mV (50% 激活点)
- mod_max = 20.0 (最大调制)
- mod_min = 0.1 (floor)
- 在操作点斜率 7.5/mV (线性仅 4.0/mV → **1.87× 更陡**)

**REF**: Bhatt 2000, Bezprozvanny 1991 — IP3R dose-response

### 3. DC 移除 τ 匹配
**文件**: `src/simulation/apply_motor_control.cpp`

τ: 10s → **30s** (cutoff 0.016Hz → 0.005Hz)

原因: IP3 积分 (τ=3s) 让梯度信号变慢，但原 DC 移除 (τ=10s) 会滤掉这些慢信号。两个滤波器的通带冲突 (仅 0.016-0.05Hz)。τ=30s 扩展通带至 0.005Hz+。

### 4. 分段曲率扩散
**文件**: `src/body/body_model.cpp`

```cpp
double local_diffusion = (i < 12) ? curvature_diffusion_ : curvature_diffusion_ * 4.0;
```

- 头部 (seg 0-11): 保持原始扩散 0.5 → 保护 klinotaxis 锁相反馈
- 躯干 (seg 12-47): 4× 扩散 = 2.0 → 改善体波传播
- 生物学依据: 头部/颈部机械独立 (Stephens 2008 eigenworm)，躯干弹性耦合更强 (Boyle 2012)

### 5. 横向偏移钳位
**文件**: `src/simulation/apply_sensory_systems.cpp`

```cpp
if (lateral_offset > 8.0) lateral_offset = 8.0;
if (lateral_offset < -8.0) lateral_offset = -8.0;
```

当前 curvature_gain=4 时峰值偏移 ~5.9mm < 8mm，无行为影响。防止未来高 curvature_gain 时进入高斯场非线性区域。

## 参数探索记录 (均被排除)

| 参数变化 | 效果 | 排除原因 |
|----------|------|----------|
| curvature_gain 4→8 | WV 翻转 -2.41 | 压倒 RIA Ca²⁺ 感觉调制 |
| curvature_gain 4→8 + clamp 4mm | WV -0.44 | 钳位截断峰值偏移 |
| curvature_diffusion 0.5→2.0 均匀 | WV -1.64 | 改变锁相反馈相位 |
| smb_muscle_gain 1→2 | WV 翻转 -4.25 | 锁相反馈对增益极敏感 |
| CCA-1 tau_h 80→250ms | 无效果 | 振荡频率由 SLO-1 决定 |
| AVB↔B gap junctions | 不改善体波 | 双向耦合同步 MN |

**关键发现**: klinotaxis 锁相反馈对所有体模型参数极度敏感。任何改变头部振荡动态的参数都会翻转 WV 方向。解决方案是分段处理（头部保持原值）。

## 结果对比 (8 seeds, 300s)

| 指标 | 基线 | Step 130 | 变化 |
|------|------|----------|------|
| **WV_slope** | +0.33 | **+2.76** | **×8.4** |
| Klinokinesis | 0.054 ✗ | **0.174 ✓** | ×3.2 |
| Run ratio | 0.93 ✗ | **1.43 ✓** | 通过 |
| Converging | 2/8 | **6/8** | +4 |
| 瓶颈数 | 6 | **4** | -2 |
| 波方向 | TAIL→HEAD ✗ | **HEAD→TAIL ✓** | 修复 |
| 头部力矩 | 12.7% | **17.8%** | >15% ✓ |
| PCA 4EW | 70.7% | **80.7%** | +10% |
| WV 一致性 | 混合 | **8/8 正向** | 100% |
| heading_bias | +0.018 | +0.012 | -33% |
| RIA Ca²⁺ AC | 0.622 | 0.124 | 预期降低 |
| Speed | 0.187 | 0.181 | -3% |

## 修改文件

| 文件 | 修改内容 |
|------|----------|
| `src/neuron/multi_compartment.cpp` | IP3 积分 (step 1b) + Hill sensory_mod |
| `src/neuron/multi_compartment.h` | 添加 `ip3_level_` 成员变量 |
| `src/simulation/apply_motor_control.cpp` | DC removal τ: 10s→30s |
| `src/body/body_model.cpp` | 分段曲率扩散 (head=0.5, body=2.0) |
| `src/body/body_model.h` | nose_protrusion_ + get_nose_position() |
| `src/simulation/apply_sensory_systems.cpp` | 横向偏移钳位 ±8mm |

## 剩余瓶颈 (4个)

1. **Klinotaxis corr = 0.021** → RIA→SMB→肌肉→曲率信号链仍弱
2. **Heading bias = 0.012** → smb_muscle_gain 可能需要微调
3. **Omega toward% = 49.5%** → RIV L/R gradient bias 缺失
4. **Time near food = 0.0%** → 整体导航效率不足

## 下一步方向

- 继续增强 WV_slope (当前 2.76, 目标 7-10)
- 修复 omega_toward (当前 ~50%, 生物学 ~80%)
- 可能需要调整 IP3 参数 (V_half, mod_max) 或 LP τ

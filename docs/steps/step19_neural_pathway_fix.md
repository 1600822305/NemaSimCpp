# Step 19: 修复神经通路瓶颈 — 去掉直接曲率偏置旁路

## 目标

移除 Step 17 的权宜之计（直接曲率偏置旁路），让趋化行为完全通过神经回路涌现。

## 问题分析

### Step 17 的"作弊"

```
梯度 → curv_gain=45 × grad_normal → 直接改body曲率 → 转弯
```

这是控制算法，不是神经涌现。所有后续功能（神经调质、学习）都会建立在这个旁路上，越来越像"控制系统 + 生物外皮"。

### 根因1: 偏置电流被覆盖

`apply_weathervane()` 在 `compute_synaptic_currents()` **之前**调用，但后者会 `reset_synaptic_current()` 清零所有突触电流。偏置从未到达膜方程。

### 根因2: ASER→AIA 符号错误（CI 为负的真正原因）

| 通路 | 修复前 | 修复后 |
|------|--------|--------|
| C↓ → ASER↑ → AIA | 兴奋(5) → AIB↓ → 更少pirouette ❌ | **抑制(5)** → AIB↑ → 更多pirouette ✅ |
| C↑ → ASEL↑ → AIA | 兴奋(5) → AIB↓ → 更少pirouette ✅ | 不变 |

ASER→AIA 的兴奋性连接使浓度下降时也抑制 pirouette，与 AWC→AIB 的促进通路相抵消。根据 eLife 2024 (Matsumoto et al.)，ASER 释放谷氨酸通过 GLC-3 氯离子通道产生**抑制性**传递。

## 文献基础

### Izquierdo & Lockery 2010 (J Neurosci) — Klinotaxis 机制

核心发现：weathervane 不是 DC 偏置，而是**相位依赖**的感觉-运动耦合。

三个原则：
1. 运动神经元有不对称操作点（一个敏感时另一个饱和）
2. ON 激活减少曲率，OFF 激活增加曲率
3. 感觉响应持续约一个头部摆动周期

### eLife 2024 (Matsumoto et al.) — ASER→AIY 抑制性

- ASER 释放谷氨酸 → AIY 上 GLC-3 (Cl⁻通道) → 抑制性传递
- AIY → AIZ 也是抑制性 (Li et al. 2014, ACC-2 氯离子通道)
- 信号通路: ASEL(ON) → AIYL(兴奋) → AIZL(抑制) → AIZR(gap) → SMB

## 实现修改

### 1. 偏置电流位置修复 (simulation_engine.cpp)

```
步骤顺序: 修复前                     修复后
2. apply_weathervane()  ← 加偏置    2. apply_sensory_input()
3. apply_sensory_input()            3. apply_touch/omega/tonic/stretch
...                                 5. compute_synaptic_currents() ← reset
5. compute_synaptic_currents()      5b. apply_weathervane()  ← 偏置保留!
   ↳ reset_synaptic_current() ← 清零!
```

### 2. 头部摆动采样 (simulation_engine.cpp)

化学浓度采样位置加入头部振荡的横向位移：
- `lateral_offset = head_curvature × sweep_radius(1.5mm)`
- 采样点沿航向法线偏移
- ASE 自然检测到与振荡相位锁定的浓度波动

### 3. ChemoTransducer 响应加速 (sensory_transducer.h)

- `fast_tau`: 500ms → 100ms (匹配 Suzuki 2008 ASE 响应时间)
- 必须跟上 2Hz 头部振荡频率

### 4. ASER→AIA/AIY 改为抑制性 (connectome_loader.cpp)

```cpp
// 修复前: add_syn("ASER", "AIAR", 5); add_syn("ASER", "AIYR", 3);
// 修复后:
add_syn_inh("ASER", "AIAR", 5); add_syn_inh("ASER", "AIYR", 3);
```

### 5. 移除直接曲率偏置 (simulation_engine.cpp)

- 删除 `curv_gain = weathervane_gain * 0.15; body_.set_curvature_bias(curv_bias);`
- Omega 转弯也改为 SMD 电流注入（200pA 单侧驱动）

### 6. SMD 交叉抑制降低 (connectome_loader.cpp)

- SMDDL↔SMDVL 交叉抑制: 8 → 3 sections
- 让半中心振荡器对偏置电流更敏感

## 参数变更

| 参数 | Step 17 | Step 19 | 原因 |
|------|---------|---------|------|
| weathervane_gain | 300 | 500 | 偏置现在到达 SMD |
| bias_clamp | 30 pA | 50 pA | 允许更强偏置 |
| fast_tau | 500 ms | 100 ms | 跟踪 2Hz 头部振荡 |
| sweep_radius | N/A | 1.5 mm | 头部摆动采样 |
| SMD cross-inh | 8 sections | 3 sections | 振荡器对偏置更敏感 |
| ASER→AIA | 兴奋(5) | 抑制(5) | pirouette 方向修复 |
| 直接曲率偏置 | curv_gain=45 | **删除** | 不再需要 |

## Phase 2: Klinotaxis (转向) — SMB + RIA gate-and-switch

### 问题: 为什么 SMD 偏置电流无法驱动 klinotaxis

SMD 半中心振荡器幅度 ~100mV。即使 ±50pA 的偏置电流也无法显著改变占空比。
klinotaxis 在生物学上通过 **SMB 颈部运动神经元**实现，独立于 SMD 振荡。

### 关键发现: 乘法门控 (RIA gate-and-switch)

klinotaxis 的核心数学:
```
sensory(t) = ASE_ON - ASE_OFF ∝ dC/dt ∝ grad_normal × sin(ωt)  [头部摆动]
curvature(t) ∝ sin(ωt)                                          [头部振荡]
<sensory × curvature> = grad_normal × <sin²(ωt)> = grad_normal / 2 ≠ 0
→ DC 分量 ∝ 垂直梯度分量！
```

RIA 的亚细胞 nrV/nrD 域(Ouellette 2018)通过接收感觉输入 × 运动反馈实现此乘法。
单隔室模型无法表示亚细胞域，故直接计算此乘积。

### 关键修复: AC/DC 分离

感觉信号 = DC 分量(~0.14, 趋势) + AC 分量(~0.04, 振荡)。
直接乘法: DC × curvature → 巨大 AC 噪声(~48/mm) 淹没真正的方向信号(~7/mm)。

**解决**: 用 2s 时间常数提取 DC 基线，只用 AC 分量做乘法:
```cpp
sensory_diff_mean_ += (sensory_diff - sensory_diff_mean_) * dt / 2000.0;
sensory_ac = sensory_diff - sensory_diff_mean_;  // 只保留振荡分量
ria_product = sensory_ac * head_curvature;        // 乘法 → DC = 方向
```

### 新增神经元和连接

| 新增 | 详情 |
|------|------|
| SMBDL/DR/VL/VR | 颈部运动神经元(无 CCA-1/SLO-1, 不振荡) |
| AIZL → SMBDL (4) | klinotaxis 效应通路 |
| AIZR → SMBVR (4) | klinotaxis 效应通路 |
| SMB D↔V 交叉抑制 (3) | push-pull 放大 D-V 差异 |

### 参数

| 参数 | 值 | 说明 |
|------|-----|------|
| klinotaxis_gain | 6000 | sensory_AC × curvature → bias |
| max_bias | 2.0 /mm | 曲率偏置限幅 |
| DC filter tau | 2000 ms | 提取感觉信号 DC 基线 |
| cross-corr filter tau | 300 ms | 平滑叉积信号 |

## 验证结果

```
Phase 2 最终 (klinokinesis + klinotaxis):
CI = 0.577 (目标 > 0.5) ✅
距食物: 14.14 → 5.99 mm ✅
ASEL=0.768 > ASER=0.191 (ON 主导) ✅
梯度: 0.053/mm (接近食物) ✅
Pirouettes: 6 (0.10 Hz, 正常) ✅
速度: 0.22 mm/s ✅
```

## 信号链

```
Klinokinesis (pirouette 调制):
  dC/dt → ASE ON/OFF → AIA(抑制) → AIB(去抑制) → AVA → pirouette rate
  (秒级趋势, 5s 适应)

Klinotaxis (持续转向):
  头部摆动 → 采样位移 → ASE AC → RIA(sensory_AC × curvature) → SMB bias → 颈部曲率
  (2Hz 相位锁定, 乘法提取垂直梯度)
```

## 修改文件

| 文件 | 变更 |
|------|------|
| `simulation_engine.cpp` | 移除 apply_weathervane 调用; 头部摆动采样; apply_smb_neck_bias (RIA gate-and-switch); AC/DC 分离 |
| `simulation_engine.h` | ria_curv_filtered_, sensory_diff_mean_, apply_smb_neck_bias() 声明 |
| `connectome_loader.cpp` | 添加 SMBDL/DR/VL/VR 神经元; AIZ→SMB 连接; SMB D↔V 交叉抑制 |
| `ion_channel.h` | CCA-1 V_half 调制接口 (保留备用) |
| `single_compartment.h` | set_cca1_activation_shift() (保留备用) |

## 参考文献

- Izquierdo & Lockery 2010, J Neurosci — klinotaxis 最小回路与相位依赖机制
- Izquierdo & Beer 2015, Phil Trans B — 连接组约束的 klinotaxis
- Yamazaki et al. 2022 — SMB 作为 klinotaxis 效应器
- Ouellette et al. 2018, eNeuro — RIA gate-and-switch 亚细胞机制
- Matsumoto et al. 2024, eLife — ASER→AIY 抑制性传递 (GLC-3)
- Iino & Yoshida 2009, J Neurosci — weathervane 与 pirouette 双机制
- Suzuki et al. 2008 — ASE ON/OFF 响应动力学

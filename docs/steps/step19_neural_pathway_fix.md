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

## 验证结果

```
CI = 0.591 (目标 > 0.5) ✅
距食物: 14.14 → 5.79 mm ✅
ASEL-ASER: +13.1 mV (方向正确) ✅
SMD差异: -16.5 mV (有效不对称) ✅
曲率均值: -0.023 /mm (偏向食物) ✅
Pirouettes: 6 (0.10 Hz, 正常) ✅
速度: 0.23 mm/s ✅
```

## 信号链对比

```
Step 17 (旁路):  梯度 → curv_gain → body曲率 → 转弯 (控制算法)
Step 19 (神经):  梯度 → 头部摆动采样 → ASE ON/OFF → 连接组 → SMD占空比 → 曲率 → 转弯
                                                                           (涌现)
```

## 修改文件

| 文件 | 变更 |
|------|------|
| `simulation_engine.cpp` | 偏置移到 compute_synaptic 之后; 移除曲率旁路; 头部摆动采样; omega 改 SMD 注入 |
| `simulation_engine.h` | gain=500, clamp=50 |
| `connectome_loader.cpp` | ASER→AIA/AIY 改抑制; SMD 交叉抑制 8→3 |
| `sensory_transducer.h` | fast_tau 500→100ms |

## 参考文献

- Izquierdo & Lockery 2010, J Neurosci — klinotaxis 最小回路与相位依赖机制
- Matsumoto et al. 2024, eLife — ASER→AIY 抑制性传递 (GLC-3)
- Li et al. 2014 — AIY→AIZ 抑制性传递 (ACC-2)
- Suzuki et al. 2008 — ASE ON/OFF 响应动力学
- Iino & Yoshida 2009, J Neurosci — weathervane 与 pirouette 双机制

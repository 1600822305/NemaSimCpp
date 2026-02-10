# Step 23: 温度趋性 (Thermotaxis) — AFD→AIY 共享通路

> 日期: 2026-02-10
> 目标: 新感觉模态接入已有回路，验证架构通用性

## 生物学背景

### 核心回路 (Mori & Ohshima 1995)

```
趋化: ASE → AIA → AIY → RIA → SMD
趋温: AFD → AIY → RIA → SMD
            ↑
       共享节点！
```

AFD 是 C. elegans 的主要温度感觉神经元：
- 位于头部，左右各一个 (AFDL/AFDR)
- 释放谷氨酸 (glutamate)
- 记忆**培养温度 Tc**：在 Tc 下培养 3-4 小时后形成温度偏好
- 响应阈值在 Tc 附近：T > Tc → Ca²⁺ 升高 → 激活

### 双向温度趋性 (Luo 2014 PNAS)

| 方向 | 机制 | 通路 |
|------|------|------|
| **负温度趋性** (T > Tc → 降温) | 类似 klinokinesis：偏离 Tc → 更多 pirouette | AFD→AIY→RIA (pirouette bias) |
| **正温度趋性** (T < Tc → 升温) | 类似 weathervane：偏向 warm 转向 | AFD→AIY→RIA (turning bias) |

### 饱食状态调制 (eLife 2021 Hawk)

```
Fed:     AFD→AIY 正常 → 热趋性活跃
Starved: INS-1 (肠) → DAF-16 (AWC) → AWC 增强 → AIA 抑制 → 热趋性破坏
```

**关键**: AFD 响应本身不受饱食影响！调制发生在下游 AWC-AIA 通路（我们已有）。

## 实现

### 1. 温度场 (`environment.h/.cpp`)

线性梯度：`T(x,y) = T_center + grad_x × (x - cx) + grad_y × (y - cy)`

```cpp
void set_temperature_gradient(double center_temp, Vector2d gradient_dir, double gradient_strength);
double sample_temperature(Vector2d pos) const;
Vector2d temperature_gradient(Vector2d pos) const;
```

默认配置：
- 中心温度: 20°C
- 梯度方向: 左→右 (x 正方向)
- 梯度强度: 0.5°C/mm
- 范围: 7.5°C (x=0) ~ 32.5°C (x=50mm)

### 2. ThermoTransducer (`sensory_transducer.h`)

```cpp
class ThermoTransducer {
    double gain_ = 60.0;           // pA per °C above Tc
    double baseline_ = 5.0;        // pA spontaneous activity
    double tc_adapt_tau_ = 120000; // ms (~2 min, cultivation memory)
    double fast_tau_ = 200.0;      // ms, fast temperature tracker

    double fast_;   // fast-tracking filter
    double tc_;     // cultivation temperature memory
};
```

响应模型：
```
dT = fast_tracker - Tc
response = dT / (1 + |dT| × 2)    // 饱和非线性
I_out = baseline + gain × response  // clamp [0, 50] pA
```

- `fast_`: 200ms tau 跟踪当前温度（匹配 AFD Ca²⁺ 响应速度）
- `tc_`: 120s tau 慢适应（模拟培养温度记忆）
- T > Tc → 正响应 → AFD 激活 → AIY 激活
- T < Tc → 负响应 → AFD 抑制 → AIY 基线

### 3. 突触连接

| 连接 | Sections | 类型 | 参考 |
|------|----------|------|------|
| AFDL→AIYL | 3 | 兴奋性 | Cook 2019 |
| AFDR→AIYR | 3 | 兴奋性 | Cook 2019 |
| AFDL→AIZL | 2 | 兴奋性 | Mori 1995 |
| AFDR→AIZR | 2 | 兴奋性 | Mori 1995 |

### 4. SimulationEngine 集成

```cpp
void apply_thermo_input() {
    Vector2d head_pos = body_.get_head_position();
    double temperature = environment_.sample_temperature(head_pos);
    for (auto& tm : thermo_mappings_) {
        double I_thermo = tm.transducer.update(temperature, dt_);
        neurons_[tm.neuron_id]->add_synaptic_current(I_thermo);
    }
}
```

在 `step()` 中 `apply_sensory_input()` 之后调用。

## 架构验证：共享节点

这是对架构通用性的关键测试：

```
ASE (化学) ──→ AIA ──→ AIY ──→ RIA ──→ SMD → 运动输出
                        ↑
AFD (温度) ─────────────┘
                        ↑
AWC (嗅觉/温度) ───→ AIA ┘
```

AIY 是**多模态整合节点**：
- 接收化学梯度信号 (ASE→AIA→AIY)
- 接收温度信号 (AFD→AIY)
- 接收嗅觉信号 (AWC→AIY)
- 所有信号在 AIY 汇聚，竞争下游 RIA→SMD 输出

**梯度冲突测试**: 食物在右 + 培养温度在左 → AIY 收到矛盾输入 → 涌现优先级决策

## Step 23b: ThermoTransducer 重写

初始实现（绝对温度响应）有两个问题：
1. **Tc 适应太快** (tau=120s)：300s 仿真中 Tc 追上环境温度，dT→0
2. **单向响应**：T < Tc 时 I=0，AFD 完全沉默，无法导航

### 重写为双滤波 OFF 响应

```cpp
deviation = |raw_ - tc_|               // 与 Tc 的距离
dev_fast  += (deviation - dev_fast) * dt / 500ms   // 快滤波
dev_slow  += (deviation - dev_slow) * dt / 5000ms  // 慢适应
signal = -(dev_fast - dev_slow) / (dev_slow + 0.5)  // OFF 响应
```

- **接近 Tc**：deviation 减小 → dev_fast < dev_slow → signal > 0 → AFD 兴奋
- **远离 Tc**：deviation 增大 → dev_fast > dev_slow → signal < 0 → AFD 安静

这与趋化 klinokinesis **完全相同的机制**，只是作用于温度。

### 参数调优教训

| 参数 | 初始值 | 问题 | 最终值 |
|------|--------|------|--------|
| Tc_tau | 120s | Tc 追上环境温度 | **3600s** (1hr, Mori 1995) |
| baseline | 15pA | AIY 过度激活，CI 0.835→0.166 | **5pA** |
| gain | 60 | AFD/ASE ratio=0.32 | **150** |
| AFD→AIY | 3 sections | 信号太弱 | **5 sections** |

## Step 23c: 饱食调制切换 (Mori 1995 经典发现)

### 核心机制

```
饥饿线虫: 忽略温度，朝食物走  ← 化学优先
饱食线虫: 忽略食物，朝培养温度走 ← 温度优先
```

### Sigmoid 增益切换

```cpp
sat_switch = sigmoid(10 × (satiety - 0.5))  // 锐利切换
chemo_gain  = 1.0 - 0.85 × sat_switch       // 饱食: 0.15, 饥饿: 1.0
thermo_gain = 0.2 + 1.8  × sat_switch       // 饱食: 2.0,  饥饿: 0.2
```

同时调制：感觉增益 + weathervane 偏置

### 温度 Weathervane

```cpp
// 导航方向: 最小化 |T - Tc|
temp_sign = (T > Tc) ? -1 : +1              // 朝 Tc 方向
temp_bias = 30 × temp_sign × grad_T_normal × thermo_wv_gain
```

- 饥饿时: thermo_wv_gain ≈ 0 → 无温度转向
- 饱食时: thermo_wv_gain ≈ 2.0 → 15 pA 朝 Tc 偏置

## 梯度冲突测试结果

### 场景

```
食物:  (35, 35) → 化学梯度朝右
温度:  左暖右冷 (-0.5°C/mm), Tc=22.5°C → 目标 x=20mm (左)
```

### 涌现行为: 食物↔Tc 振荡

```
t(s)  x_pos  sat    mode
  20   29.1  0.032  ->Fd  饿了，朝右找食物
  60   37.4  0.598  <-Tc  ★到食物，吃饱！切换TEMP
  80   36.1  0.747  <-Tc  ★朝左走！
 100   31.5  0.769  <-Tc  ★继续朝Tc走
 120   28.8  0.696  <-Tc  ★接近Tc(x=20)
 160   34.3  0.677  <-Tc  饱食度在掉，漂回食物
 220   35.6  0.708  <-Tc  ★又吃饱！再次朝Tc
 240   32.3  0.827  <-Tc  ★x在减小
 280   28.0  0.208  ->Fd  饿了，回头找食物
 300   29.8  0.082  ->Fd  朝右回去
```

**关键**: 线虫在食物和 Tc 之间**涌现出振荡行为**——饿了去吃，吃饱了朝 Tc 走，饿了回来。完全从神经回路 + 饱食调制涌现，无需编程决策逻辑。

### 对比数据

| 指标 | 无温度 | 有温度(无饱食调制) | 有温度+饱食调制 |
|------|--------|-------------------|----------------|
| CI | 0.835 | 0.396 | **0.294** |
| X 位移 | +9.1mm | +1.5mm | **+4.8mm** |
| 行为 | 直奔食物 | 犹豫不决 | **食物↔Tc振荡** |

## 参考文献

- Mori & Ohshima 1995 Nature — AFD thermosensory neuron, cultivation temperature memory
- Clark 2006 J Neurosci — AFD calcium response threshold at Tc
- Luo 2014 PNAS — Bidirectional thermotaxis: negative = klinokinesis, positive = turning bias
- Tomioka 2006 — Satiety modulates thermotaxis preference
- Hawk 2021 eLife — Feeding state reconfigures AWC-AIA (not AFD) for thermotaxis plasticity
- Cook 2019 Nature — Updated connectome (AFD→AIY synapse count)

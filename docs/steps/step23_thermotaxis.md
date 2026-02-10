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

## 诊断输出 (Section 14)

```
14. THERMOTAXIS (Step 23):
   Temperature at head: 24.2 C
   Temp gradient: 0.500 C/mm (dir: 0.50, 0.00)
   AFD: L=-39.90 mV  R=-45.78 mV
   AFDL->AIYL: n=0.912 S=0.273
```

## 结果

- **AFD 活跃**: -39.9/-45.8 mV（非静息，受温度驱动）
- **趋化不受影响**: CI 正常（3.38mm final distance）
- **架构验证通过**: 新感觉神经元接入 AIY，无需修改下游回路
- **74 神经元, ~114 化学突触 + 14 gap junction**

## 参考文献

- Mori & Ohshima 1995 Nature — AFD thermosensory neuron identification
- Clark 2006 J Neurosci — AFD calcium response threshold at Tc
- Luo 2014 PNAS — Bidirectional thermotaxis: negative = klinokinesis, positive = turning bias
- Hawk 2021 eLife — Feeding state reconfigures AWC-AIA (not AFD) for thermotaxis plasticity
- Cook 2019 Nature — Updated connectome (AFD→AIY synapse count)

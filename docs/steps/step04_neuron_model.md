# Step 4: 单隔室 HH 神经元模型

> 日期: 2026-02-10
> 状态: ✅ 完成

---

## 目标

实现适用于 C. elegans 的 HH 型分级电位神经元模型（L2 级别）。

## C. elegans 神经元的核心区别

| 特性 | 哺乳动物 | C. elegans |
|------|---------|-----------|
| 动作电位 | 钠依赖 all-or-none spike | 大部分无 spike，分级电位 |
| 信号编码 | 脉冲频率 | 膜电位幅度 / 递质释放率 |
| 递质释放 | 突触囊泡全或无释放 | 分级释放（与 V 成 sigmoid 关系） |
| 钙角色 | 触发递质释放 | 部分神经元可产生钙 spike (AWA) |

## 实现

### Neuron 基类

```cpp
class Neuron {
    virtual void step(double dt) = 0;
    virtual double get_membrane_potential() const = 0;
    virtual double get_transmitter_release_rate() const = 0;  // [0, 1]
    virtual double get_calcium() const = 0;

    void set_external_current(double I_ext);
    void add_synaptic_current(double I_syn);
    void reset_synaptic_current();
};
```

### SingleCompartmentNeuron

膜电位方程：

```
C_m · dV/dt = -(I_leak + Σ I_ion) + I_syn + I_ext
```

- `I_leak = g_leak · (V - E_leak)` — 漏电流
- `I_ion = g_max · open(V,Ca) · (V - E_ion)` — 各离子通道电流
- `I_syn` — 突触电流总和（化学 + 电突触）
- `I_ext` — 外部注入电流（感觉输入等）

符号约定：I = g·(V-E) 为**外向电流**（正 = 去极化损失）。

### 分级递质释放

```cpp
release_rate = 1 / (1 + exp(-(V - threshold) / slope))
// threshold = -35 mV, slope = 5 mV
```

V = -60 mV → release ≈ 0.007 (几乎不释放)
V = -35 mV → release = 0.5
V = -20 mV → release ≈ 0.95 (接近饱和)

### CalciumDynamics

```
d[Ca]/dt = -α · I_Ca - ([Ca] - [Ca]_baseline) / τ_Ca
```

- `α = buffer_ratio × 0.01`: pA → μM/ms 换算
- `[Ca]_baseline = 0.05 μM`
- `τ_Ca = 200 ms`
- 钙通道识别：E_rev > 40 mV 的通道视为钙通道

### NeuronFactory

按 NeuronType 配置不同参数：

| 类型 | C_m (pF) | g_leak (nS) | E_leak (mV) | 通道组合 |
|------|---------|------------|------------|---------|
| 感觉 | 1.2 | 0.25 | -60 | EGL-19(0.6) + SHL-1(0.8) + KQT-3(0.4) + NCA(0.03) |
| 中间 | 1.5 | 0.30 | -55 | EGL-19(0.8) + SHL-1(1.2) + KQT-3(0.3) + SLO-1(1.0) + NCA(0.05) |
| 运动 | 2.0 | 0.40 | -55 | EGL-19(1.2) + UNC-2(0.8) + SHL-1(1.5) + KQT-3(0.3) + NCA(0.05) |

运动神经元有更强的钙通道（驱动肌肉需要更大的信号幅度）。

## 电压钳位

V 限制在 [-100, +80] mV 范围内，防止数值发散。

## 模型层级规划

| 级别 | 当前状态 | 说明 |
|------|---------|------|
| L1 简化 RC | 未实现 | 快速原型用 |
| **L2 单隔室 HH** | **✅ 已实现** | MVP 默认 |
| L3 多隔室 HH | Phase 3 | AWA/RMD/RIA |
| L4 完整生物物理 | Phase 4 | 精确验证用 |

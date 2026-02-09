# Step 3: 离子通道库 (7 种)

> 日期: 2026-02-10
> 状态: ✅ 完成
> 参考: Nicoletti et al. 2019, PLOS ONE 14(7):e0218738

---

## 目标

实现 C. elegans 已知的关键离子通道，为 HH 型神经元模型提供电导组件。

## C. elegans 离子通道特殊性

> C. elegans 没有经典的电压门控钠通道（Nav），因此大部分神经元不产生钠依赖动作电位。
> 信号主要通过钙通道和钾通道的相互作用产生分级电位。
> 少数神经元（如 AWA）能通过 L-type 钙通道产生钙介导的全或无动作电位。

## 实现

### 基类设计

```cpp
class IonChannel {
    virtual void step(double V, double Ca, double dt) = 0;  // 更新门控变量
    virtual double get_current(double V) const = 0;          // I = g_max * open * (V - E_rev)
    // 工具函数:
    static double boltzmann(V, V_half, k);  // 稳态激活/失活
    static double relax(var, var_inf, tau, dt);  // 指数松弛
};
```

### 7 种通道详细参数

| 通道 | 基因 | 离子 | 门控 | g_max (nS) | E_rev (mV) | 关键参数 |
|------|------|------|------|-----------|-----------|---------|
| L-type Ca | EGL-19 | Ca²⁺ | m²h | 0.6-1.2 | +60 | V½_m=-4.4, k_m=7.5, τ_m=2.5ms, τ_h=50ms |
| N/P/Q-type Ca | UNC-2 | Ca²⁺ | m²h | 0.8-1.0 | +60 | V½_m=-12, k_m=4.5, τ_m=1ms, τ_h=30ms |
| T-type Ca | CCA-1 | Ca²⁺ | m²h | 0.8 | +60 | V½_m=-42, k_m=3, τ_m=3ms, τ_h=15ms |
| Shaker K | SHL-1 | K⁺ | m³h | 0.8-1.5 | -80 | V½_m=-12, k_m=14, τ_m=5ms, τ_h=100ms |
| KCNQ K | KQT-3 | K⁺ | m² | 0.3-0.4 | -80 | V½_m=-43, k_m=5, τ_m=电压依赖(50-250ms) |
| BK K | SLO-1 | K⁺ | m | 1.0-3.0 | -80 | Ca²⁺ 依赖: V½ = -10 - 40·Ca/(Ca+0.5) |
| NALCN Na | NCA | Na⁺ | 无 | 0.03-0.05 | +30 | 恒开漏通道，无门控变量 |

### 通道动力学方程

所有门控变量使用相同框架：

```
dx/dt = (x_inf(V) - x) / τ_x(V)
x_inf(V) = 1 / (1 + exp(-(V - V½) / k))    // Boltzmann
```

离散化：`x(t+dt) = x_inf + (x - x_inf) * exp(-dt/τ)`（精确指数积分，无条件稳定）

### SLO-1 的钙依赖性

BK 通道的特殊之处：激活曲线的半激活电压随 [Ca²⁺]_i 左移：

```
V½_eff = V½_base - 40 * [Ca] / ([Ca] + 0.5)
```

[Ca] = 0.05 μM (静息) → V½ ≈ -14 mV (难激活)
[Ca] = 1.0 μM (活跃) → V½ ≈ -37 mV (容易激活)

→ 负反馈回路：Ca²⁺ 流入 → SLO-1 激活 → K⁺ 外流 → 复极化 → Ca²⁺ 通道关闭

## 尚未实现的通道 (Phase 3)

| 通道 | 基因 | 说明 |
|------|------|------|
| Shaw K | EGL-36 | 高阈值钾电流 |
| EAG K | EGL-2 | 电压门控钾电流 |
| Shaker KV | SHK-1 | 延迟整流 |
| KV | KVS-1 | 缓慢失活 |
| Kir | IRK | 内向整流 |
| SK | KCNL | 小电导钙激活钾 |
| Na/Ca-activated | SLO-2 | 钙/钠双激活钾 |

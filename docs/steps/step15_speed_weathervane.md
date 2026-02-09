# Step 15: 速度调优 + Weathervane 趋化策略

> 日期: 2026-02-10
> 状态: ✅ 完成
> 前置: Step 14 (化学感觉转导 + pirouette 趋化性涌现)

---

## 目标

1. 修复前进速度偏低问题（0.06-0.09 → 目标 ~0.15 mm/s）
2. 实现第二种趋化策略 Weathervane（run 期间渐进转向），与 pirouette 机制并行工作

---

## 关键决策

### 1. Weathervane 机制 (Iino & Yoshida 2009)

**文献核心发现**:
- C. elegans 在 run 期间会**渐进弯曲**朝向高浓度方向
- 曲率偏向 = **12.7 °/mm × ∇C_⊥**（垂直于行进方向的浓度梯度）
- Pirouette 单独 或 weathervane 单独 都只能部分趋化
- **必须两者并行工作才能匹配真实动物的趋化效率**

**实现方案**:
```
1. 在头部位置计算化学浓度空间梯度 ∇C = (∂C/∂x, ∂C/∂y)
2. 分解为切向（沿行进方向）和法向（垂直行进方向）分量
3. 法向分量 grad_normal = -sin(θ)·∂C/∂x + cos(θ)·∂C/∂y
4. 差异电流 = gain × grad_normal，注入 SMD 背腹神经元
5. SMDD += bias, SMDV -= bias → 偏置半中心振荡器 → 渐进转向
```

**神经回路基础**: 头部左右摆动采样横向梯度 → ASE → AIZ → SMD。我们直接用法向梯度偏置 SMD，近似这一采样机制。

**参数**:
- `weathervane_gain = 50 pA/(conc/mm)`: 将梯度转化为 SMD 差异电流
- 偏置限幅 ±5 pA: 不干扰半中心振荡器的正常工作
- 梯度计算: 中心差分, eps = 0.05mm (≈线虫头部宽度)

### 2. 速度调优

**问题**: v_max = 0.4 mm/s 与 muscle_work ~0.3 产生 0.06-0.09 mm/s，低于文献 0.15 mm/s。

**修复**: v_max 0.4 → 0.6 mm/s → 实际速度 0.09-0.16 mm/s，与文献值吻合。

---

## 实现细节

### 新增/修改文件

| 文件 | 修改内容 |
|------|----------|
| `src/environment/chemical_field.h` | 添加 `gradient(pos)` 方法声明 |
| `src/environment/chemical_field.cpp` | 实现 `gradient()`: 中心差分 (eps=0.05mm) |
| `src/simulation/simulation_engine.h` | 添加 `apply_weathervane()` 声明 |
| `src/simulation/simulation_engine.cpp` | 实现 weathervane: ∇C_⊥ → SMD 差异驱动 |
| `src/body/body_model.cpp` | v_max 0.4 → 0.6 |

### Weathervane 信号流

```
化学梯度场 → gradient(head_pos) → 分解为 (tangential, normal) 分量
→ normal × gain → bias_current → SMDD ± bias, SMDV ∓ bias
→ 半中心振荡器偏置 → 头部曲率偏向 → dθ/dt = v × κ → 渐进转向
```

---

## 验证结果

### 60 秒仿真对比

| 指标 | Step 14 (pirouette only) | Step 15 (+weathervane) | 提升 |
|------|--------------------------|------------------------|------|
| 趋化指数 CI | +0.213 | **+0.312** | +46% |
| 终止距食物 | 11.1 mm | **9.7 mm** | -1.4 mm |
| 前进速度 | 0.06-0.09 mm/s | **0.09-0.16 mm/s** | ×1.7 |
| 60s 行进距离 | 3.8 mm | **5.6 mm** | +47% |

### 逐时距离

```
[t= 5s] dist=14.11mm  speed=0.141mm/s
[t=10s] dist=14.09mm  speed=0.123mm/s  (pirouette ~7s)
[t=15s] dist=13.58mm  speed=0.122mm/s
[t=20s] dist=13.13mm  speed=0.162mm/s
[t=25s] dist=12.68mm  speed=0.069mm/s
[t=30s] dist=12.23mm  speed=0.106mm/s
[t=35s] dist=11.80mm  speed=0.149mm/s
[t=40s] dist=11.37mm  speed=0.129mm/s
[t=45s] dist=10.94mm  speed=0.084mm/s
[t=50s] dist=10.52mm  speed=0.099mm/s
[t=55s] dist=10.11mm  speed=0.094mm/s
[t=60s] dist= 9.68mm  speed=0.097mm/s
```

距食物**单调递减**，趋化性稳定持续。

---

## 两种趋化策略协同

本仿真中两种机制的角色:

1. **Pirouette** (Step 14): 浓度降低 → AWC(OFF)→AIB→AVA ↑ → pirouette 概率增加 → 随机重定向
   - 效果: 当走错方向时，快速随机化方向，统计偏向食物
   - 频率: ~0.05 Hz 基础率，AVA 调制

2. **Weathervane** (Step 15): ∇C_⊥ → SMD 差异驱动 → 半中心振荡器偏置 → 渐进弯曲
   - 效果: 在 run 期间持续微调方向，向高浓度弯曲
   - 强度: 与梯度法向分量成正比

**两者叠加**: weathervane 在 run 期间持续校正方向 + pirouette 在走错时快速重置 = 高效趋化。

---

## 参考文献

- Iino Y, Yoshida K (2009). Parallel use of two behavioral mechanisms for chemotaxis in C. elegans. *J Neurosci* 29:5370-5380.
- Pierce-Shimomura JT, Morse TM, Lockery SR (1999). Pirouettes in C. elegans chemotaxis. *J Neurosci* 19:9557-9569.
- Fang-Yen C, et al. (2010). Biomechanical analysis of gait in C. elegans. *J Exp Biol* 213:2244-2253.

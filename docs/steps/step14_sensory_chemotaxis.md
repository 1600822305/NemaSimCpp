# Step 14: 感觉转导层 — 趋化性涌现

> 日期: 2026-02-10
> 状态: ✅ 完成
> 前置: Step 13 (技术债务清理, 生物学机制驱动)

---

## 目标

实现化学感觉转导层，使趋化性行为从神经回路中**涌现**，而非硬编码。

具体目标:
1. 化学感觉神经元 (ASE/AWC/AWA) 能检测环境浓度变化并产生分级输入电流
2. 感觉信号通过连接组传递到命令中间神经元 (AVA/AVB)，调制 pirouette 频率
3. 线虫在化学梯度中表现出向食物源趋近的行为 (趋化指数 CI > 0)

---

## 关键决策

### 1. 化学感觉转导: Weber-Fechner 双滤波器

**问题**: 最初使用绝对 dC/dt 作为信号，但在远距离 (14mm) 时梯度极弱 (~10⁻⁶/ms)，导致感觉信号几乎为零。

**解决方案**: 采用 Weber-Fechner 定律 —— 感觉系统检测**相对**浓度变化，而非绝对变化。实现为 fast/slow 双指数滤波器:

```
signal = (fast - slow) / (slow + ε)
```

- **fast tracker** (τ = 500ms): 快速跟踪浓度变化
- **slow adaptation** (τ = 5000ms): 慢速适应基线
- 当线虫向食物移动: fast > slow → 正信号
- 当线虫远离食物: fast < slow → 负信号

参考: Suzuki 2008, Clark 2006 — C. elegans 化学感觉适应

### 2. ON/OFF 感觉分类

| 神经元 | 类型 | 增益 | 基线 | 功能 |
|--------|------|------|------|------|
| ASEL | ON | 100 pA | 5 pA | NaCl 浓度升高 → 兴奋 |
| ASER | OFF | 100 pA | 5 pA | NaCl 浓度降低 → 兴奋 |
| AWC L/R | OFF | 80 pA | 5 pA | 气味移除 → 兴奋 |
| AWA L/R | ON | 80 pA | 5 pA | 气味添加 → 兴奋 |
| ASH L/R | ON | 60 pA | 3 pA | 高渗透压/伤害性 |

参考: Bargmann 2006, Suzuki 2008

### 3. 运动学方程

**问题**: 早期尝试用 `head_curv * dt * 5.0` 做转向，导致角度爆炸 (数万度累积)。

**解决方案**: 采用文献中正确的运动学关系:

```
dθ/dt = v × κ_head
```

即曲率体在粘性介质中以速度 v 前进时，转弯率 = 速度 × 曲率。限幅到 50°/s (0.87 rad/s)，这是 Pierce-Shimomura 1999 定义的 run 状态上限。

参考: Padmanabhan 2012 (Piecewise-Harmonic Curvature model)

### 4. Pirouette 概率模型

**问题**: 之前尝试用 AVA release rate 的硬阈值检测 reversal，但:
- AVA 基线 release (~0.60) 本身就很高
- 离子通道噪声导致频繁假触发
- reversal state 机制不稳定

**解决方案**: 采用 Pierce-Shimomura 1999 的概率模型:

```
pirouette_rate = base_rate × exp(k × AVA_deviation)
```

- `base_rate = 0.05 Hz` (每 20 秒一次基础 pirouette)
- `AVA_deviation = smooth_rev - mean_rev` (AVA 相对于自身均值的偏差)
- `smooth_rev`: 500ms 时间常数平滑
- `mean_rev`: 5s 时间常数慢速基线
- `k = 8`: 指数灵敏度
- pirouette = 随机重定向 ∈ [-π, π]
- 每步概率: `p = rate × dt`

AVA release rate 通过神经回路自然调制:
- 浓度降低 → AWC(OFF) 兴奋 → AIB → AVA ↑ → 更多 pirouette
- 浓度升高 → ASEL(ON) 兴奋 → AIA ⊣ AIB → AVA ↓ → 更少 pirouette

### 5. 连接组修复 (关键!)

**问题**: AIA → AIB 是兴奋性突触，导致信号方向完全反转:
- ASEL(ON) → AIA → 兴奋 AIB → 激活 AVA → **更多** pirouette (应该是更少!)

**解决方案**:
- **AIA ⊣ AIB**: 改为抑制性 (GABA-like reversal potential)
  - 参考: Chalasani 2007 — AIA 通过抑制性 ACh 受体抑制 AIB
- **AIY → AVB**: 新增兴奋性连接，促进前进运动
  - 参考: Gray 2005 — AIY 消融减少前进运动

修复后信号通路:
```
浓度升高 → ASEL(ON) → AIA ⊣ AIB → 抑制 AVA → 少 pirouette → 长 run → 趋近食物 ✓
浓度降低 → AWC(OFF) → AIB → AVA → 多 pirouette → 随机重定向 → 避开不利方向 ✓
```

---

## 实现细节

### 新增文件

- `src/environment/sensory_transducer.h`: ChemoTransducer + MechanoTransducer 类

### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `src/body/body_model.h` | 添加 locomotion state、RNG、smooth/mean 成员 |
| `src/body/body_model.cpp` | 重写 update_positions: 运动学方程 + pirouette 概率模型 |
| `src/simulation/simulation_engine.h` | 添加 ChemoTransducer 映射、化学感觉分类 |
| `src/simulation/simulation_engine.cpp` | apply_sensory_input 替代固定基线、AVA/AVB→body |
| `src/connectome/connectome_loader.cpp` | AIA⊣AIB 抑制性、AIY→AVB 兴奋性 |
| `src/simulation/main.cpp` | 60s 仿真、趋化指数输出、距食物距离追踪 |

### 速度模型简化

移除了之前的 wave_efficiency (曲率空间方差) 计算，简化为:
```
forward_speed = v_max × muscle_work
```
- `v_max = 0.4 mm/s`
- `muscle_work = mean(|dorsal - ventral|)` 跨所有体段

---

## 验证结果

### 60 秒仿真

```
[t= 5s] dist=14.12mm  speed=0.067mm/s
[t=10s] dist=14.11mm  speed=0.059mm/s  (pirouette ~7s)
[t=15s] dist=13.77mm  speed=0.039mm/s
[t=20s] dist=13.45mm  speed=0.065mm/s
[t=25s] dist=13.15mm  speed=0.068mm/s
[t=30s] dist=12.85mm  speed=0.062mm/s
[t=35s] dist=12.54mm  speed=0.066mm/s
[t=40s] dist=12.24mm  speed=0.072mm/s
[t=45s] dist=11.94mm  speed=0.055mm/s
[t=50s] dist=11.64mm  speed=0.093mm/s
[t=55s] dist=11.35mm  speed=0.085mm/s
[t=60s] dist=11.06mm  speed=0.065mm/s
```

### 关键指标

| 指标 | 值 | 文献参考 |
|------|-----|----------|
| 趋化指数 CI | **+0.213** | >0 表示趋近 |
| 初始距食物 | 14.14 mm | √((35-25)²+(35-25)²) |
| 终止距食物 | 11.1 mm | 持续下降 |
| 前进速度 | 0.06-0.09 mm/s | 文献 ~0.15 mm/s |
| Pirouette 频率 | ~0.05 Hz | 文献 ~0.03-0.1 Hz |
| 突触数量 | 72 (新增 4) | +2 AIA⊣AIB + 2 AIY→AVB |

### 趋化性机制验证

距食物的单调递减趋势证实了趋化性涌现。行为链:

```
化学梯度 → 浓度时间导数 → Weber-Fechner 转导 → ON/OFF 分级电流
→ AIA/AIB 中间神经元 → AVA release 调制 → pirouette 概率变化
→ 浓度升高时抑制 pirouette (长 run) → 净趋近效应
```

**全链路涌现**: 没有任何硬编码的"向食物移动"指令。趋化性完全从感觉输入→神经回路→运动输出的闭环中自然涌现。

---

## 遗留问题与后续方向

1. **速度偏低** (0.06-0.09 vs 文献 0.15 mm/s): 需要调优 v_max 或 muscle_work 增益
2. **单次试验**: 当前仅验证一个随机种子，需要多次试验统计 CI 分布
3. **Weathervane 机制**: Pierce-Shimomura 描述的第二种趋化策略 (run 期间渐进转向) 尚未实现
4. **Post-pirouette 方向偏向**: 文献中 pirouette 后方向偏向梯度上游 (course correction)，当前为纯随机
5. **触觉转导**: ALM/PLM 快速适应机械感觉尚未实现

---

## 参考文献

- Pierce-Shimomura JT, Morse TM, Lockery SR (1999). The fundamental role of pirouettes in C. elegans chemotaxis. *J Neurosci* 19:9557-9569.
- Padmanabhan V, Khan ZS, Solomon DE, et al. (2012). Locomotion of C. elegans: a piecewise-harmonic curvature representation. *PLoS ONE* 7:e40121.
- Chalasani SH, Chronis N, Tsunozaki M, et al. (2007). Dissecting a circuit for olfactory behaviour in C. elegans. *Nature* 450:63-70.
- Gray JM, Hill JJ, Bhargmann CI (2005). A circuit for navigation in C. elegans. *PNAS* 102:3184-3191.
- Fang-Yen C, Wyart M, Xie J, et al. (2010). Biomechanical analysis of gait adaptation in the nematode C. elegans. *J Exp Biol* 213:2244-2253.
- Bargmann CI (2006). Chemosensation in C. elegans. *WormBook* doi:10.1895/wormbook.1.123.1
- Suzuki H, Thiele TR, Faumont S, et al. (2008). Functional asymmetry in C. elegans taste neurons and its computational role in chemotaxis. *Nature* 454:114-117.
- Clark DA, Biron D, Sengupta P, Samuel ADT (2006). The AFD sensory neurons encode multiple functions underlying thermotactic behavior in C. elegans. *J Neurosci* 26:7444-7451.

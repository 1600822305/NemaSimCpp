# Step 80: Temperature Cultivation Learning (Tc Plasticity)

## 动机

Step 23 实现了 AFD 温度感知和基本趋温行为（饱腹度依赖增益切换）。但 Tc（培养温度记忆）
是硬编码的 22.5°C，不随经历变化。Hedgecock & Russell 1975 发现 C. elegans 会记住
与食物配对的温度并趋向它，饥饿配对则产生温度回避。

## 生物学基础

- **Hedgecock & Russell 1975 PNAS**: 温度趋化行为 — 蠕虫迁移至培养温度
- **Mori & Ohshima 1995 Nature**: AFD→AIY 趋暖驱动 + AIZ 趋冷驱动，Tc 处平衡
- **Chi 2007 J Exp Biol**: 温度和食物通过**独立机制**影响趋温行为
  - Tc 记忆建立在食物位置（不是内部饱腹度）
  - 食物条件影响表现哪种行为，而非记忆建立速率
- **Kodama 2006**: INS-1/DAF-2 胰岛素信号介导饥饿-温度关联
- **Nishida 2011 EMBO Rep**: CREB 在 AFD 中（细胞自主记忆）
- **Kimura 2004 Curr Biol**: AFD 既是传感器又是记忆器

## 关键发现

AFD 神经元内部的 Tc 记忆是**细胞自主的**（Nishida 2011）：
- 激光消融 AFD → 失去趋温行为
- CREB 转录因子特异性表达于 AFD
- Tc 适应不需要下游回路反馈

Chi 2007 的独立机制模型：
1. **温度记忆**（Tc 适应）：与当前温度缓慢对齐
2. **行为输出**：取决于食物条件（趋向 vs 回避）
3. 这**不是**经典条件反射 — 两个过程独立运作

## 实现细节

### 1. ThermoTransducer::adapt_tc() (sensory_transducer.h)

```
adapt_tc(learn_signal, temperature, dt):
  rate = learn_signal × tc_learn_factor_ × dt / tc_adapt_tau_
  tc_ += (temperature - tc_) × rate
```

- `learn_signal > 0`（在食物上）: Tc → 当前温度（正关联）
- `learn_signal < 0`（离食物）: Tc ← 远离当前温度（厌恶）
- `tc_learn_factor_ = 10.0`: 学习率倍增器（补偿仿真时间压缩）

### 2. apply_thermo_input() 学习信号 (simulation_engine.cpp)

```cpp
double food_here = environment_.sample_food_density(head_pos);
double thermo_learn_signal = (food_here > 0.1) ? 0.5 : -0.3;
```

使用食物存在（而非 satiety）作为信号：
- Chi 2007: "food conditions affect WHICH behavior is exhibited"
- 在食物上 → learn_signal = +0.5 → Tc 向当前温度漂移
- 离食物 → learn_signal = -0.3 → Tc 远离当前温度（不对称：学习快于遗忘）

### 3. 移除无条件 Tc 适应

原始 ThermoTransducer::update() 中的 `tc_ += (temperature - tc_) * dt / tc_adapt_tau_`
（无条件 Tc 漂移）被移除，替换为喂食状态门控的 adapt_tc()。

### 4. Diag 输出增强

Section 14 THERMOTAXIS 输出更新：
- `Tc(initial)=22.5  Tc(learned)=XX.XX  dTc=XX.XX`
- `learned_tc()` 访问器返回第一个 AFD ThermoTransducer 的当前 Tc

## 验证结果 (2 seeds × 2 conditions, 300s)

### 有食物 (--no-toxin) vs 无食物 (--no-food)

| 条件 | Seed | Tc(learned) | dTc | X 位移 | 结果 |
|------|------|-------------|-----|--------|------|
| 有食 | 42 | 23.94 | +1.44 | +21.2mm | **FOOD wins** |
| 有食 | 7 | 21.81 | **-0.69** | +3.9mm | **FOOD wins** |
| 无食 | 42 | 22.58 | +0.08 | -10.1mm | **TEMP wins** |
| 无食 | 7 | 23.93 | +1.43 | -3.8mm | **TEMP wins** |

### 涌现行为分析

1. **有食条件**: 蠕虫趋向食物（化学趋化主导）
   - Seed 7: Tc 下移 -0.69°C → 向食物区温度学习（正关联）✅
   - Seed 42: 蠕虫远离食物区后 Tc 上移（离食厌恶生效）

2. **无食条件**: 蠕虫远离初始位置（温度趋化主导）
   - Tc 上移 → 远离蠕虫所在的冷区（饥饿厌恶）✅
   - X 位移为负 → 蠕虫向暖侧移动

3. **行为分离涌现**: 
   - 有食 → 化学趋化压倒温度趋化 (Mori 1995: hungry = food priority)
   - 无食 → 温度趋化主导行为 (Hedgecock 1975: starved = avoid Tc)

## 修改文件

| 文件 | 修改内容 |
|------|---------|
| `src/environment/sensory_transducer.h` | ThermoTransducer: 移除无条件 Tc 适应, 添加 adapt_tc(), tc_learn_factor_ |
| `src/simulation/simulation_engine.h` | 添加 learned_tc() 访问器 |
| `src/simulation/simulation_engine.cpp` | apply_thermo_input(): 食物存在信号 + adapt_tc() 调用 |
| `src/simulation/diag_main.cpp` | Section 14: Tc(learned)/dTc 追踪 |

## Regtest

20/20 PASS，神经元 171，突触 337，间隙连接 98（无变化）

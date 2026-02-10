# Step 26: 条件性病原体回避学习

## 概述

实现线虫的**条件性味觉厌恶学习**：吃了有毒食物 → 生病 → 学会回避同种气味。这是线虫化学回避的**第二层**（先天ASH之上的学习层），与人类吃坏肚子后再也不想碰那道菜的机制一模一样。

## 生物学基础

### Zhang, Lu & Bargmann 2005 Nature 438:179-184

线虫最初被致病菌 PA14 的气味**吸引**（看起来像食物），进食 4-6 小时后**生病**，此后**回避**同种细菌的气味。

```
第一次: 闻到 PA14 → AWC: "食物！" → 趋近 → 进食 → 生病
第二次: 闻到 PA14 → "上次吃了生病" → 回避
```

### 三条回路 (Frontiers Immunol 2024 综述)

#### 回路1: AWB-AWC 感觉运动回路 (先天偏好 + 学习)
- PA14 暴露 → AWC 钙动力学被**抑制** → 减弱趋近
- AWB 钙动力学被**激活** → 增强回避

#### 回路2: ADF 5-HT 调制回路 (学习核心)
```
PA14 进食 → 肠道免疫响应
  → AWB/AWC 中 EGL-30 (Gqα)
  → ADF 中 CaMKII/UNC-43
  → TPH-1 (色氨酸羟化酶) 上调
  → ADF 5-HT 释放 ↑
  → MOD-1 (5-HT门控 Cl⁻通道) 在 AIY/AIZ 上
  → 抑制 AIY/AIZ → 压制趋近行为
```

#### 回路3: AWC 突触可塑性翻转
```
健康时: AWC → AIY → "趋近" (强连接)
         AWC → AIB → "回避" (弱连接)
生病后: AWC → AIY → "趋近" (减弱!)
         AWC → AIB → "回避" (增强!)
```

### 关键分子

| 分子 | 位置 | 功能 |
|------|------|------|
| TPH-1 | ADF | 色氨酸羟化酶, 5-HT 合成限速酶 |
| MOD-1 | AIY, AIZ | 5-HT 门控 Cl⁻ 通道 (抑制性) |
| EGL-30 | AWB/AWC | Gqα蛋白, 激活 ADF 中 tph-1 |
| CaMKII | ADF | 调控 tph-1 表达 |
| DAF-7 | ASI | TGF-β, 促进病原回避 |

## 实现

### 1. ADF 神经元 (L/R)

- 新增 2 个感觉神经元: ADFL, ADFR (5-HT 源)
- 总神经元: 83 → **85**
- ADF 是**第二个 5-HT 源** (与 NSM 并列)
- 驱动: `I_ext = 2.0 + 30.0 × sickness_`
  - baseline 2pA (低, 正常时几乎不释放 5-HT)
  - sickness=1.0 时: 32pA (强去极化 → 大量 5-HT 释放)
- 从 `other_sensory_ids_` 排除, 由 sickness_ 单独驱动

### 2. 新增突触

| 连接 | Sections | 类型 | 功能 |
|------|----------|------|------|
| ADF→AIY (L/R) | 2 | 5-HT (MOD-1) | 抑制趋近通路 |
| ADF→AIZ (L/R) | 1 | 5-HT (MOD-1) | 抑制趋近通路 |

ADF 的 5-HT 同时通过**容积传递** (neuromodulation 系统) 作用于所有 MOD-1 靶标。

### 3. Sickness 内部状态

```cpp
// 有毒食物区进食时累积
// 条件: food_density > 0.1 AND repellent_conc > 0.1 AND pump_rate > 0.5 Hz
bool eating_toxic = (food_here > 0.1 && toxin_here > 0.1 && pump_rate > 0.5);

if (eating_toxic) {
    double toxicity = toxin_here / (toxin_here + 0.3);  // 饱和函数
    sickness_ += toxicity * dt / tau_rise;  // tau_rise = 30s (加速版4-6小时)
} else {
    sickness_ -= sickness_ * dt / tau_decay;  // tau_decay = 600s (持久记忆, ~10分钟)
}
```

### 4. AWC 突触可塑性翻转 (嗅觉学习位点)

```cpp
// 每 100ms 更新, 仅当 sickness_ > 0.05
double lr = 0.003 * sickness_;  // 学习率 ∝ 生病程度 (15x原始值)
// w_mod 在 ~80s 持续生病内变化 ±50% (800更新 × 0.003 × S_pre≈0.2 = 0.48)

// AWC→AIY: 减弱趋近通路
syn.adjust_weight_mod(-lr * S_pre);  // → 降到 0.1 (底限)

// AWC→AIB: 增强回避通路
syn.adjust_weight_mod(+lr * S_pre);  // → 升到 ~2.8
```

### 5. Weathervane AWC 偏好翻转 (Step 26b)

```cpp
// AWC→AIY w_mod → awc_pref: 不对称缩放
// 回避代价 > 错过食物代价 → 排斥力 > 引诱力
awc_pref = (mean_w_mod - 0.55) * 3.0;  // clamp [-2.0, +1.0]
// w_mod=1.0 → pref=+1.0 (天真吸引)
// w_mod=0.1 → pref=-1.35 (强排斥, 1.35× 吸引力!)

// 食物气味 weathervane *= awc_pref
// 学习后: weathervane 反向, 主动推离食物!
```

### 6. Sickness 化学感觉抑制 (疾病性厌食)

```cpp
// REF: DAF-7 (TGF-β) from ASI 减少食物吸引
double sick_suppression = 1.0 - 0.85 * sickness_;  // 生病: 15% 残余
// 作用于 ASE/AWC/AWA 神经驱动 (NOT NSM/CEP 食物检测)
// Weathervane 不受影响 (独立通路, 由 awc_pref 调制)
```

### 7. 多化学物种基础设施 (Step 26b)

- 新增 `soluble_field_` (水溶性化学物: 盐/氨基酸)
- 食物源同时发射 food_odor (σ²=144) + soluble (σ²=144, strength=0.4)
- ASE 暂留 chemo_mappings_ (回归安全); soluble_mappings_ 就绪
- REF: Bargmann 2006 — AWC 检测挥发性气味, ASE 检测离子

### 8. Sickness 清除 food_memory (Step 26c)

**双重保护**:

```cpp
// 保护1: 快速衰减 — sickness 加速 food_memory 清除
if (sickness_ > 0.3) {
    effective_decay_tau = 5000.0;  // 5s 快速清除 (vs 正常 90s)
}

// 保护2: 充值门控 — 生病时不把毒食物记成"好食物"
if (on_food > food_memory_ && sickness_ < 0.3) {
    food_memory_ += ...  // 只有健康时才记住好食物
}
// REF: Hills 2004 — sickness 抑制 DA 释放 → DARPP-32 去磷酸化
// 效果: fmem 清零后即使靠近毒食物也不会重新充值
//   无门控: t=220 fmem 0.607→0.849 (被充满!)
//   有门控: t=220 fmem = 0.000 (保持为零!)
```

**为什么需要**: food_memory 是 ARS (觅食局部搜索) 的基础——高 fmem → AVA +2.5pA → 高频 reversal → 困在食物旁。对好食物这是生存优势，但对毒食物变成"被困在毒食物旁"。

## 信号链

```
第一次 (天真):
  食物气味 → AWC(OFF) → AIY(强) → 趋近 → 到达食物
  weathervane: food_odor 梯度 → awc_pref=+1.0 → 曲向食物
  进食 → 咽部泵(Step 24) → 摄入有毒食物
  sickness_ ↑ → ADF TPH-1 ↑ → 5-HT ↑

学习过程 (~60s):
  5-HT → MOD-1 → AIY 被抑制 (趋近受阻)
  sickness → AWC→AIY w_mod ↓ → 0.1 (底限! 趋近关闭)
  sickness → AWC→AIB w_mod ↑ → 2.8 (回避增强 3×)
  sickness → 化学感觉增益 × 0.15 (厌食)
  sickness → food_memory 快速清除 (tau 90s→5s)

学习后 (永久):
  同样气味 → AWC → AIB(增强) → AVA → 回避!
  同样气味 → AWC → AIY(w_mod=0.1, 几乎无效) → 不趋近
  weathervane: awc_pref=-1.35 → 主动推离食物气味!
  化学感觉: 15% 残余 → klinokinesis 无法拉回
  food_memory: 0.000 → 不再局部搜索 → 直线离开
```

## 三层化学回避体系

| 层 | 机制 | 速度 | Step |
|---|------|------|------|
| 先天 | ASH 直接检测毒素 → reversal + weathervane绕路 | 即时 | 25 |
| 学习 | 吃了→生病→AWC突触翻转→闻到就跑 | ~30s | **26** |
| 调制 | 5-HT (NSM+ADF) → MOD-1 抑制 AIY/AIB | 秒级 | 20/26 |

## 验证结果

### Regtest (无排斥源): 12 pass, 0 FAIL
- 无排斥物时 sickness_=0, ADF 只有 2pA baseline → 无影响
- awc_pref=+1.0 (天真) → weathervane 行为不变
- food_memory 正常衰减 (tau=90s, 无 sickness 加速)

### Diag (有毒食物: 食物和排斥物同在 35,25):

```
t=20:  dist=5.88, sick=0.204, fmem=0.679 → 接近食物 (天真)
t=40:  dist=1.87, sick=0.644, fmem=0.036 → 到达食物! fmem不充值(已生病)
t=60:  dist=2.40, sick=1.000, fmem=0.001 → MAX sickness, fmem归零
t=100: dist=10.5, sick=0.987, fmem=0.000 → 远离!
t=160: dist=16.7, sick=0.893, fmem=0.000 → 最远!
t=220: dist=5.15, sick=1.000, fmem=0.000 → 短暂接近但fmem保持零!
t=300: dist=11.8, sick=0.957, fmem=0.000 → CI=-0.182 ✅
```

- **CI = -0.182** (反向! 主动远离有毒食物)
- **time_near_food = 29.0%** (vs 毒食物无学习时 45.2%)
- **最远距离: 16.7mm** (t=160)
- **AWC→AIY w_mod = 0.10** (底限! 趋近通路关闭)
- **AWC→AIB w_mod = 2.30** (+130%, 回避通路大幅增强)
- **fmem = 0.000** (永久为零! 充值门控生效)
- **awc_pref ≈ -1.35** (weathervane 排斥食物气味)
- **sick_suppression ≈ 0.15** (化学感觉大幅抑制)

## 修改文件

| 文件 | 修改 |
|------|------|
| `connectome_loader.cpp` | 新增 ADFL/ADFR (5-HT), ADF→AIY(2), ADF→AIZ(1) |
| `simulation_engine.h` | sickness_, adf_ids_, aiy_ids_, tau_decay=600s, soluble_mappings_ |
| `simulation_engine.cpp` | lr=0.003, AWC偏好weathervane, sick_suppression, soluble_field_基础设施, fmem清除+充值门控 |
| `environment.h/.cpp` | 新增 soluble_field_ (水溶性化学通道) |
| `diag_main.cpp` | 有毒食物场景, sickness追踪, 多化学物种源 |

## 参考文献

- Zhang, Lu & Bargmann 2005 Nature 438:179-184 — 发现病原体厌恶学习
- Ha et al. 2010 Neuron 68:1173-1186 — 厌恶嗅觉学习神经网络功能组织
- Frontiers Immunol 2024 (10.3389/fimmu.2024.1353747) — 三回路综述
- eNeuro 2025 (ENEURO.0127-25.2025) — 5-HT 调控冲突感觉整合

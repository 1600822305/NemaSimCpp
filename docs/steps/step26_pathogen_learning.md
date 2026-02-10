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
    sickness_ -= sickness_ * dt / tau_decay;  // tau_decay = 120s (缓慢恢复)
}
```

### 4. AWC 突触可塑性翻转 (嗅觉学习位点)

```cpp
// 每 100ms 更新, 仅当 sickness_ > 0.05
double lr = 0.0002 * sickness_;  // 学习率 ∝ 生病程度

// AWC→AIY: 减弱趋近通路
syn.adjust_weight_mod(-lr * S_pre);

// AWC→AIB: 增强回避通路
syn.adjust_weight_mod(+lr * S_pre);
```

## 信号链

```
第一次 (天真):
  食物气味 → AWC(OFF) → AIY(强) → 趋近 → 到达食物
  进食 → 咽部泵(Step 24) → 摄入有毒食物
  sickness_ ↑ → ADF TPH-1 ↑ → 5-HT ↑

学习过程:
  5-HT → MOD-1 → AIY 被抑制 (趋近受阻)
  sickness → AWC→AIY w_mod ↓ (长期减弱)
  sickness → AWC→AIB w_mod ↑ (长期增强)

第二次 (学会了):
  同样气味 → AWC → AIB(增强) → AVA → 回避!
  同样气味 → AWC → AIY(减弱 + 被5-HT抑制) → 不趋近
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

### Diag (有毒食物: 食物和排斥物同在 35,25):

```
t=20:  dist=5.41, sick=0.224  → 接近食物 (天真)
t=40:  dist=1.46, sick=0.671  → 到达食物！开始生病
t=60:  dist=2.62, sick=1.000  → MAX sickness, 离开
t=100: dist=8.36, sick=1.000  → 远离食物
t=200: dist=3.58, sick=1.000  → 又靠近 (趋化拉力)
t=280: dist=9.62, sick=0.881  → 最终远离
t=300: dist=3.45, sick=1.000  → CI=0.655
```

- **sickness = 1.0** (最大值, 持续吃毒食)
- **ADFL I_ext = 32pA** (被 sickness 强烈激活)
- **AWC→AIY w_mod = 0.880** (-12%, 趋近减弱)
- **AWC→AIB w_mod = 1.120** (+12%, 回避增强)
- **5-HT = 0.787** (NSM+ADF 双源释放)
- All stages healthy, CI=0.655

## 修改文件

| 文件 | 修改 |
|------|------|
| `connectome_loader.cpp` | 新增 ADFL/ADFR (5-HT), ADF→AIY(2), ADF→AIZ(1) |
| `simulation_engine.h` | sickness_, adf_ids_, aiy_ids_, update_sickness(), update_pathogen_learning() |
| `simulation_engine.cpp` | ADF 驱动, ADF 5-HT源注册, sickness累积, AWC突触翻转, ADF排除 |
| `diag_main.cpp` | 有毒食物场景, sickness追踪, PATHOGEN LEARNING诊断输出 |

## 参考文献

- Zhang, Lu & Bargmann 2005 Nature 438:179-184 — 发现病原体厌恶学习
- Ha et al. 2010 Neuron 68:1173-1186 — 厌恶嗅觉学习神经网络功能组织
- Frontiers Immunol 2024 (10.3389/fimmu.2024.1353747) — 三回路综述
- eNeuro 2025 (ENEURO.0127-25.2025) — 5-HT 调控冲突感觉整合

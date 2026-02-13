# Step 117: 联想气味-食物条件学习 (Associative Odor-Food Conditioning)

> 日期: 2026-02-13

---

## 动机

C. elegans 能形成气味与食物的双向联想记忆：
- **正向条件学习**: 丁酮(butanone) + 食物配对 → AWC→AIY 增强 → 趋化性增强
- **负向条件学习**: 苯甲醛(benzaldehyde) + 饥饿配对 → AWC→AIY 减弱 → 回避

这是该仿真系统中第一个真正的**联想学习**机制（之前的病原体学习由 sickness 驱动，不需要 CS-US 配对）。

## 生物学基础

### 分子通路
- **INS-1** (胰岛素同源物): 从 ASI/AIA 释放，作为饥饿/疾病信号
- **DAF-2** (胰岛素受体): 在 AWC 中表达，接收 INS-1
- **AGE-1** (PI3K): 在 AWC 中作用，修改突触输出增益
- **CRH-1** (CREB): AWC 中的转录因子，长期记忆所需

### 关键发现
- Lin 2010 JNeurosci: INS-1/DAF-2 在苯甲醛-饥饿学习中起双重作用（获取 vs 提取）
- Kauffman 2010 PNAS: 正向丁酮条件学习，需要 CREB
- Wen 2012 Neuron: AWC→AIY 突触是可塑性位点
- Cho 2016 Neuron: CREB/CRH-1 在 AWC 中用于长期记忆

### 回路
```
   气味 → AWC(L/R) →[w_mod可塑]→ AIY(L/R) → 前进驱动
              ↑
        INS-1 from ASI/AIA (饥饿信号)
              ↓
        DAF-2 → AGE-1(PI3K) → 修改 w_mod
```

## 实现细节

### 学习规则
```
food_signal = sigmoid(food_density - 0.1)  // >0.5=有食, <0.5=无食
learn_signal = food_signal - 0.5           // 正=有食, 负=无食
ins1_amp = 1.0 + ins1_conc × 2.0          // INS-1 放大学习率
lr = 0.0008 × ins1_amp × sleep_factor
S_awc = sigmoid(V_awc)                     // AWC活动门控

Δw_mod = lr × learn_signal × S_awc
```

### 条件门控
- **CS (条件刺激)**: S_awc > 0.05 (AWC 必须活跃 = 气味存在)
- **US (非条件刺激)**: food_signal (食物存在/缺失)
- **配对**: CS 和 US 同时存在时才发生可塑性
- **INS-1 调制**: 饥饿增强学习率 (Lin 2010: ins-1 突变体学习缺陷)
- **睡眠巩固**: 睡眠期间学习率 ×2 (Chouhan 2023)
- **遗忘**: 已有 apply_synaptic_forgetting() 自动处理 w_mod→1.0 漂移

### 与已有系统的整合
- 复用 `awc_aiy_syn_indices_` 缓存（已有）
- 复用 `update_awc_pref_cache()` 更新（已有）
- 复用 `apply_synaptic_forgetting()` 遗忘（已有）
- 复用 `ins1_conc_` INS-1 浓度（Step 63 已有）
- 复用睡眠巩固机制（Step 62 已有）

## Diag 验证

```
35. ASSOCIATIVE ODOR CONDITIONING (Step 117):
   AWCL->AIYL: w_mod=0.2408
   AWCR->AIYR: w_mod=0.2499
   AWC->AIY mean w_mod=0.2453 (NEGATIVE conditioning: learned aversion)
   INS-1=1  satiety=0.00907
```

默认场景中虫子大部分时间远离食物 → 负向条件学习生效 → w_mod 从 1.0 降至 ~0.245。

## 修改文件
- `src/simulation/simulation_engine.h`: 声明 `odor_cond_lr_` + `update_odor_conditioning()`
- `src/simulation/update_learning.cpp`: 实现 `update_odor_conditioning()`
- `src/simulation/simulation_engine.cpp`: step() 中调用
- `src/simulation/diag_main.cpp`: 诊断输出 AWC→AIY w_mod

# Tier 2 缺失行为 — 文献调研汇总

> 目标: 在 Tier 1 (6/6 ✅) 基础上，识别并实现下一批缺失行为。
> 方法: 代码审计 → 文献调研 → 按难度排序 → 逐一实现。

---

## 代码审计发现

已有 30+ 行为机制完整实现。以下 6 个行为存在 **连接组已布线但感觉传导缺失** 或 **全新缺失** 的情况。

---

## 1. PVM 后体轻触激活 (Posterior Gentle Touch)

### 现状
- PVM 神经元已注册 (Step 106): `NT::SENSORY, NTT::GLUTAMATE`
- 连接组已有: PVM→PVC(1), PVM→AVA(1), PVM↔AVM(gj2)
- **缺失**: `apply_touch_stimulus()` 中无 PVM 感觉驱动

### 生物学
- PVM 是 AVM 的后体对应物，感知后体轻触 (mec-4/mec-10 DEG/ENaC)
- 后体轻触 → PVM → PVC → 促进前进 (逃离后方刺激)
- 与 PLM 功能重叠但 PVM 更弱 (5-10% 贡献)
- REF: Chalfie 1985, Way & Chalfie 1989

### 实现
- 在 `apply_touch_stimulus()` 的 `rear_touch` 分支中加 PVM 激活
- PVM 电流弱于 PLM (PLM=80pA, PVM=40pA)

### 难度: ★☆☆☆☆ (~5行代码)

---

## 2. FLP 热伤害感受 (Thermal Nociception)

### 现状
- FLP 已有机械感觉: 鼻触 (gentle: 15pA) + 粗触 (harsh: 50pA)
- **缺失**: FLP 不响应高温 (>33°C)

### 生物学
- FLP 是多模态伤害感受器: 机械 (MEC-10) + 热 (TRPA-1)
- 温度 >33°C → TRPA-1 通道开放 → FLP 激活 → 逃逸反应
- 与 AFD 温度趋性不同: AFD 是温和温度导航, FLP 是有害热回避
- 逃逸反应: 速度↑ + 反转↑ + omega↑
- REF: Chatzigeorgiou & Schafer 2010 Neuron, Liu 2012 Nature

### 实现
- 在 `apply_touch_stimulus()` 中加温度采样
- T > 33°C → FLP 获得热伤害电流 (比例于超过阈值的程度)
- FLP→AVA/AVD 逃逸回路已有

### 难度: ★★☆☆☆ (~15行代码)

---

## 3. ALA 应激诱导静止 (Stress-Induced Quiescence)

### 现状
- ALA 神经元已注册 (Step 106): `NT::INTER, NTT::GLUTAMATE`
- **缺失**: 无功能性输入/输出代码

### 生物学
- ALA 介导应激诱导睡眠 (与 RIS 疲劳睡眠不同)
- 应激信号: UV 暴露、热休克 (>33°C)、机械损伤
- ALA 释放 FLP-13 + NLP-22 神经肽 → 全身运动抑制
- 与 RIS 的区别: RIS=内稳态/疲劳; ALA=急性应激/保护性
- EGF-LET-23 信号激活 ALA → 连锁激活 (Hill 2014)
- REF: Hill 2014 Curr Biol, Nelson 2014 eLife, Nath 2016

### 实现
- 检测应激条件: T>33°C 或 ASH 强激活 (代表损伤)
- 应激 → ALA 激活 → FLP-13/NLP-22 释放
- 效果: 类似 RIS 的全身运动抑制但由应激触发
- 可复用已有的 `apply_sleep_effects()` 框架

### 难度: ★★☆☆☆ (~25行代码)

---

## 4. 长期嗅觉适应 (Long-term Olfactory Adaptation)

### 现状
- ChemoTransducer 有 fast_tau=100ms + slow_tau=5s 双滤波器
- 这只覆盖秒级适应 (Weber-Fechner)
- **缺失**: 分钟/小时级适应 (真正的感觉疲劳)

### 生物学
- AWC 嗅觉适应: 持续暴露 >30min → 对该气味的响应减弱
- 机制: EGL-4 cGMP-dependent kinase → 核定位 → 基因表达改变
- 短期适应 (秒): 离子通道反馈 (已有)
- 长期适应 (分钟): 细胞内信号级联 (缺失)
- 效果: CI 随暴露时间下降, 促进新区域探索
- REF: Colbert & Bargmann 1995, L'Etoile 2002 Neuron, Cho 2016

### 实现
- 在 ChemoTransducer 中加第三个超慢滤波器 `ultra_slow_tau_=300s`
- 长期暴露 → gain 逐渐降低 → 响应减弱
- 影响 AWC/AWA (嗅觉) 但不影响 ASE (味觉)

### 难度: ★★☆☆☆ (~20行代码)

---

## 5. ADL 信息素场采样 (Pheromone Field Sensing)

### 现状
- ADL 有 ChemoTransducer (ON, gain=15, 采样 chemical field)
- 信息素场 `pheromone_field_` 已有 (Step 64)
- **缺失**: ADL 不采样信息素场, 只采样食物挥发物

### 生物学
- ADL 是主要的信息素/ascaroside 感觉神经元
- 检测 ascr#3 (C9), ascr#5 (C3) → 回避行为
- ADL→AVA: 直接反转驱动; ADL→RMG: 社会整合
- 对聚集/扩散行为至关重要
- REF: Jang 2012, Troemel 1997, Macosko 2009

### 实现
- 在 `apply_sensory_input()` 中加 ADL 信息素采样
- `pheromone_input = max(repellent_response, pheromone_response)`
- ADL 是多模态: 化学斥力 + 信息素, 取最大

### 难度: ★★☆☆☆ (~15行代码)

---

## 6. 嗅觉联想学习 (Olfactory Associative Conditioning)

### 现状
- 已有: 盐学习 (ASER w_mod, Step 21c), 病原体学习 (AWC→AIY/AIB flip, Step 26)
- **缺失**: 经典的丁酮适应范式 (butanone + 饥饿 → 学习回避)

### 生物学
- 经典范式: 暴露于丁酮 30min + 无食物 → 对丁酮的趋化性下降 (CI 0.8→0.2)
- 机制: AWC 细胞自主适应 (EGL-4/PKG) + AIA→AWC 反馈 (INS-1/DAF-2)
- 与病原体学习不同: 不需要吃毒食物, 只需要 "气味+饥饿" 配对
- 恢复: 回到食物上 ~30min 恢复 (w_mod 漂移回 1.0)
- REF: Colbert & Bargmann 1995, Cho 2016 Cell Rep, Torayama 2007

### 实现
- 复用 `awc_syn_indices_` + w_mod 机制
- 学习信号: `-(1-satiety) × awc_activity` (饥饿时 AWC 活跃 → w_mod↓)
- 与病原体学习共存但独立 (不需要 sickness)
- 效果: 长时间嗅到食物但吃不到 → AWC 输出减弱 → CI 下降

### 难度: ★★★☆☆ (~30行代码)

---

## 实现优先级排序

| 排名 | 行为 | 难度 | 新代码量 | 依赖 |
|------|------|------|---------|------|
| 1 | PVM 后体轻触 | ★☆ | ~5行 | 无 |
| 2 | FLP 热伤害 | ★★ | ~15行 | 温度场已有 |
| 3 | ADL 信息素采样 | ★★ | ~15行 | 信息素场已有 |
| 4 | 长期嗅觉适应 | ★★ | ~20行 | ChemoTransducer |
| 5 | ALA 应激静止 | ★★ | ~25行 | ALA 神经元已有 |
| 6 | 嗅觉联想学习 | ★★★ | ~30行 | awc_syn_indices 已有 |

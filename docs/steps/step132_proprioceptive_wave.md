# Step 132: 本体感觉波传播 (Boyle & Cohen 2012)

## 动机

用户指出虫体"基本上一直是直的"，即使 muscle_gain 提高到 8.0 后仍然如此。
对比 OpenWorm Sibernetic，我们的虫体缺乏明显的 S 形弯曲波。

## 根因分析

研究 Boyle & Cohen 2012 (Front Comput Neurosci) 论文后发现：

**C. elegans 体波的核心机制是 B 类运动神经元的本体感觉反馈：**

1. B 类神经元 (DB/VB) 是**双稳态**的，具有滞后特性
2. B 类神经元长突起上有假设的**stretch receptor（拉伸受体）**
3. 当前方体节弯曲时，拉伸信号传到后方 B 神经元 → 超过阈值 → 触发该段弯曲
4. 波从头部（SMD驱动）向尾部传播
5. 肌肉作为漏积分器，τ_M = 100ms

**我们的模型缺失：**
- DB/VB 只收到 AVB 的恒定驱动，**没有本体感觉输入**
- 背腹两侧同时激活（共收缩）→ 差值趋零 → 无弯曲
- 对称扩散无法产生定向波传播

## 实现

### 定向本体感觉耦合

```
头部 (seg 0-3): target = muscle_gain × (dorsal - ventral)
                SMD 振荡直接驱动，是波的起源

体部 (seg 4-47): target = anterior_curv × amp_gate + 0.3 × muscle_target
                 amp_gate = min(1.0, muscle_amp × 2.0)
                 本体感觉（前段曲率）主导，肌肉活动门控振幅
```

### 关键参数
- `prop_coupling_ = 12.0`: 本体感觉耦合强度（替代 body stiffness 用于体段）
- `prop_tau_ = 0.1`: 肌肉积分时常 100ms (Boyle 2012 Table 3)
- `curvature_diffusion_ = 0.3`: 降低对称扩散（避免干扰定向传播）
- `max_curv = 5.0`: 提高正常运动曲率上限（3.0→5.0）

### 机制说明

1. SMD 头部振荡产生 ±1-2/mm 头部曲率
2. Seg 4 感受 seg 3 的弯曲 → 通过本体感觉耦合跟随
3. DB/VB 的恒定激活作为"就绪"门控（amp_gate）
4. 没有肌肉活动 → 段不响应本体感觉 → 波停止
5. 波从头到尾传播，每段有 ~100ms 时间常数延迟

## 验证
- regtest: 20/20 通过
- 头部曲率: 1.1/mm
- 体中曲率: 3.7/mm（从 3.0 提升，生物学范围 3-5/mm）
- 速度: 0.2 mm/s（不变）
- 曲率稳定性: 2.2 Hz（正常）

## 参考文献
- Boyle JH, Berri S, Cohen N. 2012. Gait modulation in C. elegans: an integrated neuromechanical model. Front Comput Neurosci 6:10
- Wen Q, Po MD, Bhatt DK, et al. 2012. Proprioceptive coupling within motor neurons drives C. elegans forward locomotion. Neuron 76:750-761
- Fang-Yen C, et al. 2010. Biomechanical analysis of gait adaptation in the nematode C. elegans. J Exp Biol 213:2244-2253

## 修改文件
- `src/body/body_model.h`: 添加 prop_coupling_, prop_tau_ 参数
- `src/body/body_model.cpp`: compute_curvatures 重写为本体感觉波传播

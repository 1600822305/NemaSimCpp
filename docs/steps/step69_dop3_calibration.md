# Step 69: DOP-3 校准 + DA 速度调控文献研究

## 动机

Step 68 将 basal slowing 从直接速度乘法重构为 DA→DOP-3→B-class 运动神经元通路，
CI 从 0.45 降至 0.155。需要确定是否可通过参数调优恢复 CI。

## 文献研究

### Chase 2004 (Nature Neuroscience) — 核心机制确认
- DOP-3 (D2-like) → **Gαo (GOA-1)** → 抑制胆碱能运动神经元 ACh 释放
- DOP-1 (D1-like) → **Gαq (EGL-30)** → 兴奋胆碱能运动神经元
- 两者**共表达**于相同运动神经元，**不是** DA 突触后靶标 → 体外突触传递
- dop-1 KO 可挽救 dop-3 KO → 拮抗关系

### Vidal-Gadea 2012 (PLOS One) — DOP-1 不参与速度调节
- DA 对**速度精度维持**至关重要（限制速度波动幅度）
- **DOP-1 不参与速度波动调节** — 只参与食物依赖减速
- 速度精度和食物依赖减速是 DA **独立调控**的两个功能
- 只有 **DOP-3 + GOA-1** 对速度精度必要
- DA 以**体液方式**发挥作用（外源 DA 完全挽救 cat-2）

### Wang 2014 (PLOS One) — 行为选择中的拮抗
- DOP-1 在**胆碱能神经元**中功能，DOP-3 在 **GABA 能神经元** + RIC + SIA 中功能
- 两者都不在命令中间神经元（AVA/AVB）中表达
- DOP-3 拮抗 DOP-1 在行为选择中的功能

## 参数探索

### 实验 1: 降低 release_threshold + 提高 DOP-3 增益
- 配置: release_threshold 0.3→0.1, DOP-3 -3→-5 pA
- 结果 (4-seed, 300s):
  - CI: -0.049, 0.055, 0.007, -0.016 → **均值 ~0.00**
  - near_food: ~21%
- 结论: **CI 恶化**，过度抑制破坏运动协调

### 根因分析: 为什么 DOP-3 增强反而降低 CI

```
直接 speed_scale:
  保留肌肉激活模式 → 波形质量不变 → 纯速度降低
  蠕虫变慢但转向能力完好 → 食物捕获效率高

DOP-3 运动神经元抑制:
  降低 B-class 兴奋性 → MEC 本体感觉通道敏感度降低
  → 波传播受损 → 运动不协调 → 转向受损
  蠕虫既变慢又失去方向控制 → 趋化效率崩溃
```

**核心限制**: 当前 body model 的 B-class 运动神经元同时承载
本体感觉 (MEC) + DA 调控 (DOP-3) 两个功能。抑制 DOP-3 会
同时削弱本体感觉反馈，破坏前进波协调性。

### 最终配置
- release_threshold: 0.3 (保持原值)
- DOP-3 gain: -3 pA (安全值，不干扰波协调)
- 结论: DOP-3 通路在建筑上正确，但当前 body model 限制了
  涌现减速的有效性。完整的涌现减速需要 body model 改进
  (肌肉作为独立计算节点，分离本体感觉和运动输出)。

## 修改文件列表

| 文件 | 修改内容 |
|------|---------|
| `src/simulation/setup_neuromodulation.cpp` | DOP-3 注释更新 (参数值不变) |

## 验证结果

### Regtest: 17/17 ✅

### CI 保持 Step 68 水平: 均值 0.155 (4-seed)

## 未来改进方向

1. **Body model 升级**: 肌肉作为独立计算节点（目前 muscle_work 直接从
   运动神经元电压差计算），分离运动输出和本体感觉反馈
2. **SYNAPSE_GAIN 调控**: 需要运动神经元→肌肉的突触连接（目前是直接映射）
3. **DOP-1 兴奋性平衡**: 添加 DOP-1 到运动神经元后，DOP-3 可适度提高
4. **CEP 驱动校准**: 解耦 CEP↔OLQ 间隙连接后可增加 CEP 驱动 → 更高 DA

## 参考文献

- Chase 2004 Nat Neurosci — DOP-3 extrasynaptic on motor neurons
- Vidal-Gadea 2012 PLOS One — DOP-3+GOA-1 essential for speed precision
- Wang 2014 PLOS One — DOP-1/DOP-3 antagonistic behavioral choice
- Sawin 2000 Neuron — BSR ~30% reduction, cat-2 mutants

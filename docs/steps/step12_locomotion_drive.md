# Step 12: 运动驱动 — 线虫蠕动前进

> 日期: 2026-02-10
> 状态: ✅ 完成

---

## 目标

让虚拟线虫产生自主蠕动前进，从静止网络变为运动中的生物体。

## 生物学背景

真实 C. elegans 的前进运动由三个机制协同产生：

1. **命令中间神经元 AVB 的持续激活**：AVB 在前进状态下保持活跃，通过化学突触驱动 B 类运动神经元 (DB/VB)。AVA 在前进时相对抑制，仅在后退时被激活 (Chalfie 1985, Zheng 1999)。

2. **头部背腹交替振荡**：头部运动神经元 SMD/RMD 产生 ~0.8 Hz 的交替收缩，启动蠕动波。这可能由内在振荡电路或肌肉-神经元交互产生。

3. **本体感觉反馈驱动波传播**：身体曲率的变化通过拉伸敏感机制传递给后方的 B 类运动神经元。当前方段弯曲时，后方段的对侧运动神经元被激活，使弯曲波从头到尾传播 (Wen 2012, Izquierdo & Beer 2018)。

## 实现

### 1. AVB Tonic 驱动

```cpp
// SimulationEngine::apply_tonic_drive()
neurons_[avbl_id_]->set_external_current(20.0);  // pA, 前进偏置
neurons_[avbr_id_]->set_external_current(20.0);
neurons_[aval_id_]->set_external_current(3.0);   // 较弱, 前进时抑制
neurons_[avar_id_]->set_external_current(3.0);
```

AVB 20pA 将膜电位从 -52.8 mV 推到 -36.9 mV，释放率从 ~2% 升至 ~70%。
AVA 3pA 仅维持低活性 (-48.1 mV, release ~5%)，确保 A 类运动神经元不与 B 类竞争。

### 2. 头部 SMD 正弦振荡

```cpp
// SimulationEngine::apply_head_oscillation()
double I_dorsal = 15.0 * sin(phase) + 2.0;   // SMD dorsal
double I_ventral = -15.0 * sin(phase) + 2.0;  // SMD ventral (反相)
// phase += 2π × 0.8Hz × dt
```

- 频率: 0.8 Hz (真实爬行频率 0.5-1.5 Hz)
- 幅度: 15 pA (足以驱动 SMD 背腹交替发放)
- Tonic 偏移: +2 pA (确保双侧都有基础活性)

### 3. 本体感觉反馈

```cpp
// SimulationEngine::apply_proprioceptive_feedback()
// DB (背侧): 前方段腹弯(负曲率) → 激活
I_proprio = -curvature_anterior * 40.0;  // 仅正值(兴奋性)
// VB (腹侧): 前方段背弯(正曲率) → 激活
I_proprio = curvature_anterior * 40.0;   // 仅正值
```

- 采样位置: 该运动神经元支配区域前方 3 段
- 增益: 40 pA / 曲率单位
- 单向: 仅兴奋性反馈 (负值截断为 0)

### 映射表

| 运动神经元 | 采样段 | 支配段 | 方向 |
|-----------|--------|-------|------|
| DB01 | seg 0 | 2-10 | 背侧 |
| DB02 | seg 5 | 8-20 | 背侧 |
| DB03 | seg 15 | 18-30 | 背侧 |
| VB01 | seg 0 | 2-10 | 腹侧 |
| VB02 | seg 5 | 8-20 | 腹侧 |
| VB03 | seg 15 | 18-30 | 腹侧 |

## 仿真主循环更新

```
1. 环境更新
2. Tonic 驱动 (AVB/AVA)           ← NEW
3. 头部振荡 (SMD sin/cos)          ← NEW
4. 本体感觉反馈 (曲率→DB/VB)       ← NEW
5. 突触电流计算 (化学+电突触)
6. 神经元膜电位更新
7. 运动输出 (运动神经元→肌肉)
8. 身体物理
9. 回调
```

## 验证结果

| 指标 | Step 11 | Step 12 | 目标 |
|------|---------|---------|------|
| V_AVB | -52.8 mV | **-36.9 mV** | 激活态 |
| V_AVA | -51.7 mV | **-48.1 mV** | 抑制态 |
| 速度 | 0 mm/s | **0.01-0.06 mm/s** | ~0.2 mm/s |
| 振荡 | 无 | **0.8 Hz** | 0.5-1.5 Hz |
| 5s 位移 | 0 mm | **0.2 mm** | ~1 mm |
| 运动方向 | 无 | **向右 (heading=0)** | — |

### 速度振荡模式

```
t(ms)   speed(mm/s)
500     0.024
1000    0.052
1500    0.062    ← 波峰
2000    0.057
2500    0.011    ← 波谷 (头部振荡过零)
3000    0.024
3500    0.052    ← 周期重复
```

周期 ~1250 ms = 0.8 Hz，与头部振荡器完全匹配。

## 后续调优方向

- 速度仍比真实线虫慢 ~4×，可能需要增大身体模型速度增益或优化通道动力学
- 当前本体感觉反馈仅覆盖 B 类前 3 对，完整 302 连接组有 11 对 B 类
- DD/VD 交叉抑制应由网络突触自发产生，当前可能不够强

## 参考文献

- Chalfie et al. 1985. "The neural circuit for touch sensitivity in C. elegans"
- Zheng et al. 1999. "Neuronal control of locomotion in C. elegans"
- Wen et al. 2012. "Proprioceptive coupling within motor neurons drives C. elegans forward locomotion"
- Izquierdo & Beer 2018. "From head to tail: a neuromechanical model of forward locomotion"

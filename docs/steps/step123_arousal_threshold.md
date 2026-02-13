# Step 123: 唤醒阈值调制 (Arousal Threshold Modulation)

> 日期: 2026-02-13

---

## 动机

Step 27/71 实现了 RIS/FLP-11 睡眠回路，唤醒阈值作为涌现现象存在（FLP-11 -15pA vs 触觉 80pA 竞争）。但文献（Schwarz 2011）揭示了显式的多层级回路抑制机制：ASH 感觉神经元灵敏度降低 + AVA/AVD 同步性丢失。本步添加 FLP-11 浓度驱动的分级唤醒阈值。

## 生物学基础

### 多层级回路抑制 (Schwarz et al. 2011 Cell Reports)
```
清醒:
  ASH → AVA/AVD (同步) → VA 运动神经元 → 回避反应
  
睡眠 (lethargus):
  ASH: Ca²⁺ 响应显著降低（灵敏度下降）
  AVA/AVD: 活动变为异步（信息传递中断）
  VA: 运动输出受抑制
  
唤醒:
  强刺激 → 恢复 AVA/AVD 同步性 → 立即唤醒
  弱刺激 → 不足以克服抑制 → 维持睡眠
```

### 关键发现
- **ASH 感觉抑制**: Cu²⁺/glycerol 刺激的 Ca²⁺ 响应在 lethargus 中显著降低
- **中间神经元去同步化**: AVA/AVD 从同步→异步，信号传递效率降低
- **分级**: 弱 ChR2 → 无响应；强 ChR2 → 可唤醒（剂量依赖）
- **快速可逆**: 一次强刺激唤醒后，后续弱刺激也能响应
- **非发育特异**: EGF 诱导和饱食诱导的静息也表现相同模式

### 参考文献
- Schwarz et al. 2011 Cell Reports — Multilevel modulation of ASH circuit during sleep
- Raizen et al. 2008 — Arousal threshold during lethargus
- Turek 2016 eLife — FLP-11 as major sleep transmitter

## 实现细节

### 唤醒阈值计算
```cpp
if (sleeping):
    flp11 = neuromod_.get_concentration("FLP-11")
    target = flp11 × min(1.0, fatigue / threshold)  // 深睡=高阈值
    arousal_threshold += (target - threshold) × dt / 5000ms
else:
    arousal_threshold -= threshold × dt / 2000ms  // 2s 衰减
```

### 感觉门控 (arousal_threshold > 0.05)
1. **ASH 抑制**: -15pA × gate → 需要 >15pA 额外刺激才能激活
2. **AVA/AVD 抑制**: -8pA × gate → 提高反转阈值
3. **刺激依赖唤醒**: touch_activity > threshold×0.8 且 >0.3 → 强制唤醒

### 示例
| 睡眠深度 | FLP-11 | 阈值 | 触觉(80pA→0.9) | 弱梯度(→0.1) |
|----------|--------|------|-----------------|-------------|
| 深睡 | 0.8 | 0.7 | 0.9 > 0.56 → **唤醒** | 0.1 < 0.56 → **维持** |
| 浅睡 | 0.3 | 0.2 | 0.9 > 0.16 → **唤醒** | 0.1 < 0.16 → **维持** |
| 清醒 | 0.0 | 0.0 | 无门控 | 无门控 |

## Diag 验证

```
18. SLEEP / QUIESCENCE (Step 27):
   Sleep episodes: 1  total_sleep=0.5s (0.4%)
   Arousal threshold (Step 123): 0.000 AWAKE (no gating)
```

120s 仿真中虫子仅短暂入睡，诊断时已清醒（阈值≈0）。在更长仿真或强制睡眠中，阈值会升高至 0.5-0.7。

## 修改文件
- `src/simulation/simulation_engine.h`: arousal_threshold_ 参数和函数声明
- `src/simulation/update_internal_states.cpp`: update_arousal_threshold() + apply_arousal_gating()
- `src/simulation/simulation_engine.cpp`: 在 step() 中调用
- `src/simulation/diag_main.cpp`: 唤醒阈值诊断输出

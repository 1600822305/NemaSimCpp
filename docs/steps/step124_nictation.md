# Step 124: Nictation — Dauer 特异摆动散布行为

> 日期: 2026-02-13

---

## 动机

Nictation 是 Dauer 幼虫的标志性行为：虫子用尾部站立，在空中摆动头部以寻找宿主或散布。Step 122 实现了 Dauer 决策通路，本步添加 Dauer 特异的运动模式。

## 生物学基础

### Nictation 行为 (Lee et al. 2011 Nat Neurosci)
```
Dauer 状态激活:
  IL2 感觉神经元 (6个, 内唇) → 机械感觉 (OSM-9)
  ↓ (Dauer 特异树突分支增强)
  RIG 中间神经元 (Dauer 连接增强)
  ↓ (胆碱能传递)
  SMD 头部运动神经元 → 大振幅头摆
  AVB/AVA 抑制 → 周期性运动暂停
```

### 运动模式
| 相位 | 持续时间 | 行为 | 2D 模拟 |
|------|----------|------|---------|
| 摆动 (Waving) | ~4s | 头部大振幅波动 | IL2/RIG/SMD 激活 |
| 站立 (Standing) | ~4s | 尾端站立，无运动 | AVB/AVA 抑制 |

### 关键神经元
- **IL2** (4×四象限 + 2×侧向): Dauer 必需，消融减少 nictation，光遗传激活促进
- **RIG** (L/R): IL2 下游，Dauer 连接增强，消融减少 nictation
- **ASG/ASI/ASJ**: 通过 DAF-7/TGF-β 调节 nictation

### 参考文献
- Lee et al. 2011 Nat Neurosci — IL2 调控 nictation
- Yim et al. 2024 — RIG 下游 Dauer 特异重连
- Cassada & Russell 1975 — Dauer 行为特征

## 实现细节

### 激活条件
```cpp
if (!is_dauer()) return;  // dauer_signal > 0.8
```

### 周期 (8s = 4s wave + 4s stand)
```cpp
nictation_waving_ = (timer < period × 0.5)
```

### 摆动相位
- IL2 → +12pA (Dauer 特异激活)
- RIG → +8pA (下游驱动)
- SMD → +6pA (增强头摆振幅)

### 站立相位
- IL2 → +3pA (基础维持)
- AVB → -12pA (抑制前进)
- AVA → -5pA (抑制后退)

## Diag 验证

```
38. DAUER FORMATION (Step 122):
   dauer_signal=0.202  is_dauer=no
   Nictation (Step 124): inactive (not dauer)
```

默认有食物场景中不进入 Dauer，nictation 正确不激活。长时间无食物仿真或高信息素环境下会触发 Dauer→nictation。

## 修改文件
- `src/simulation/simulation_engine.h`: nictation 参数和函数声明
- `src/simulation/update_internal_states.cpp`: apply_nictation()
- `src/simulation/simulation_engine.cpp`: 在 step() 中调用
- `src/simulation/diag_main.cpp`: nictation 状态诊断

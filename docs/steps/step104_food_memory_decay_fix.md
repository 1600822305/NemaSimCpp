# Step 104: 修复 food_memory 病态衰减过快 (ARS 抑制时间尺度)

> 日期: 2026-02-13

---

## 动机

diag 输出显示 food_memory 在 300s 内从 0.635 指数衰减到 1e-24，远快于预期的 tau_decay=300s。
这是因为当 sickness > 0.3 时，衰减 tau 被设为 5s（5000ms），导致 food_memory 在病态条件下以 60 倍速度归零。

## Bug 分析

### 原代码逻辑
```cpp
if (sickness_ > 0.3) {
    effective_decay_tau = 5000.0;  // 5s — 太激进！
}
```

### 时间线 (diag 输出)
| t(s) | sickness | food_memory | 说明 |
|------|----------|-------------|------|
| 20 | 0.195 | 0.635 | sickness < 0.3，正常衰减 |
| 40 | 0.599 | 0.041 | sickness > 0.3，tau=5s 生效 |
| 60 | 1.000 | 0.00075 | 极速衰减 |
| 80 | 1.000 | 0.000014 | |
| 300 | 0.791 | 1e-24 | 完全归零 |

### 数学验证
- tau=5s: 每秒衰减因子 = e^(-1/5) ≈ 0.82
- 从 t=30s (sickness>0.3) 到 t=300s (270s): fmem × 0.82^270 ≈ fmem × 10^(-23) ✓

## 生物学依据

### Hills 2004 (J Neurosci)
- ARS 的 local→global 搜索转换在正常条件下需要 **5-15 分钟**
- DA → DARPP-32 磷酸化 → GLR-1 增强 → 反转率升高
- tau_decay=300s (5min) 匹配实验数据

### 病态条件下的 ARS 抑制
- **Lei 2024 (Frontiers Immunol)**: 病原体厌恶学习在**分钟到小时**尺度发展
- **Zhang 2005 Nature**: 学习性厌恶需要数分钟暴露
- **PMC11335288 foraging review**: patch-leaving 受病原体调制，但在行为时间尺度（分钟级）
- 生物逻辑：生病后不应在有毒食物附近做 local search，但抑制应在 1-2 分钟内完成

### 修复值选择: 60s
- 正常 off-food: tau_decay = 300s (5 min, Hills 2004)
- 生病 (sickness > 0.3): tau_decay = 60s (1 min)
- 5× 加速衰减 — ARS 在 ~2 分钟内被抑制
- 不再出现 1e-24 的极端值

## 修复

```cpp
// Step 104: sickness → suppress ARS (don't linger near toxic food)
// Was 5000ms (5s) — too aggressive, food_memory dropped to 1e-24 in 300s
// 60s gives ~2 time constants in 2min: fmem → ~13% of peak
// REF: Zhang 2005 Nature — learned aversion develops over minutes
if (sickness_ > 0.3) {
    effective_decay_tau = 60000.0;
}
```

### 修复后预期 (tau=60s)
| t(s) | food_memory 预期 |
|------|-----------------|
| 30 | ~0.5 (sickness 刚超 0.3) |
| 60 | ~0.3 (1 tau 后) |
| 120 | ~0.07 (2 tau 后, ~13%) |
| 180 | ~0.01 (~2%) |
| 300 | ~0.001 (合理的低值) |

## 附加修复

### Bottleneck 阈值更新
`diag_main.cpp` 中 curvature bottleneck 阈值从 0.1 改为 0.04，匹配 Step 102-103 新增 12 个头部运动映射后的预期值。

### cli_nseeds 默认值恢复
`diag_main.cpp` 中 `cli_nseeds` 从 4 恢复为 1，无参数运行时显示完整详细诊断输出。

## 修改文件

- `src/simulation/update_internal_states.cpp`: sickness decay tau 5000→60000
- `src/simulation/diag_main.cpp`: bottleneck curvature 0.1→0.04 + cli_nseeds 4→1
- `src/simulation/regression_test.cpp`: curvature amplitude baseline 0.14→0.06

## 参考文献

- Hills 2004 J Neurosci — ARS 时间尺度 (DA/GLR-1, 5-15min)
- Zhang 2005 Nature — 学习性厌恶时间尺度 (分钟级)
- Lei 2024 Frontiers Immunol — 病原体回避机制综述
- PMC11335288 — C. elegans 觅食决策综述

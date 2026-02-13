# Step 126: 外源 5-HT 诱导产卵 (Exogenous Serotonin Egg-Laying)

> 日期: 2026-02-13

---

## 动机

外源 5-HT（浴液施加 2mM）是 C. elegans 产卵研究的经典实验协议。5-HT 绕过正常的卵压力阈值，直接激活 HSN/VC 回路，导致产卵率大幅增加（~5×）。添加 `--exo-5ht` CLI 参数实现此协议。

## 生物学基础

### 正常产卵机制 (Step 38)
```
卵积累 → egg_pressure↑ → 超过阈值(0.7)
→ HSN 爆发 → 5-HT 释放 → VC 激活 → 产卵
→ egg_pressure 重置 → 下一轮积累
```

### 外源 5-HT (Trent 1983, Schafer 2006)
```
浴液 2mM 5-HT
→ 绕过 egg_pressure 阈值
→ HSN 持续激活 (80% max drive)
→ VC 持续兴奋
→ 产卵率 ~5× 增加
→ TA 抑制仍然有效 (LGC-55 反馈)
```

### 参考文献
- Trent et al. 1983 — 外源 5-HT 诱导产卵的首次报告
- Schafer 2006 — 产卵回路综述
- Collins 2016 eLife — HSN/VC/uv1 产卵回路完整解析

## 实现细节

### CLI 参数
```
--exo-5ht    Apply exogenous serotonin (egg-laying induction)
```

### 代码修改
```cpp
// 1. HSN 强制激活 (绕过 egg_pressure 阈值)
if (exo_5ht_) {
    I_hsn = max(I_hsn, hsn_egg_gain_ * 0.8);  // 80% max drive
    hsn_sigmoid = max(hsn_sigmoid, 0.8);        // 确保 active state 触发
}

// 2. 产卵事件: exo-5ht 绕过 egg_pressure 检查
if (egg_pressure_ > threshold || exo_5ht_) {
    egg_laid_count_ += 1;
}
```

## Diag 验证

### 默认 (120s, 无外源 5-HT)
```
25. EGG-LAYING (Step 38):
   egg_pressure: final=0.632  range=[0.001, 0.632]
   eggs_laid: 0
   HSNL: V mean=-42.6 mV  S(release)=0.178
```

### --exo-5ht (120s)
```
25. EGG-LAYING (Step 38):
   egg_pressure: final=0.115  range=[0.001, 0.115]
   eggs_laid: 59
   HSNL: V mean=-30.8 mV  S(release)=0.698
```

- **HSN 激活**: -42.6 → -30.8 mV, release 0.18 → 0.70 (+289%)
- **产卵率**: 0 → 59 eggs/120s (~0.5 eggs/s)
- **egg_pressure**: 保持低位 (0.115) 因为不断被重置

## 修改文件
- `src/simulation/simulation_engine.h`: exo_5ht_ 标志 + setter/getter
- `src/simulation/apply_sensory_systems.cpp`: HSN 强制驱动 + 产卵阈值绕过
- `src/simulation/diag_main.cpp`: --exo-5ht CLI 参数解析 + 帮助文本

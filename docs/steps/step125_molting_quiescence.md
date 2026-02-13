# Step 125: 蜕皮静息 / Lethargus (Molting Quiescence)

> 日期: 2026-02-13

---

## 动机

C. elegans 在每次蜕皮前进入 lethargus（蜕皮静息）— 一种发育性睡眠状态。此前 Step 27/71 实现了疲劳驱动的睡眠和 FLP-11 系统，但缺少蜕皮激素周期驱动的周期性 lethargus。本步添加 LIN-42/Period 振荡器和类固醇激素通路。

## 生物学基础

### 蜕皮周期
```
LIN-42/Period 振荡器
  ↓ (类昼夜节律时钟)
ecdysone-like 类固醇激素上升
  ↓ (激素峰值)
RIS 激活 → FLP-11 释放 → 全身抑制
ALA 激活 → FLP-13 → AVE 抑制 → 运动停止
MC 抑制 → 咽泵停止（咽部重塑）
  ↓
LETHARGUS: 运动+进食抑制 (~20% 的周期)
  ↓ (激素下降)
蜕皮完成 → 清醒 → 恢复正常行为
```

### 四次蜕皮
- L1→L2, L2→L3, L3→L4, L4→成虫
- 每次蜕皮前 1-2hr lethargus（总周期 6-8hr）
- 仿真压缩: 200s 周期，~40s lethargus

### 关键分子/神经元
- **LIN-42**: Period 同源物，mRNA 随蜕皮周期振荡 (Monsalve 2011)
- **RIS**: 释放 FLP-11 → 系统性静息 (Turek 2016)
- **ALA**: 释放 FLP-13 → AVE 抑制 → 运动停止 (Katz 2018)
- **CEPsh glia**: 调节 ALA→AVE 突触，防止过早进入睡眠 (Katz 2018)

### 参考文献
- Raizen 2008 Nature — lethargus 满足睡眠所有标准
- Monsalve 2011 Curr Biol — LIN-42/Period 控制蜕皮时序
- Katz 2018 Cell Rep — CEPsh glia 调节 ALA→AVE 抑制
- Singh 2011 Curr Biol — Notch 信号在蜕皮静息中的作用

## 实现细节

### LIN-42 振荡器
```cpp
omega = 2π / 200000ms
molt_phase += omega × dt
// 初始相位 = π (避免仿真开始即命中 lethargus)
```

### 类固醇激素
```cpp
cos_phase = cos(molt_phase)
hormone_raw = (cos_phase > 0.8) ? (cos_phase - 0.8) / 0.2 : 0.0  // 窄峰
// 3s 平滑
```

### Lethargus 效应 (hormone > 0.4)
1. **RIS 驱动**: +15pA × hormone → FLP-11 释放 → 全身抑制
2. **ALA 驱动**: +10pA × hormone → AVE 抑制 → 运动停止
3. **MC 抑制**: -20pA × hormone → 咽泵停止
4. **强制睡眠**: hormone > 0.5 → is_sleeping=true

## Diag 验证

### 120s 仿真 (lethargus 活跃)
```
18. SLEEP / QUIESCENCE:
   Sleep episodes: 5  total_sleep=30s (20%)
   Molting cycle (Step 125): hormone=0.282  lethargus=YES
   Arousal threshold (Step 123): 0.759 DEEP
```

### 250s 仿真 (完整周期后)
```
   Sleep episodes: 6  total_sleep=30s (12%)
   Molting cycle (Step 125): hormone=0  lethargus=no
```

蜕皮周期正确产生周期性 lethargus，期间 RIS/ALA 被激活，sleep episodes 增加。

## 修改文件
- `src/simulation/simulation_engine.h`: molt_phase_, molt_hormone_, in_lethargus_ 参数
- `src/simulation/update_internal_states.cpp`: update_molting_cycle()
- `src/simulation/simulation_engine.cpp`: 在 step() 中调用
- `src/simulation/diag_main.cpp`: 蜕皮周期诊断输出

# Step 71: P0-5 DMP 涌现减速 + P0-6 FLP-11 神经调质化

## 动机

`architecture_biology_review.md` 中两个 P0 级违规：
- **P0-5**: DMP 通过 `dmp_speed_factor_` 直接乘法控制速度（God's eye view）
- **P0-6**: FLP-11 睡眠效应通过 `apply_sleep_effects()` 直接电流注入，绕过 `NeuromodulationManager` 框架

## 生物学基础

### P0-5: DMP 减速机制
- **Jiang 2022 Nat Commun**: AVL/DVB 发射同步复合动作电位（UNC-2 Ca²⁺ + EXP-2 K⁺）
- **White 1986**: AVL 轴突贯穿整个腹索，与 D 型运动神经元形成 gap junction
- **Alkema 2015 Sci Rep**: DMP 与运动耦合，产生短暂运动暂停
- **Haspel & O'Donovan 2011**: VD 神经元抑制局部 VA/VB

机制：DMP 期间 AVL/DVB 被强激活（50-70pA）→ GABA 释放 → 抑制附近 B 类运动神经元 → 速度涌现性降低

### P0-6: FLP-11 睡眠
- **Turek 2016 eLife**: FLP-11 是 RIS 的主要睡眠诱导递质（非 GABA），通过体积传递系统性作用
- **Rossi 2025 Current Biology**: DMSR-1 受体发现
  - Gi/o 偶联 GPCR → 抑制性
  - 作用于胆碱能神经元 → 抑制 ACh 释放 → 运动停止
  - RIS 自身也表达 DMSR-1 → 负反馈自抑制 → 限制睡眠时长
  - "抑制胆碱能信号是睡眠所必需的"
- **Konietzka 2020 Nat Commun**: RIS 作为运动停止神经元

## 实现细节

### P0-5 修改

1. **connectome_builder.cpp**: 添加 AVL/DVB → B 类运动神经元抑制突触
   - `AVL → VB05` (1 section), `AVL → DB05` (1 section)
   - `DVB → VB06` (1 section), `DVB → VB07` (1 section)

2. **simulation_engine.h**: 移除 `dmp_speed_factor_` 成员变量

3. **simulation_engine.cpp**: 移除 `effective_speed *= dmp_speed_factor_`

4. **update_internal_states.cpp**: 移除 `update_defecation()` 中所有 `dmp_speed_factor_` 赋值

### P0-6 修改

1. **setup_neuromodulation.cpp**: 添加 FLP-11 为第 7 种神经调质
   - 来源: RIS
   - τ_rise = 2000ms, τ_decay = 8000ms, threshold = 0.3
   - 靶点 (DMSR-1 Gi/o):
     - AVA/AVB 命令中间神经元: -20pA
     - MC 咽部运动神经元: -18pA
     - SMD/RMD 头部运动神经元: -28pA
     - A/B/D 类体壁运动神经元: -42pA
     - SPEED_SCALE: -0.95 (系统性运动抑制)
   - 自抑制 (FRPR-8): RIS -8pA (负反馈 → 睡眠稳态)

2. **update_internal_states.cpp**: `apply_sleep_effects()` 函数体清空（所有效果已转移到 NeuromodulationManager）

3. **simulation_engine.cpp**: 移除 `sleep_speed_factor` 直接乘法

## 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `src/connectome/connectome_builder.cpp` | 添加 AVL/DVB → VB05/DB05/VB06/VB07 抑制突触 |
| `src/simulation/simulation_engine.h` | 移除 `dmp_speed_factor_` 成员变量 |
| `src/simulation/simulation_engine.cpp` | 移除 `dmp_speed_factor_` 和 `sleep_speed_factor` 乘法 |
| `src/simulation/update_internal_states.cpp` | 清空 `apply_sleep_effects()`; 移除 DMP 中 speed_factor 赋值 |
| `src/simulation/setup_neuromodulation.cpp` | 添加 FLP-11 第 7 种调质 (52 个靶点) |

## 验证结果

- 编译: 零错误
- Regtest: 17/17 PASS
- 单种子 (seed=42, 300s, no-toxin): CI=0.373, near_food=23.9%, speed=0.209mm/s
- FLP-11 调质正常: conc=0.067 (清醒), 0.407 (睡眠), 睡眠 1 episode ~20%
- DMP cycles: 2 (正常)
- 神经调质数量: 6 → 7

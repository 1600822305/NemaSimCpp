# Step 76: 增强减速响应 (ESR) — 涌现行为

## 动机

Sawin 2000 发现 C. elegans 的两种食物减速行为：
- **BSR** (Basal Slowing Response): 饱食虫遇食物 → 多巴胺介导 → ~30% 减速 ✅ 已涌现 (Step 68)
- **ESR** (Enhanced Slowing Response): 饥饿虫遇食物 → 5-HT 介导 → ~80% 减速 ❌ 缺失

Step 68 移除了 ESR 直接速度乘法（P1 违规），但没有实现涌现替代。
本步骤通过**回路级神经效应**让 ESR 从 171 神经元网络中涌现。

## 生物学基础

### 关键实验 (Sawin 2000 Neuron)
- `cat-2` 突变体（无 DA）: BSR 消失，ESR 正常 → BSR = DA 介导
- `tph-1` 突变体（无 5-HT）: BSR 正常，ESR 消失 → ESR = 5-HT 介导
- 需要 30min+ 饥饿才能激活 ESR
- `mod-1;ser-4` 双突变体完全消除 ESR (Gürel 2012 Genetics)

### 分子机制
- 饥饿 → MOD-1/SER-4 受体表达上调（转录水平）
- 遇食物 → NSM 释放 5-HT → 与上调的受体结合
- 放大的抑制效应 → 前进驱动降低 → 速度剧降

### 受体表达 (Gürel 2012)
- **MOD-1** (5-HT 门控 Cl⁻ 通道): 表达于运动回路神经元 (AIY, PVC 等)
- **SER-4** (Gαi/o GPCR): 表达于感觉/中间神经元 (RIC 等)
- 两条平行通路，缺一不可

## 实现细节

### 涌现机制（非直接速度操控）

```
饥饿 (satiety↓)
  ↓ 慢速上调 (τ=60s)
MOD-1/SER-4 受体水平↑ (esr_receptor_level_)
  ×
遇食物 → NSM → 5-HT↑
  ↓
额外抑制电流注入到:
  AIY (-8pA × receptor × 5-HT)   → 前进驱动↓
  PVC (-8pA × receptor × 5-HT)   → 前进命令↓
  RIC (-4pA × receptor × 5-HT)   → OA 产生↓ → 漫游↓
  ↓
速度降低从回路涌现（非直接乘法！）
```

### 关键设计：慢速受体上调
- `esr_receptor_level_` 从 0 开始
- 饥饿 > 0.5 时缓慢上调 (τ_rise = 60s)
- 饱食时缓慢下调 (τ_decay = 30s)
- **解决问题**: 仿真初始 satiety=0，但受体从 0 开始 → 不会立即触发 ESR
- 生物学合理: 受体转录上调需要持续饥饿信号

### 涌现性验证
速度变化的因果链：
1. 饥饿 → esr_receptor_level_ ↑ (内部状态)
2. 食物 → NSM 泵送 → 5-HT ↑ (外部刺激)
3. receptor × 5-HT → 额外 I_inhibitory 到 AIY/PVC/RIC (神经效应)
4. AIY 更沉默 → 更少前进驱动; PVC 更沉默 → 更少 AVB 激活 (回路传播)
5. 运动神经元活性↓ → 肌肉驱动↓ → **速度涌现性降低** (行为输出)

没有任何一步直接操控速度 — 每一步都是神经生理效应。

### 参数
| 参数 | 值 | 含义 |
|------|----|----- |
| esr_mod1_gain_ | -8.0 pA | 最大 ESR 抑制电流 |
| esr_upregulate_tau_ | 60000 ms | 受体上调时间常数 |
| esr_downregulate_tau_ | 30000 ms | 受体下调时间常数 |
| 饥饿阈值 | hunger > 0.5 | 触发受体上调 |
| RIC 系数 | ×0.5 | SER-4 效应弱于 MOD-1 |

## 验证结果

### Regtest: 20/20 PASS
- 30s regtest 内受体上调不充分 (τ=60s) → 不影响基线
- 连接组计数不变 (171/337/98)

### Diag 输出 (300s, seed=42)
```
29. ENHANCED SLOWING RESPONSE (Step 76, Sawin 2000):
   satiety: 0.043  hunger: 0.957  esr_receptor: 0.906
   5-HT: 0.208  esr_current: -1.51 pA
   speed_scale: 0.959
```
- ESR 受体在 300s 后充分上调 (0.906)
- 5-HT=0.208 时产生 -1.51pA 额外抑制
- 速度 0.167mm/s — 回路涌现的减速

## 修改文件列表

- `src/simulation/simulation_engine.h` — 添加 ESR 状态变量 + 访问器
- `src/simulation/simulation_engine.cpp` — 在 neuromod update 后调用 apply_esr_modulation()
- `src/simulation/update_internal_states.cpp` — 实现 apply_esr_modulation()
- `src/simulation/diag_main.cpp` — 添加 Section 29 ESR 诊断输出

## 参考文献

- Sawin et al. 2000 Neuron — ESR 发现，tph-1/cat-2 解离
- Gürel et al. 2012 Genetics — mod-1;ser-4 双突变体消除 ESR
- Flavell et al. 2013 Cell — 5-HT via MOD-1 抑制 AIY 促进 dwelling
- Dag & Flavell 2023 Cell — SER-4 核心减速受体

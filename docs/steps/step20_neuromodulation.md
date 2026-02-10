# Step 20: 神经调质层 (Layer 6) — 行为状态切换

## 目标

实现 8 层架构中第 6 层——"无线连接组"。神经调质通过体积传递 (volume transmission) 在秒级时间尺度上调制全脑活动，驱动行为状态切换 (roaming ↔ dwelling)。

## 生物学背景

### 行为状态 (Flavell 2013 Cell)

| 状态 | 速度 | Reversal 率 | Pirouette | 驱动调质 |
|------|------|-------------|-----------|----------|
| **Roaming** (探索) | 高 (~0.2 mm/s) | 低 | 低 | PDF 神经肽 |
| **Dwelling** (开采) | 低 (~0.1 mm/s) | 高 | 高 | **5-HT** |

切换是 abrupt 且持续分钟到小时。

### 单胺调质系统

| 调质 | 源神经元 | 数量 | 核心功能 | 参考 |
|------|---------|------|---------|------|
| **5-HT** | NSM, ADF, HSN | ~8 | 食物→dwelling, 减速 | Flavell 2013 |
| **DA** | CEP, ADE, PDE | 8 | 食物检测, basal slowing | Sawin 2000 |
| **Tyramine** | RIM | ~4 | 抑制头部振荡 | Alkema 2005 |
| **Octopamine** | RIC | ~2 | 饥饿响应 | Chase 2007 |

### 关键通路

1. **NSM → 5-HT → MOD-1(AIY) → dwelling**
   - NSM 咽部神经元检测细菌食物 (tonic 响应)
   - 5-HT 通过 MOD-1 (Cl⁻ 通道) 抑制 AIY
   - AIY↓ → AVB↓ → 前进减少 → dwelling 状态
   - REF: Flavell 2013 — NSM drives dwelling via serotonin

2. **CEP → DA → DOP-3 → basal slowing response**
   - CEP(4) 头部纤毛神经元机械检测细菌
   - DA 通过 DOP-3 (D2-like, 抑制性) 降低运动速度
   - 效果: 在食物上减速 20-30%
   - REF: Sawin 2000 — cat-2 mutants (无 DA) 不在食物上减速

## 实现

### 1. 框架: NeuromodulationManager

新建 `src/neuromodulation/neuromodulation.h/.cpp`

```
核心数据结构:
  Neuromodulator {
    name: "5-HT" / "DA"
    source_neuron_ids: 释放该调质的神经元
    targets: [{neuron_id, receptor, effect_type, strength}]
    concentration: 当前胞外浓度 [0, 1]
    tau_rise: 上升时间常数 (ms)
    tau_decay: 降解时间常数 (ms)
    release_threshold: 源神经元 release 必须超过此值
  }

  ModulationEffect:
    EXCITABILITY    → tonic 电流 (pA)
    SYNAPSE_GAIN    → 突触权重倍率
    SPEED_SCALE     → 全局速度调制
    REVERSAL_RATE   → 全局 reversal 率调制
```

### 2. 新增神经元

| 神经元 | 类型 | 神经递质 | 感觉模式 |
|--------|------|---------|---------|
| NSML/R | 感觉 | 5-HT | TONIC (绝对浓度) |
| CEPDL/DR/VL/VR | 感觉 | DA | TONIC (绝对浓度) |

共 6 个新神经元 (64→70 总)。

### 3. TONIC 感觉转导

新增 `ChemoTransducer::ResponseType::TONIC`:
- 与 ON/OFF 不同: 响应**绝对浓度**，不是浓度时间导数
- 饱和函数: `sat = C / (C + 0.1)` (半最大值 C=0.1)
- NSM: gain=30, baseline=1 pA, fast_tau=500ms
- CEP: gain=20, baseline=1 pA, fast_tau=500ms

### 4. 调质配置

**5-HT (血清素)**:
- 源: NSM L/R
- tau_rise: 3000ms (3s 累积)
- tau_decay: 8000ms (8s 持续)
- release_threshold: 0.3
- 靶点:
  - AIY L/R via MOD-1: -5 pA (抑制性 Cl⁻ 通道)
  - 全局 SPEED_SCALE: -0.15 (15% 减速)

**DA (多巴胺)**:
- 源: CEP DL/DR/VL/VR
- tau_rise: 2000ms
- tau_decay: 5000ms
- release_threshold: 0.3
- 靶点:
  - 全局 SPEED_SCALE: -0.25 (25% 减速)

### 5. 仿真集成

在 `SimulationEngine::step()` 中:
```
  5. compute_synaptic_currents()
  5a. apply_smb_neck_bias()
  5b. neuromod_.update(neurons_, dt_)     ← 新增
      body_.set_speed_scale(params.speed_scale * neuromod_.get_speed_scale())
  6. neuron->step(dt_)
```

## 参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 5-HT tau_rise | 3000 ms | 慢累积 (volume transmission) |
| 5-HT tau_decay | 8000 ms | 持久 dwelling |
| DA tau_rise | 2000 ms | |
| DA tau_decay | 5000 ms | |
| release_threshold | 0.3 | 防止基线静息释放 |
| MOD-1 strength | -5 pA | AIY 抑制 |
| 5-HT speed effect | -15% | |
| DA speed effect | -25% | |
| NSM gain | 30 | pA per unit concentration |
| CEP gain | 20 | pA per unit concentration |

## 验证结果 (60s 仿真)

```
12. NEUROMODULATION (Layer 6):
   5-HT: conc=0.842  sources=2  targets=3
   DA:   conc=0.512  sources=4  targets=1
   speed_scale=0.762  (effective=1.524, base=2.0)
```

| 指标 | Step 19b | Step 20 | 说明 |
|------|----------|---------|------|
| CI | 0.564 | **0.579** | 保持>0.5 ✅ |
| Reversals | 8/min | 10/min | 正常 ✅ |
| Speed | 0.227 | **0.205** mm/s | 食物附近减速 ✅ |
| 5-HT | 0 | 0.84 | 食物上累积 ✅ |
| DA | 0 | 0.51 | 食物上累积 ✅ |
| AIY L | -31 | **-32** mV | 5-HT 抑制 ✅ |
| 神经元 | 64 | **70** | +6 (NSM×2, CEP×4) |

## 文件变更

- **新增**: `src/neuromodulation/neuromodulation.h` — 框架头文件
- **新增**: `src/neuromodulation/neuromodulation.cpp` — 框架实现
- **修改**: `CMakeLists.txt` — 添加 celegans_neuromodulation 库
- **修改**: `src/simulation/simulation_engine.h` — 添加 NeuromodulationManager 成员
- **修改**: `src/simulation/simulation_engine.cpp` — setup_neuromodulation(), step() 集成
- **修改**: `src/connectome/connectome_loader.cpp` — 添加 NSM/CEP 神经元
- **修改**: `src/environment/sensory_transducer.h` — 添加 TONIC 响应类型
- **修改**: `src/simulation/diag_main.cpp` — 添加调质诊断输出

## Step 20c: OA + 饱食度 — 完整行为循环

### 问题

Step 20 只实现了 roaming→dwelling 单向切换。线虫到达食物后永远 dwelling，不会再探索。
缺失：饱食后 dwelling→roaming 的反向切换。

### 新增组件

**Octopamine (OA)**:
- 源: RIC L/R (新增 2 个中间神经元, 72 总)
- tau_rise=2s, tau_decay=4s
- 靶点:
  - SER-3: SPEED_SCALE +30% (促进快速运动)
  - SER-6 on AIY: +4 pA (促进前进)
- 5-HT→RIC 交叉抑制: SER-4 -8 pA (dwelling 时抑制 OA)

**饱食度 (Satiety)**:
- 内部状态变量 [0, 1]: 0=饥饿, 1=饱食
- on_food 检测: C²/(C²+0.09), 半最大值 C=0.3 (只有真正在食物上才算进食)
- tau_fill = 20s, tau_deplete = 40s
- 效应 1: NSM 抑制 (-15 pA × satiety) → 5-HT 下降
- 效应 2: RIC 激励 (5 pA baseline + 10 pA × satiety) → OA 上升
- 效应 3: ASE/AWC 趋化抑制 (-8 pA × (sat-0.3)/0.7) → 饱食时不再趋化
- REF: You 2008 — insulin/DAF-2, Tomioka 2006 — chemotaxis modulation

### 验证结果 (120s 仿真)

完整行为循环涌现:
```
t(s)  dist   5-HT   OA    sat   spd    行为
 10  12.4   0.15  0.18  0.00  1.01   ROAMING (快速, OA>5-HT)
 40   9.9   0.46  0.10  0.04  0.90   → DWELLING 过渡
 50   8.0   0.64  0.08  0.15  0.83   DWELLING 峰值 (最慢)
 70   5.6   0.59  0.22  0.50  0.84   饱食度>0.5, 5-HT下降
 80   7.1   0.46  0.31  0.57  0.90   ★ 离开食物 ★
110  12.5   0.16  0.25  0.19  1.02   ROAMING (speed>1.0!)
120  13.8   0.07  0.24  0.09  1.05   饥饿, 准备再次觅食
```

### 机制总结

```
饥饿 → 高OA/低5-HT → roaming(快直走) → 发现食物
  → NSM检测食物 → 5-HT↑ → 抑制AIY+RIC → dwelling(慢多转)
  → 进食 → satiety↑ → 抑制NSM → 5-HT↓
  → satiety↑ → 激励RIC → OA↑ → 抑制趋化 → 随机运动
  → 离开食物 → satiety↓ → NSM恢复 → 回到饥饿状态
```

## 未来扩展

- **Tyramine**: RIM 已在模型中，添加 TA 释放和效应即可
- **神经肽**: FLP-1, PDF 等 — 驱动 roaming 状态
- **受体多样性**: SER-1 (兴奋性), DOP-1 (D1-like)
- **空间扩散**: 目前用全局浓度，未来可加空间衰减
- **多食物源**: 测试在多个食物斑块间的最优觅食策略

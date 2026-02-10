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

## 未来扩展

- **Tyramine**: RIM 已在模型中，添加 TA 释放和效应即可
- **Octopamine**: 添加 RIC 神经元
- **神经肽**: FLP-1, PDF 等 — 驱动 roaming 状态
- **受体多样性**: SER-1 (兴奋性), SER-4 (抑制性), DOP-1 (D1-like)
- **空间扩散**: 目前用全局浓度，未来可加空间衰减

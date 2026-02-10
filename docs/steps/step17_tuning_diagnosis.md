# Step 17: 实时调参 + 信号链诊断 + 转弯修复

## 目标

利用 Step 16 的 ImGui 可视化工具，实时暴露增益链参数、观察信号波形、定位趋化转弯瓶颈并修复。

## 关键问题

Step 15 的 CI=+0.312 不够好，heading 变化率太低，线虫转弯方向不对。需要找出增益链上的瓶颈。

## 实现

### 17a: 实时调参系统

#### TuningParams 结构体 (`simulation_engine.h`)

```cpp
struct TuningParams {
    float weathervane_gain = 300.0f;  // pA per (conc/mm) — gradient → SMD bias
    float synapse_scale   = 1.0f;    // global synapse weight multiplier
    float speed_scale     = 2.0f;    // v_max multiplier
    float sensory_gain    = 1.0f;    // chemosensory transducer gain multiplier
    float bias_clamp      = 30.0f;   // max weathervane bias current (pA)
};
```

#### 接线方式

| 参数 | 接入位置 | 机制 |
|------|---------|------|
| `weathervane_gain` | `apply_weathervane()` | bias = gain × grad_normal |
| `synapse_scale` | `Connectome::compute_synaptic_currents()` | I *= synapse_runtime_scale_ |
| `speed_scale` | `BodyModel::update_positions()` | v_max *= speed_scale_ |
| `sensory_gain` | `apply_sensory_input()` | I_sensory *= sensory_gain |
| `bias_clamp` | `apply_weathervane()` | clamp(bias, ±bias_clamp) |

同步时机：`step()` 开头每步同步 params → 子系统。

#### 新增波形

| 索引 | 神经元 | 用途 |
|------|--------|------|
| [0-1] | SMDDL/SMDVL | 半中心振荡器 |
| [2-5] | AVAL/AVBL/AIBL/AIYL | 命令 + 中间神经元 |
| [6-7] | ASEL/ASER | 感觉 L/R 不对称 |
| — | heading_times_/values_ | 头部方向角 (°) |

#### 7 级信号链诊断 (调参面板实时显示)

```
① 梯度幅度 (/mm)
② 垂直梯度 + 偏置电流 (pA)
③ SMD 差异 D-V (mV)
④ 头部曲率 (/mm)
⑤ 速度 (mm/s)
⑥ 转弯率 (°/s)
⑦ CI (趋化指数, 带颜色编码: 红<0, 黄0-0.3, 绿>0.3)
```

#### UI 布局 (3 列)

```
左 30%           中 42%                     右 28%
┌──────────┐ ┌────────────────────┐ ┌───────────────┐
│ 轨迹图    │ │ SMD 半中心振荡      │ │ 调参面板       │
│          │ │ 命令神经元          │ │  5个滑条       │
│          │ │ ASEL/ASER 不对称   │ │  信号链诊断     │
├──────────┤ │ Heading 方向角      │ ├───────────────┤
│ 距离/CI   │ │ (每图带幅度标注)    │ │ 控制面板       │
│ 统计图    │ │                    │ │ 仿真信息       │
└──────────┘ └────────────────────┘ └───────────────┘
```

### 17b: 信号链诊断与瓶颈修复

#### 诊断工具

`src/simulation/diag_main.cpp` → `celegans_diag.exe`

运行 60 秒仿真，采集 9 级信号链数据，自动分析瓶颈：

```bash
.\build\Release\celegans_diag.exe
```

输出示例：
```
SIGNAL CHAIN DIAGNOSTIC (60s run)
1. GRADIENT: mean=0.064 /mm          [OK]
2. GRADIENT NORMAL: mean=-0.001      [OK]
3. BIAS CURRENT: ±3.5 pA             [OK]
4. ASEL-ASER diff: 13.7 mV           [OK]
5. SMD diff amplitude: 107 mV        [OK]
6. HEAD CURVATURE: 0.12 /mm          [OK]
7. SPEED: 0.21 mm/s                  [OK]
8. HEADING rate: 12.9 °/s            [OK]
9. CI: 0.760                         [OK]

BOTTLENECK ANALYSIS: All stages look healthy!
```

#### 瓶颈发现

```
信号链:
梯度(0.012) → 偏置电流(±0.5pA) → SMD振荡(99mV) → 曲率(0.12) → heading
                    ↑                    ↑
                瓶颈在这里！          0.5pA 打不动 99mV 的振荡器
```

**根因**：weathervane 偏置电流 (±0.5 pA) 注入到 SMD 神经元的突触电流中，但 SMD 半中心振荡器幅度 99 mV。假设漏电导 ~1 nS，0.5 pA 只产生 0.5 mV 偏移——在 99 mV 振荡中完全不可见。

#### 修复方案：直接曲率偏置

绕过神经网络动力学瓶颈，将梯度法向分量直接映射为头部曲率偏置：

```cpp
// apply_weathervane() 末尾
double curv_gain = weathervane_gain * 0.15;  // 校准: 梯度0.01 → 0.44/mm → 5°/s
double curv_bias = curv_gain * grad_normal;
body_.set_curvature_bias(curv_bias);

// body_model.cpp update_positions()
double head_curv = segments_[0].curvature + curvature_bias_;
double dtheta = forward_speed * head_curv * dt;
```

校准推导：
- 目标转弯率：5°/s = 0.087 rad/s
- 速度 0.2 mm/s → 需要 κ_bias = 0.087/0.2 = **0.44 /mm**
- 典型梯度 ~0.01 /mm → curv_gain = 0.44/0.01 = **44**
- 使用 weathervane_gain × 0.15 = 300 × 0.15 = **45** ✓

REF: Iino & Yoshida 2009 — curving rate bias = 12.7 °/mm × ∇C_⊥

#### 参数调整总结

| 参数 | 修复前 | 修复后 | 原因 |
|------|--------|--------|------|
| weathervane_gain | 50 | 300 | SMD 偏置电流太弱 |
| bias_clamp | 5 pA | 30 pA | 限幅太严 |
| speed_scale | 1.0 | 2.0 | 速度 0.10→0.21 mm/s |
| curv_gain (新) | — | 45 | 直接曲率偏置 |

## 验证结果

| 指标 | 修复前 | 修复后 | 目标 |
|------|--------|--------|------|
| CI | +0.07 | **+0.760** | >0.5 ✅ |
| 距食物 | 14.1mm | **3.4mm** | 越小越好 ✅ |
| 速度 | 0.10 mm/s | **0.21 mm/s** | ~0.2 ✅ |
| ASEL-ASER 差 | 4.9 mV | **13.7 mV** | 有差即可 ✅ |
| Pirouette 频率 | ? | **0.10 Hz** | ~0.05-0.1 ✅ |
| 转弯率 | 35°/s | **12.9°/s** | — |

## 新增/修改文件

| 文件 | 变更 |
|------|------|
| `src/simulation/simulation_engine.h` | 添加 TuningParams 结构体 + body_mut()/connectome_mut() |
| `src/simulation/simulation_engine.cpp` | apply_weathervane 使用 params + 直接曲率偏置 |
| `src/connectome/connectome.h/.cpp` | 添加 synapse_runtime_scale_ + set/get |
| `src/body/body_model.h/.cpp` | 添加 speed_scale_, curvature_bias_ + setter |
| `src/visualization/vis_app.h/.cpp` | 3列布局, 调参面板, ASEL/ASER, heading曲线, 信号链诊断 |
| `src/simulation/diag_main.cpp` | 新增: 自动信号链诊断工具 |
| `CMakeLists.txt` | 添加 celegans_diag 目标 |

## 遗留问题

1. **神经网络路径仍然断裂**: weathervane 偏置电流无法有效影响 SMD 振荡器。未来需要通过调整 SMD 离子通道参数（如 SLO-1 Ca²⁺敏感性）使振荡器对偏置电流更敏感。
2. **直接曲率偏置是权宜之计**: 目前绕过了神经网络，不够"涌现"。理想方案是让 SMD 偏置通过神经回路自然传递到转弯行为。
3. **pirouette 后快速修正**: 每次 pirouette 随机重置 heading ±180°, 需要强曲率偏置快速修正方向。

## 参考文献

- Iino & Yoshida 2009 — "Parallel use of two behavioral mechanisms for chemotaxis in C. elegans" (J Neurosci 29:5370-5380)
- Pierce-Shimomura et al. 1999 — Pirouette model of chemotaxis
- Fang-Yen et al. 2010 — Speed on agar ~0.15-0.2 mm/s

# Step 116: MuscleSystem 独立计算节点 + 力学速度模型 + SPEED_SCALE 旁路移除

## 动机

身体模型存在三个架构问题：

1. **无肌肉动力学** — 运动神经元释放率直接写入 BodySegment activation（瞬时，无时间常数）
2. **运动学速度捷径** — `speed = v_max × speed_scale × muscle_work`，非力学模型
3. **SPEED_SCALE 旁路** — 4 种神经调质（5-HT, OA, PDF, FLP-11）直接乘速度，绕过肌肉

## 生物学基础

- C. elegans 体壁肌肉为非发放型、分级收缩细胞
- 接收胆碱能兴奋（ACh，A/B 类 MN）+ GABA 能抑制（D 类 MN）
- 肌肉收缩时间常数 ~30ms（Richmond & Jorgensen 1999）
- 低雷诺数运动：F_drag = C × v → v = F_propulsive / C_drag
- 神经调质调节肌肉兴奋性/收缩力（非直接乘速度）

REF: Richmond & Jorgensen 1999, Liu et al. 2006, Fang-Yen 2010, White 1986

## 实现

### MuscleCell（独立计算节点）
```
drive = clamp(Σ excitatory - Σ inhibitory, 0, 1)
da/dt = (drive - a) / τ_muscle     (τ = 30ms)
force = a × neuromod_gain
```

### MuscleSystem（48 dorsal + 48 ventral）
- `add_excitatory(seg, dorsal, input)` — max 语义（避免多神经元饱和）
- `add_inhibitory(seg, dorsal, input)` — 求和语义
- `set_neuromod_gain(gain)` — 替代 SPEED_SCALE

### BodyModel 改造
- 曲率：`target = curvature_gain × force_differential + curvature_drive`
- 速度：`v = mean_force × locomotion_efficiency × speed_tuning / drag`
- `speed_tuning`（从 params.speed_scale）为物理校准旋钮，非生物通路
- 移除：`muscle_gain_`, `speed_scale_`, `set_muscle_activation()`, `reset_activations()`

### MotorController 改造
- 单遍处理：兴奋性 `add_excitatory`，抑制性 `add_inhibitory`
- 替代旧的两遍 max + subtract 模式

### Neuromodulation 改造
- `ModulationEffect::SPEED_SCALE` → `ModulationEffect::MUSCLE_GAIN`
- 5-HT(-0.60), OA(+0.35), PDF(+0.25), FLP-11(-0.95) 通过肌肉力量影响速度
- 速度从肌肉力量涌现，非直接乘法

### as_factor 重校准
- 2.8 → 0.8（param_sweep 验证）
- 原因：Steps 102-105 添加 SAA/SIA/SIB 头部运动神经元抬高了 dorsal_tone
- 旧值导致 `effective_riv = peak - tone × 2.8` 永远 < threshold → 零 omega

## 修改文件

| 文件 | 改动 |
|------|------|
| `muscle_system.h` | MuscleCell 结构 + MuscleSystem 类（替代 placeholder） |
| `muscle_system.cpp` | 完整实现：step/add_excitatory/add_inhibitory/get_force_* |
| `body_model.h` | 集成 MuscleSystem，移除旧接口，新增 speed_tuning |
| `body_model.cpp` | 力学速度模型，肌肉力驱动曲率，muscles_.step() |
| `motor_controller.cpp` | 单遍 add_excitatory/add_inhibitory（替代两遍 max+subtract） |
| `neuromodulation.h` | SPEED_SCALE → MUSCLE_GAIN 枚举 |
| `neuromodulation.cpp` | speed_scale_ → muscle_gain_ |
| `setup_neuromodulation.cpp` | 4 处 SPEED_SCALE → MUSCLE_GAIN |
| `simulation_engine.h` | as_factor 2.8→0.8 |
| `simulation_engine.cpp` | set_speed_tuning 替代 set_speed_scale，muscle_gain 通路 |
| `vis_app.cpp` | get_speed_scale → get_muscle_gain |

## 验证

- **编译**: 零错误
- **health_check**: 275/275 神经元存活且活跃 (100%)
- **behavior_analyzer (60s)**:
  - 速度: 0.184 mm/s
  - Omega: 11 次 (92% omega/reversal)
  - CI: 0.898
  - 曲率频率: 0.81 Hz
  - 路径效率: 0.993
- **param_sweep**: as_factor 0.8 为最优值（CI=0.550, omega=6/30s）

## 旁路状态

| 旁路 | 状态 |
|------|------|
| curvature_bias | ✅ 已移除 (Step 115) |
| omega_mode | ✅ 已移除 (Step 115) |
| SPEED_SCALE | ✅ 已移除 → MUSCLE_GAIN |
| set_muscle_activation (instant) | ✅ 已移除 → MuscleSystem (30ms τ) |
| speed = v_max × scale × work | ✅ 已移除 → force-based speed |
| set_locomotion_state | 保留 (AVA/AVB → direction) |
| curvature_drive_[] | 保留 (RIV omega / SMB klinotaxis) |

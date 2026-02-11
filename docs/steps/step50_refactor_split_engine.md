# Step 50: 重构 — 拆分 simulation_engine.cpp

## 动机

`simulation_engine.cpp` 已膨胀至 ~2880 行，典型 God Class 问题。
每新增行为/受体需同时修改初始化、主循环、头文件等多处，不利于管理和维护。

## 方法: 同类拆源文件 (Split Source, Same Class)

将独立的功能方法从 `simulation_engine.cpp` 移到单独的 `.cpp` 文件，
但仍作为 `SimulationEngine` 类的成员方法实现。**零接口变化，不修改 .h 文件。**

## 实现

### 新建文件

| 文件 | 包含方法 | 行数 |
|------|---------|------|
| `setup_neuromodulation.cpp` | `setup_neuromodulation()` — 6种神经调质配置 | 504 |
| `update_internal_states.cpp` | `update_satiety()`, `update_food_memory()`, `apply_gradient_klinokinesis()`, `update_fatigue()`, `apply_sleep_effects()` | 181 |
| `update_learning.cpp` | `update_salt_learning()`, `update_sickness()`, `update_pathogen_learning()` | 96 |
| `update_pharynx_system.cpp` | `apply_pharyngeal_modulation()`, `update_pharynx()` | 99 |

### 修改文件
- `simulation_engine.cpp` — 删除已移动方法，原位留注释标记
- `CMakeLists.txt` — `celegans_simulation` 库添加 4 个新源文件

### 效果
- `simulation_engine.cpp`: **2880 → 1835 行** (-36%, -1045 行)
- 保留: `initialize_default()`, `step()`, `cache_*()`, `setup_stp_params()`, `setup_gpu_backend()`, omega/RIV 控制, 传感/运动更新
- 后续可继续拆分 sensory/motor 逻辑（目前不急）

## 验证

- **编译**: 零错误
- **regtest**: 17 pass, 0 FAIL
- **行为**: 纯代码移动，零逻辑修改，完全不变

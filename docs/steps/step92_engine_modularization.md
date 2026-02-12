# Step 92: SimulationEngine 模块化拆分

**日期**: 2025-02-12

## 动机

`simulation_engine.cpp` 已膨胀至 2012 行，包含感觉转导、运动控制、STP 参数设置、GPU 后端等多个功能域的实现。
文件过大导致导航困难、编译时间长、职责不清晰。需要按功能域拆分为独立编译单元。

## 生物学基础

纯工程重构，无生物学变更。所有函数签名、逻辑、参数完全保持不变。

## 实现细节

### 拆分方案

从 `simulation_engine.cpp` 提取 3 个新文件：

| 新文件 | 函数 | 行数 | 功能域 |
|--------|------|------|--------|
| `apply_sensory_systems.cpp` | `apply_sensory_input`, `apply_thermo_input`, `apply_tail_chemosensation`, `apply_touch_stimulus`, `apply_sensitization` | ~822 | 感觉转导（化学/温度/触觉/O₂/CO₂/光/信息素/本体感觉/产卵/食物边缘） |
| `apply_motor_control.cpp` | `apply_head_tonic`, `apply_weathervane`, `apply_smb_neck_bias`, `apply_ria_smd_modulation`, `apply_proprioceptive_stretch`, `apply_riv_omega` | ~290 | 转向与运动控制（weathervane/RIA-SMD/SMB/本体感觉/RIV omega） |
| `setup_gpu_stp.cpp` | `setup_stp_params`, `setup_gpu_backend`, `sync_synapses_to_gpu` | ~160 | STP 参数初始化 + GPU 后端设置 |

### 拆分后文件结构

`simulation_engine.cpp` 保留：
- `SimulationEngine()` 构造函数
- `initialize_default()` / `initialize()` 初始化
- `cache_neuron_ids_and_synapses()` / `update_awc_pref_cache()` 缓存
- `step()` 主循环
- `run()` 批量运行

已有拆分文件（Step 50/50a）：
- `setup_neuromodulation.cpp` — 神经调质配置
- `update_internal_states.cpp` — satiety/food_memory/fatigue/sleep
- `update_learning.cpp` — 盐学习/病原体学习/突触遗忘
- `update_pharynx_system.cpp` — 咽部泵食

### 修改文件列表

| 文件 | 变更 |
|------|------|
| `src/simulation/simulation_engine.cpp` | 删除已移出的函数体（2012→693 行） |
| `src/simulation/apply_sensory_systems.cpp` | **新建** — 感觉转导函数 |
| `src/simulation/apply_motor_control.cpp` | **新建** — 运动控制函数 |
| `src/simulation/setup_gpu_stp.cpp` | **新建** — STP/GPU 初始化 |
| `CMakeLists.txt` | 添加 3 个新源文件到 `celegans_simulation` 库 |

## 验证

- 编译零错误（MSVC Release）
- 所有 4 个可执行文件成功构建：`celegans_sim`, `celegans_diag`, `celegans_regtest`, `celegans_vis`
- 纯重构，无逻辑变更，无回归风险

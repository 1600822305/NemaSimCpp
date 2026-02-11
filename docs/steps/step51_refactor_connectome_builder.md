# Step 51: 重构 — 拆分 generate_default_connectome()

## 动机

`connectome_loader.cpp` 中的 `generate_default_connectome()` 是一个 ~800 行的巨函数，
包含所有 132 个神经元定义和 ~200 条突触/间隙连接。
新增神经元或回路时需在这个巨函数中搜索正确位置，容易漏连接或重复。

## 方法

### 1. ConnectomeBuilder (CB) 辅助结构
在匿名命名空间中定义 `CB` 结构，封装短名方法：
- `b.syn(pre, post, sections)` — 兴奋性化学突触
- `b.inh(pre, post, sections)` — 抑制性突触 (GABA 反转)
- `b.comp(pre, post, sections, compartment)` — 分隔靶向突触
- `b.gj(a, b, sections)` — 间隙连接
- `b.neuron(name, type, nt)` — 注册神经元

### 2. 12 个 build_xxx() 函数（按回路）

| 函数 | 回路 | 内容 |
|------|------|------|
| `build_neurons` | 全部 | 132 个神经元定义 |
| `build_chemotaxis` | 化学趋向 | ASE/AWC/AWA/AFD → AIA/AIB/AIY/AIZ |
| `build_touch_nociception` | 触觉/伤害 | ALM/PLM/ASH/AWB 触觉回路 |
| `build_interneuron` | 中间层 | AIA⊣AIB, AIB→AVA, AIY→RIA/AVB, RIB→AVB |
| `build_head_motor` | 头部振荡器 | SMD/RMD/RME/SMB + OLQ |
| `build_command_ventral` | 指令+腹索 | AVA/AVB/AVE → 运动神经元, DD↔VD, AS |
| `build_omega` | Omega 转弯 | RIA↔RIV 反馈环 |
| `build_gas_sensing` | 气体感知 | O₂ (URX/AUA/AQR/PQR) + CO₂ (BAG) |
| `build_proprioception` | 本体感觉 | DVA + PVD |
| `build_pharynx` | 咽部 CPG | MC/M3/M4/I1/RIP |
| `build_egg_laying` | 产卵 | HSN/VC |
| `build_sleep_and_gaps` | 睡眠 + 核心间隙 | RIS + 指令 L-R 耦合 |

### 3. 文件变化

| 文件 | 变化 |
|------|------|
| `connectome_builder.h` (新) | `build_default_connectome()` 声明 |
| `connectome_builder.cpp` (新) | CB 结构 + 12 个 build 函数 (~620 行) |
| `connectome_loader.cpp` | 942 → **148 行** (-84%), 函数体改为一行委托调用 |
| `CMakeLists.txt` | 添加 `connectome_builder.cpp` |

## 验证

- **编译**: 零错误
- **regtest**: 17 pass, 0 FAIL
- **发现并保留**: 原始代码中 AIY→AVB 连接出现两次（line 484 + 611），
  builder 中如实保留此重复以确保行为一致

## 后续 (Step 52)

- ids_ map 自动注册，消除 simulation_engine.h 中 30+ 个手动缓存字段

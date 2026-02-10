# 回归测试与电流溯源工具

## 概述

`celegans_regtest.exe` 是一个自动化的回归检测和 bug 定位工具。每次代码变更后运行，**30 秒内**完成：

1. **基线对比** — 12 个关键指标与已知良好状态对比
2. **电流溯源** — 自动分解异常神经元的 I_syn 到每个突触来源
3. **注入检测** — 区分连接组突触电流 vs 代码注入（weathervane/omega/neuromod）

## 设计动机

| 过去的调试流程 | 现在的流程 |
|---------------|-----------|
| 发现异常 → 猜测原因 → 加 fprintf → 编译 → 运行 → 分析 → 再猜 → 重复 5 次 | 发现异常 → 运行 regtest → 直接看到 "SMDDL I_syn=227pA, 来源: omega 注入 200pA" |

### 实际案例：SMD 振荡器回归

omega 转弯代码向 SMD 注入 ±200pA，破坏了半中心振荡器（222mV vs 正常 110mV）。
手动调试花了很长时间。如果当时有 regtest，**2 秒定位**：

```
[!!] SMDDL |I_syn| max  227.0 pA (baseline=40, dev=+468%)

AUTO-TRACE: SMDDL
  RIAL [chem_exc]    10.47 pA
  SMDVL [chem_inh]    -0.38 pA
  (inject) [    code]  200.0 pA  <-- ANOMALY    ← 立刻发现！
```

## 用法

### 基本回归检查

```bash
.\build\Release\celegans_regtest.exe
```

输出示例（全部通过）：

```
========================================
  REGRESSION TEST (30s simulation)
========================================

  [OK] SMDDL V swing             58.8 mV
  [OK] SMDVL V swing             58.1 mV
  [OK] SMD diff amplitude        99.2 mV
  [OK] SMDDL |I_syn| max         36.2 pA
  [OK] SMDDL I_ext                3.0 pA
  [OK] Curvature amplitude        0.2 /mm
  [OK] Speed mean                 0.3 mm/s
  [OK] Heading rate              18.7 deg/s
  [OK] ASEL mean V              -36.9 mV
  [OK] ASER mean V              -41.9 mV
  [OK] Reversal count             3.0
  [OK] Omega count                2.0

  Result: 12 pass, 0 FAIL
  All metrics within expected range. No regression detected.
```

输出示例（检测到回归）：

```
  [!!] SMDDL |I_syn| max  227.0 pA  (baseline=40, dev=+468%)
  [!!] SMD diff amplitude  222.0 mV  (baseline=115, dev=+93%)

  Result: 10 pass, 2 FAIL

========================================
  AUTO-TRACING ANOMALOUS NEURONS       ← 自动触发！
========================================
  CURRENT BUDGET: SMDDL
    RIAL [chem_exc]    10.47 pA
    SMDVL [chem_inh]   -0.38 pA
    (inject) [    code] 200.0 pA  <-- ANOMALY
  NOTE: Non-connectome injection detected!
  Sources: weathervane bias, omega turn, neuromodulation tonic
```

### 手动电流溯源

对任意神经元查看完整电流预算：

```bash
.\build\Release\celegans_regtest.exe --trace SMDDL
.\build\Release\celegans_regtest.exe --trace ASEL
.\build\Release\celegans_regtest.exe --trace MCL
```

输出：

```
========================================
  CURRENT BUDGET: SMDDL
========================================

  V = -60.0 mV
  I_ext = 3.00 pA (mean)          ← set_external_current() 总和
  I_syn = 8.37 pA (mean)          ← add_synaptic_current() 总和
    range=[-1.80, 16.61]

  Connectome synaptic breakdown (mean pA):
      RIAL [chem_exc]    10.47 pA  ← RIA→SMD 兴奋性突触
     SMDVL [chem_inh]    -0.38 pA  ← 交叉抑制
  (inject) [    code]    -1.71 pA  ← 代码注入（weathervane偏置）
```

### 保存基线值

当确认系统状态良好时，打印当前值用于更新代码中的基线：

```bash
.\build\Release\celegans_regtest.exe --save
```

## 监控的 12 个指标

### SMD 振荡器健康（最常见的回归目标）

| 指标 | 基线 | 容差 | 说明 |
|------|------|------|------|
| SMDDL V swing | 65 mV | ±30% | 单神经元电压摆幅 |
| SMDVL V swing | 65 mV | ±30% | 反相神经元电压摆幅 |
| SMD diff amplitude | 115 mV | ±30% | 差分振幅（D-V） |
| SMDDL \|I_syn\| max | 40 pA | ±50% | **>60pA 几乎必然是异常注入** |
| SMDDL I_ext | 3.0 pA | ±10% | head_tonic，应恒定 |

### 躯体力学

| 指标 | 基线 | 容差 | 说明 |
|------|------|------|------|
| Curvature amplitude | 0.19 /mm | ±40% | 头部曲率范围 |
| Speed mean | 0.26 mm/s | ±30% | 运动速度 |
| Heading rate | 15 deg/s | ±50% | 航向变化率 |

### 感觉与行为

| 指标 | 基线 | 容差 | 说明 |
|------|------|------|------|
| ASEL mean V | -40 mV | ±20% | 化学感觉 ON-type |
| ASER mean V | -42 mV | ±20% | 化学感觉 OFF-type |
| Reversal count | 5 | ±150% | 30s内反转次数（随机性大） |
| Omega count | 3 | ±150% | 30s内omega转弯次数 |

## 电流溯源原理

### 三层分解

```
总 I_syn = 连接组突触 + 间隙连接 + 代码注入
           ↑               ↑           ↑
      trace_inputs()    trace_inputs()  差值计算
```

1. **连接组突触**（`Connectome::trace_inputs()`）  
   遍历所有 `ChemicalSynapse`，对 `post_id == target` 的计算瞬时电流

2. **间隙连接**（`Connectome::trace_inputs()`）  
   遍历所有 `GapJunction`，对包含 target 的计算双向欧姆电流

3. **代码注入**（差值检测）  
   `total_I_syn - sum(connectome)` = 非连接组注入  
   来源：`apply_weathervane()`, `apply_omega_turn()`, `apply_thermo_input()` 等

### ANOMALY 标记规则

- 单个来源 `|I| > 50 pA` → `<-- ANOMALY`
- SMD 的 C_m = 1.8 pF，50pA → dV/dt = 28 mV/ms，已经很显著
- 200pA → dV/dt = 111 mV/ms，单步 55mV 变化，**必然破坏振荡器**

## 何时运行

- **每次代码修改后** — 编译完就跑，30 秒出结果
- **添加新神经元/突触后** — 检查是否意外影响 SMD
- **修改电流注入后** — 检查是否超出安全范围
- **修改离子通道参数后** — 检查振荡器是否稳定

## 源码位置

- `src/simulation/regression_test.cpp` — 主工具
- `src/connectome/connectome.h` — `trace_inputs()` 接口
- `src/connectome/connectome.cpp` — `trace_inputs()` 实现
- `src/neuron/single_compartment.h` — `get_I_syn()` / `get_I_ext()` getter

## 扩展方向

- [ ] 从文件加载/保存基线（替代硬编码）
- [ ] 添加更多神经元的监控（AIY, RIA, AVA 等）
- [ ] 子系统隔离模式：自动禁用各子系统定位根因
- [ ] CI/CD 集成：每次 push 自动运行

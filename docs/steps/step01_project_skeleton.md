# Step 1: C++ 工程骨架 + CMake 构建系统

> 日期: 2026-02-10
> 状态: ✅ 完成

---

## 目标

搭建 C. elegans 仿真项目的 C++ 工程骨架，建立模块化的 CMake 构建系统。

## 决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 语言 | C++20 | 高性能计算，模板/概念支持 |
| 编译器 | MSVC 19.44 | 用户环境 Visual Studio 2022 |
| 构建 | CMake 4.2 | 跨平台，IDE 集成好 |
| 生成器 | Visual Studio 17 2022 | 默认可用 |

## 实现

### CMakeLists.txt 结构

7 个静态库 + 1 个可执行文件：

```
celegans_core          ← 基础类型、配置、日志
celegans_neuron        ← 离子通道、神经元模型
celegans_connectome    ← 突触、连接组
celegans_body          ← 身体物理
celegans_motor         ← 运动控制
celegans_environment   ← 环境仿真
celegans_simulation    ← 仿真引擎
celegans_sim           ← 可执行文件 (main.cpp)
```

### 依赖关系

```
celegans_sim → celegans_simulation → celegans_connectome → celegans_neuron → celegans_core
                                   → celegans_body → celegans_core
                                   → celegans_motor → celegans_neuron + celegans_body
                                   → celegans_environment → celegans_core
```

### 可选依赖

- OpenMP: `CELEGANS_USE_OPENMP` 选项，默认关闭
- CUDA: 预留，Phase 4 扩展

## 文件清单

```
CMakeLists.txt                    — 顶层构建配置
src/core/types.h/.cpp             — 基础类型
src/core/config.h/.cpp            — 配置解析
src/core/logger.h/.cpp            — 日志系统
src/neuron/                       — 神经元模块 (8 文件)
src/connectome/                   — 连接组模块 (8 文件)
src/body/                         — 身体模块 (4 文件)
src/motor/                        — 运动模块 (2 文件)
src/environment/                  — 环境模块 (4 文件)
src/simulation/                   — 仿真模块 (3 文件)
```

## 验证

- `cmake -S . -B build -G "Visual Studio 17 2022"` → 成功
- `cmake --build build --config Release` → 零错误 (2 个 Unicode 警告，无关紧要)
- 生成 `build/Release/celegans_sim.exe`

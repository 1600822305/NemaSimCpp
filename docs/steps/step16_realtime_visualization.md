# Step 16: 实时可视化 (Dear ImGui + ImPlot)

> 日期: 2026-02-10
> 状态: ✅ 完成
> 前置: Step 15 (速度调优 + Weathervane)

---

## 目标

为 C. elegans 仿真添加实时可视化仪表盘，支持：
1. 线虫轨迹实时绘制 + 化学场等浓度线
2. 关键神经元膜电位实时曲线
3. 趋化性指标（距离、CI）时间序列
4. 仿真控制（暂停/继续、速度调节、重置）

---

## 技术选型

| 方案 | 优势 | 劣势 | 选择 |
|------|------|------|------|
| Dear ImGui + ImPlot | GPU加速, 科学绘图原生支持, 60fps+ | 需要窗口后端 | ✅ |
| Raylib | 极简API | 无科学图表 | ❌ |
| SFML | 成熟2D库 | 无科学图表 | ❌ |
| Web (D3/Canvas) | 漂亮UI | 架构过重 | ❌ |

**窗口后端**: GLFW (vcpkg 原生支持, 比 SDL2 更轻量)

---

## 依赖安装

```powershell
# 安装 vcpkg (一次性)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg integrate install

# 安装依赖
vcpkg install imgui[glfw-binding,opengl3-binding,docking-experimental]:x64-windows
vcpkg install implot:x64-windows glfw3:x64-windows

# CMake 配置
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

---

## 架构

```
celegans_vis.exe (新目标)
├── vis_main.cpp          — 入口, 创建 VisApp
├── vis_app.h             — VisApp 类声明
└── vis_app.cpp           — 渲染逻辑 + 仿真驱动
    ├── initialize()      — GLFW+OpenGL+ImGui 初始化
    ├── run()             — 主循环: poll events → sim steps → render
    ├── sim_step_batch()  — 每帧执行 N 步仿真
    ├── render_frame()    — 4 面板布局
    │   ├── render_trajectory_panel()  — 线虫轨迹 + 等浓度线
    │   ├── render_neuron_panel()      — SMD/AVA/AVB 膜电位曲线
    │   ├── render_chemical_field()    — 距离 + CI 时间序列
    │   └── render_control_panel()     — 暂停/速度/重置
    └── shutdown()        — 清理资源
```

**与 headless 版本共存**: `celegans_sim.exe` (无GUI) 和 `celegans_vis.exe` (带GUI) 并行构建。

---

## 面板布局

```
┌──────────────────────────────┬──────────────────────────┐
│  Trajectory & Arena          │  Neuron Activity         │
│  · 50×50mm 竞技场            │  · SMD半中心振荡器        │
│  · 绿色轨迹线                │  · AVA/AVB/AIB/AIY 曲线  │
│  · 食物源标记 + 等浓度环     │  · 5秒滑动窗口           │
├──────────────────────────────┼──────────────────────────┤
│  Stats & Metrics             │  Control Panel           │
│  · 距食物距离曲线            │  · 暂停/继续 [Space]     │
│  · 趋化指数 CI 曲线          │  · 速度滑块 (1-200步/帧) │
│                              │  · 重置按钮              │
└──────────────────────────────┴──────────────────────────┘
```

---

## 关键实现细节

- **轨迹记录**: 每 10 步 (5ms) 采样, 环形缓冲 100k 点
- **神经元追踪**: 每步记录 6 个关键神经元 (SMDDL/SMDVL/AVAL/AVBL/AIBL/AIYL), 滑动窗口 20k 点 (~10s)
- **CI 计算**: 每 100ms 计算运动方向与食物方向的 cos 夹角, 累积平均
- **布局**: 4 面板自适应窗口大小 (55%/45% 左右, 60%/40% 上下)

---

## 新增/修改文件

| 文件 | 说明 |
|------|------|
| `CMakeLists.txt` | 添加 `CELEGANS_BUILD_VIS` 选项, `celegans_vis` 目标 |
| `src/visualization/vis_main.cpp` | 可视化入口 |
| `src/visualization/vis_app.h` | VisApp 类声明 |
| `src/visualization/vis_app.cpp` | 完整渲染逻辑 (~430 行) |
| `src/simulation/simulation_engine.h` | 添加 `get_step_count()` |
| `src/environment/environment.h` | 添加 `const chemical_field()` |

---

## 键盘快捷键

| 按键 | 功能 |
|------|------|
| Space | 暂停/继续仿真 |
| Esc | 退出 |
| 鼠标拖拽 | ImPlot 图表缩放/平移 |

# Step 137: 清理 Three.js 查看器残留

> 日期: 2026-02-13

## 动机

项目中存在一个独立的 Three.js 3D 查看器 (`vis/worm3d.html`)，它读取预录的
`worm3d_anim.json` 动画数据进行离线回放。该查看器：

1. **与主仿真无关** — 不是实时驱动，只是 `diag_main.cpp` 导出的预录正弦波动画
2. **与可视化系统重复** — `celegans_vis.exe` (visualization_v2) 已提供实时 302 神经元驱动的虫体渲染
3. **产生混淆** — 文件名 `worm3d` 暗示 3D 物理，但主仿真实际使用 2D `BodyModel`
4. **残留日志文件** — `vis_crash.log`、`vis_err.log` 散落在根目录

## 清理内容

### 删除的文件
- `vis/worm3d.html` — Three.js 离线查看器 (359 行)
- `vis/worm3d_anim.json` — 预录动画数据
- `worm3d_anim.json` — 根目录残留副本
- `vis_crash.log` — 可视化崩溃日志
- `vis_err.log` — 可视化错误日志

### 修改的文件
- `src/simulation/diag_main.cpp` — 删除 "Export animation JSON for Three.js viewer"
  代码块 (~60 行)。保留 BodyModel3D 诊断输出（节点位置、半径、肌肉象限信息）。

## 未删除的内容

- `src/body/body_model_3d.h/.cpp` — 3D 身体模型实现保留，供未来接入主仿真
- `src/visualization/worm_renderer_3d.h/.cpp` — ImGui 伪 3D 渲染器保留（当前 vis 使用）
- `diag_main.cpp` 中 BodyModel3D 诊断模式 — 保留（用于验证 3D 模型参数）

## 验证

- 编译: Release 零错误
- 三个 exe 均成功生成: celegans_sim, celegans_diag, celegans_vis

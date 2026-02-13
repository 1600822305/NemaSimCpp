# Step 130: 实时虫体可视化

## 动机

Step 129 实现了 3D 身体模型，但缺乏可视化。初始尝试用 Three.js 预录回放被否决（不是真仿真）。
需要直接集成到现有 VisApp，用**真实 302 神经元仿真**驱动虫体渲染。

## 实现

### 方案选择
- ❌ Three.js 预录回放 → 不是真仿真
- ❌ OpenGL 3.3 FBO + shader → 需要 GL loader (glad/gl3w)，项目未安装
- ✅ **ImGui DrawList** → 直接在现有 UI 框架内绘制，零额外依赖

### WormRenderer3D (DrawList 版)

**上半部: 俯视图 (TOP XY)**
- 48 段身体轮廓，椭球渐缩 (prolate ellipsoid tapering)
- 每段分背腹两半，独立着色:
  - 背侧肌肉激活 → 橙/红色
  - 腹侧肌肉激活 → 蓝/青色
  - 无激活 → 暗灰绿
- 自动缩放跟随虫体
- 绿色圆 = 头 (H)，蓝色圆 = 尾

**下半部: 曲率图 (CURVATURE)**
- 48 段曲率柱状图
- 背弯 (正) = 暖色，腹弯 (负) = 冷色
- 零线标注 D/V 方向
- 头(H)→尾(T) 从左到右

### 集成
- 替换左下角化学场面板位置
- 由 `engine_.body().segments()` 驱动 — **真实 302 神经元仿真**
- 每帧实时更新

## 验证
- 编译零错误
- regtest 20/20 通过
- 可视化窗口正常启动，虫体轮廓随仿真实时变化

## 修改文件
- `src/visualization/worm_renderer_3d.h`: WormRenderer3D 类 (ImGui DrawList)
- `src/visualization/worm_renderer_3d.cpp`: 俯视图 + 曲率图渲染
- `src/visualization/vis_app.h`: 添加 3D 面板成员
- `src/visualization/vis_app.cpp`: render_3d_body_panel + 布局调整
- `CMakeLists.txt`: 添加 worm_renderer_3d.cpp 到构建
- `src/simulation/diag_main.cpp`: --body-3d JSON 导出 (保留)
- `vis/worm3d.html`: Three.js 查看器 (保留作辅助工具)

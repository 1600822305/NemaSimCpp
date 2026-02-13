# Step 128: 多虫群体仿真 (Multi-Worm Simulation)

> 日期: 2026-02-13

---

## 动机

此前仿真仅模拟单虫行为。社交行为（聚集/bordering）虽通过 NPR-1/RMG 回路实现（Step 96），但缺少虫间信息素交互和群体涌现。本步添加 MultiWormSimulation 框架，支持 N 虫并行仿真。

## 生物学基础

### 群体聚集三规则 (Ding & Schumacher 2019 eLife)
```
规则 1: 群边缘反转
  虫离开群落 → 密度下降 → ADL 信号减弱 → AVA 激活 → 反转
  ← 已有: ADL→AVA 突触 (Step 64 ascaroside sensing)

规则 2: 密度依赖速度切换
  高密度 → 更多邻居 → RMG 激活 → 减速
  ← 已有: NPR-1/RMG hub-and-spoke (Step 96)

规则 3: 邻虫趋化
  信息素梯度 → ASK/ADL → weathervane 偏转
  ← 已有: pheromone field + ADL/ASK sensing (Step 64)
```

### NPR-1 表型
- **N2 (npr-1 gof)**: 独居觅食，NPR-1 抑制 RMG
- **Hawaiian/npr-1(lf)**: 社交聚集，RMG 放大信息素信号

### 参考文献
- Ding & Schumacher 2019 eLife — 聚集三规则 agent-based model
- de Bono & Bargmann 1998 — NPR-1 社交/独居
- Macosko 2009 Nature — hub-and-spoke 信息素回路

## 实现细节

### 架构
```
MultiWormSimulation
├── worms_[N]: 独立 SimulationEngine (各 302 神经元)
├── 共享信息素场: 每虫沉积 ascaroside → 他虫 ADL 感知
├── 物理碰撞: 软排斥 (0.8mm 阈值)
└── 统计: 聚类分析 (union-find, 2mm 阈值)
```

### 信息素交互 (每 1s 更新)
- 每虫在头部位置沉积 ascaroside (强度 0.5, σ²=4mm²)
- 不感知自身信息素 (仅感知他虫)
- 通过已有 ADL 感觉系统处理

### 物理碰撞
- 头部距离 < 0.8mm → 软排斥 (0.02mm/step)

### CLI
```
--multi-worm <N>   启动 N 虫并行仿真
--npr1 0           社交表型 (Hawaiian)
--npr1 -15         独居表型 (N2, 默认)
```

## 关键修复

### Bug 1: speed_scale 被覆写
运动控制段的 `body_.set_speed_scale(effective_speed)` 覆写了 NPR-1 段设置的密度减速。
修复: 将密度减速整合到 effective_speed 计算中。

### Bug 2: 缺少群边缘反转
密度下降时未触发 AVA 反转，导致虫子自由离群。
修复: 添加 prev_neighbor_density_ 跟踪，密度下降→AVA 脉冲，NPR-1 门控。

### 并行化
每步中的 worm->step() 使用 std::thread 并行执行，自动检测 CPU 核数。

## Diag 验证

### N2 vs Hawaiian 表型分离 (10 虫, 60s)

```
               N2 (npr1=-20)    Hawaiian (npr1=0)
Speed:         0.230 mm/s       0.187 mm/s       ← -19% ✅
Clusters@40s:  5                3                ← Hawaiian 更聚
Frac@40s:      0.700            0.900            ← +29% ✅
NN@40s:        1.45mm           1.20mm           ← -17% ✅
Final Frac:    0.400            0.500            ← +25% ✅
Final NN:      2.85mm           2.51mm           ← -12% ✅
```

Hawaiian 显著更慢、更聚集、更紧密，三条聚集规则均已涌现。

## 修改文件
- `src/simulation/multi_worm_simulation.h`: MultiWormSimulation 类声明 + 线程支持
- `src/simulation/multi_worm_simulation.cpp`: 多虫仿真实现 + 并行化
- `src/simulation/simulation_engine.h`: neighbor_density/prev 成员 + public setter
- `src/simulation/simulation_engine.cpp`: 密度减速 + 群边缘反转 + RMG 驱动
- `src/body/body_model.h`: set_position/set_heading/nudge_position
- `src/simulation/diag_main.cpp`: --multi-worm CLI + 输出
- `CMakeLists.txt`: 添加 multi_worm_simulation.cpp

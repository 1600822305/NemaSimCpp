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

## Diag 验证

### 5 虫 30s (--multi-worm 5 --duration 30 --no-toxin)
```
  Time(s)  Clusters  ClusterFrac  MeanNN(mm)  MeanSpeed(mm/s)
     10.0         4        0.400        2.77            0.221
     20.0         3        0.800        1.29            0.202
     30.0         3        0.600        1.54            0.207

  FINAL STATS:
   Clusters: 3
   Cluster fraction: 0.600
   Mean nearest neighbor: 1.54 mm
```

虫子向食物源趋化并开始聚集，cluster fraction 从 0.4→0.6。

## 修改文件
- `src/simulation/multi_worm_simulation.h`: MultiWormSimulation 类声明
- `src/simulation/multi_worm_simulation.cpp`: 多虫仿真实现
- `src/body/body_model.h`: set_position/set_heading/nudge_position
- `src/simulation/diag_main.cpp`: --multi-worm CLI + 输出
- `CMakeLists.txt`: 添加 multi_worm_simulation.cpp

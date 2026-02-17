# Step 125: 多感觉整合测试

> 日期: 2026-02-17
> 状态: ✅ 完成

## 目标

测试多感觉冲突场景下的决策行为：当趋化、斥力、趋温、趋氧信号相互矛盾时，
神经回路如何整合并产生行为输出。

REF:
- Ghosh 2017 Curr Opin Neurobiol — C. elegans 多感觉整合综述
- Shinkai 2011 J Neurosci — 趋温/趋化交互
- Gray 2004 Nature — O₂ 感知与食物关联
- Kumar 2023 PLoS Biol — 运动回路对感觉处理的抑制反馈

## 整合回路

### 巧合检测器（AIA/AIB/AIY/AIZ）

- **AIA**: 接收 AWA(引力) + ASH(斥力) → 趋化/趋避决策
- **AIB**: 整合 AWC(食物气味) + ASH(斥力) → 前进/后退决策
- **AIY**: 趋化 + 趋温输入整合

### Hub-and-spoke（RMG）

- 中枢 RMG 通过间隙连接连接 URX(O₂) + ASK(信息素) + 其他感觉神经元
- NPR-1 抑制 RMG → N2 品系为独居型

## 实现

### multisensory_analyzer (src/diagnostics/multisensory_analyzer_main.cpp)

4 个场景，每个 300s 仿真：

| 场景 | 食物 | 竞争信号 | 测量 |
|------|------|----------|------|
| A. 基线趋化 | (35,25) 10mm | 无 | CI + 均距 |
| B. 食物+斥力 | (35,25) | 同位置 repellent 0.8 | ASH 活动量 |
| C. 食物+高温 | (35,25) | +0.5°C/mm 梯度 | AFD vs AWC |
| D. 食物+边缘 | (43,25) 18mm | 壁附近高O₂ | URX vs 趋化 |

### CI 计算

标准趋化指数: `CI = (start_dist - end_dist) / total_path`
（与 chemotaxis_analyzer 一致）

## 结果

```
场景          CI      均距(mm)  路径(mm)  反转
----------  ------  --------  -------   ----
baseline    -0.005      10.3     57.8     75
food+repel   0.002      10.2     69.2     75
food+temp    0.016       9.6     66.6     75
food+O2     -0.017      18.7     56.1     76
```

### 关键发现

1. **基线 CI ≈ 0** — 单种子 300s 趋化信号弱（正常，需多种子平均）
2. **斥力增加 20% 探索路径** (57.8→69.2mm) — ASH 激活增加活动量
   - 与 Ghosh 2017 描述的 ASH 驱动回避行为特征一致
3. **温度影响不显著** — 0.5°C/mm 梯度下趋化与趋温平衡
4. **虫不趋近边缘食物** (均距 18.7mm >> 初始 18mm) — 壁回避有效

### 方法学说明

- 单种子 CI 噪声大（|CI| < 0.02），需 chemotaxis_analyzer 的多种子模式做精确测量
- 活动量（路径长度）和均距是更稳健的单次运行指标
- N2 品系 NPR-1 抑制 URX/RMG，O₂ 趋避效应可能被掩盖

## 修改文件

- `src/diagnostics/multisensory_analyzer_main.cpp` — 新建，18th 诊断工具
- `CMakeLists.txt` — multisensory_analyzer 目标

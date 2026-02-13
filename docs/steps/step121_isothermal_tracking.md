# Step 121: 等温线追踪 (Isothermal Tracking)

> 日期: 2026-02-13

---

## 动机

C. elegans 在温度梯度中有两种不同的热行为机制（Ryu & Samuel 2002）：
1. **向 Tc 迁移**（T > Tc）：调控 run duration（已由 Step 23 温度 weathervane 实现）
2. **等温线追踪**（T ≈ Tc）：调控 run **方向**，沿等温线精确移动

等温线追踪是完全不同的机制 — 近 Tc 时，虫子调整运动方向以抵消温度变化，沿等温线移动，偏差仅 ±0.05°C。本步实现这一缺失的行为。

## 生物学基础

### 两种机制对比
| 特征 | 向 Tc 迁移 | 等温线追踪 |
|------|-----------|-----------|
| 活跃区 | T > Tc | |T - Tc| < 2°C |
| 控制方式 | run duration | run **方向** |
| 精度 | ±2°C | ±0.05°C |
| 神经元 | AFD, AIZ | AFD, AIY |
| 效果 | 缩短/延长前进 | 转向以抵消温度变化 |

### 等温线追踪的特点
- 虫子在等温线上持续前进运动
- 运动方向垂直于温度梯度（沿等温线）
- 当偏离等温线时，高增益校正将虫子拉回
- 需要 AFD 和 AIY（Mori & Ohshima 1995, Gomez 2001）
- NCS-1 钙结合蛋白是等温线追踪所必需的

### 参考文献
- Hedgecock & Russell 1975 — 发现等温线追踪 (±0.05°C 精度)
- Ryu & Samuel 2002 — 区分两种机制：run duration vs run orientation
- Mori & Ohshima 1995 — AFD/AIY 对追踪必需
- Gomez 2001 — NCS-1/Ca²⁺ signaling for isothermal tracking
- Luo 2014 — AFD→AIY→RIA→SMD 回路

## 实现细节

### 激活条件
```
|T - Tc| < 2.0°C  AND  thermo_wv_gain > 0.1 (需要饱食状态)
```

### 等温线权重
```cpp
iso_weight = (1 - |T-Tc| / threshold)²  // 二次: Tc 处最强
```

### 两个分量

1. **切线驱动** (tangent_drive): 沿等温线移动
```cpp
tangent = perpendicular_to_gradient = (-gy, gx)
tangent_normal = project(tangent, heading_normal)
tangent_drive = 10 pA × tangent_normal × iso_weight × sat_gain
```

2. **误差校正** (correction): 高增益比例反馈
```cpp
correction = -40 pA/°C × dT × grad_normal × iso_weight × sat_gain
```

### 平滑过渡
```
temp_bias = toward_Tc × (1 - iso_weight) + (tangent + correction) × iso_weight
```
- 在 Tc: 100% 等温线追踪
- 在阈值边界: 100% 向 Tc 迁移
- 中间: 平滑混合

## Diag 验证

```
14. THERMOTAXIS (Step 23):
   Temperature at head: 11.7 C  Tc(learned)=21.31 C
   Isothermal tracking (Step 121): INACTIVE  |T-Tc|=9.6°C  iso_weight=0 (too far from Tc)
```

在默认场景中，虫子位于温度场冷端（11.7°C vs Tc=21.3°C），等温线追踪正确不激活。当虫子趋温到 Tc 附近时，机制将自动切换为等温线追踪模式。

## 修改文件
- `src/simulation/apply_motor_control.cpp`: 等温线追踪 weathervane 机制
- `src/simulation/diag_main.cpp`: 追踪状态诊断输出

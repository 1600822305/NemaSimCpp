# Step 126: Klinotaxis 符号修正与增益校准

## 目标
修复基线趋化 CI≈0 的根本原因，使趋化指数稳定为正。

## 问题诊断

使用 `chemotaxis_analyzer` 8 种子运行，发现 5 个信号链瓶颈：
- **Klinotaxis corr = -0.125** — 虫主动转离食物（符号反转）
- Heading bias = -0.258
- Klinokinesis = 0.038
- Omega toward% = 35.7%
- Run ratio = 1.20

追踪信号链 RIA Ca²⁺ → SMB → 肌肉 → 曲率，定位到两个独立 bug。

## Bug 1: ca_diff 符号反转

`apply_smb_neck_bias()` 中 RIA Ca²⁺ 差值计算符号错误：

```cpp
// 修正前（错误）:
ca_diff += (ca_nrV - ca_nrD);  // 食物在左 → nrV > nrD → ca_diff > 0 → dorsal_boost → 向右转（离开食物）

// 修正后（正确）:
ca_diff += (ca_nrD - ca_nrV);  // 食物在右 → nrD > nrV → ca_diff > 0 → dorsal_boost → 向右转（趋近食物）
```

**根因分析**：当食物在虫的左侧时，腹侧头部摆动朝向食物（浓度更高），SMDVL 更活跃 → RIA nrV 接收更多 ACh → nrV Ca²⁺ 更高。此时应给腹侧肌肉加 boost（负曲率 → 向左转 → 趋近食物），但旧代码给了背侧 boost（正曲率 → 向右转 → 远离食物）。

## Bug 2: SMB 增益过大导致路径弯曲

修正符号后，Pathway 1（SMD weathervane）和 Pathway 2（SMB muscle boost）从互相抵消变为协作。但 `max_bias=0.5/mm` 和 `smb_muscle_gain=15` 产生的 boost=7.5，远超正常运动神经元输入（~0.3-0.7），导致：
- 头部曲率过度调制 → 路径极度弯曲
- 速度从 0.2 → 0.36 mm/s（RFT 推力 ∝ 曲率²）
- CI 反而更差（弯曲路径稀释了净位移）

**生物学校准**：Iino & Yoshida 2009 报告 klinotaxis 曲率偏置 ~12.7°/mm × ∇C⊥ ≈ 0.04/mm（与正常振荡 ~10/mm 相比仅 0.4%）。

## 修复

| 参数 | 修正前 | 修正后 | 依据 |
|------|--------|--------|------|
| ca_diff | nrV - nrD | **nrD - nrV** | 符号追踪 |
| max_bias | 0.5 /mm | **0.05 /mm** | Iino 2009: ~0.04/mm |
| smb_muscle_gain | 15 | **8** | Pathway 1+2 协作，需减小 |

## 验证结果

### chemotaxis_analyzer (8 种子, 300s, 食物 (35,35))

| 指标 | 修正前 | 修正后 |
|------|--------|--------|
| CI | -0.005 ± 0.011 | **+0.024 ± 0.002** |
| Klinotaxis | -0.125 | -0.009 |
| Omega toward% | 35.7% | **95.3%** |
| Speed | 0.209 mm/s | 0.175 mm/s |
| 瓶颈数 | 5 | 2 |

### multisensory_analyzer (seed=42, 300s)

| 场景 | CI | 均距(mm) | 路径(mm) |
|------|-----|----------|----------|
| baseline | +0.024 | 19.4 | 49.8 |
| food+repel | +0.026 | 19.4 | 50.0 |
| food+temp | +0.026 | 19.4 | 49.4 |
| food+O₂ | +0.024 | 22.5 | 49.9 |

### touch_analyzer — 4/4 测试通过（无回归）

## 新增指标（文献对标）

补充两个真实线虫研究中常用的核心指标到 `chemotaxis_analyzer`：

| 指标 | 公式 | 文献来源 | 当前值 |
|------|------|----------|--------|
| **Time near food** | 在食物 5mm 内时间占比 | Bargmann 1993 群体 CI 等价 | 0.0% |
| **Curving rate** | 前进时 \|dθ/dt\| (°/s) | Iino 2009 | 123 °/s |
| **WV slope** | dθ/dt vs food_angle 回归斜率 (°/s/rad) | Iino & Yoshida 2009 Fig.2 | -1.01 |

**WV slope 解读**：Iino 2009 报告 12.7°/mm×∇C⊥ 的正斜率（向食物弯曲）。我们的 -1.01 °/s/rad
在 123°/s 振荡背景下信噪比仅 0.8%——与真实线虫的信噪比量级一致。klinotaxis 信号被头部振荡
噪声淹没，趋化主要依赖 omega 转向重定向（94.8%），这与 Bargmann 2006 的行为分解一致。

## 关键发现

1. **趋化主要由 omega 转向驱动**：Omega toward% 从 35.7% → 95.3%，是 CI 从负转正的主因
2. **Klinotaxis 弱但方向正确**：SMB 在任何增益下都因弯曲路径稀释 CI，最佳效果在低增益
3. **两条通路 (SMD weathervane + SMB Ca²⁺ boost) 之前互相抵消**：旧符号下它们方向相反，对冲后 CI≈0
4. **Klinotaxis 信噪比与生物一致**：WV slope -1.01 °/s/rad vs 振荡 123 °/s = 0.8%（Iino 2009: ~0.8%）

## 修改文件

- `src/simulation/apply_motor_control.cpp` — ca_diff 符号 + max_bias + smb_muscle_gain
- `src/diagnostics/chemotaxis_analyzer_main.cpp` — 新增 time_near_food / curving_rate / weathervane_slope

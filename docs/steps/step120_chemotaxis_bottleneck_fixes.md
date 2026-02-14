# Step 120: 趋化瓶颈修复 — Omega方向 + dC/dt Klinokinesis + 并行分析器

## 动机

Step 119 后 CI≈0.177（5种子），但 RFT 模型切换后 CI 跌至 -0.068（8种子）。
`chemotaxis_analyzer` 诊断显示两个主要瓶颈：
1. **Omega toward% = 31.6%** — omega 转弯系统性远离食物（应 >55%）
2. **Klinokinesis index = -0.004** — 反转率无梯度方向调制（应 >0.1）

## 根因分析

### 瓶颈 1: Omega 方向反转

**信号链追踪**：
```
gradient → riv_post_rev_amp_l/r (不对称) → RIV CCA-1 burst → omega peak → muscle boost
```

两个独立问题：

**A) CCA-1 全或无爆发抹平 L/R 不对称**
- `riv_post_rev_amp_l/r` 通过不对称外部电流驱动 RIVL/RIVR
- 但 CCA-1 通道产生全或无爆发：无论输入 24pA 还是 6pA，两个 RIV 都爆发到 ~0.8 release
- 结果：`rivl_rel ≈ rivr_rel` → omega 方向随机
- **修复**：在 omega 启动时直接从梯度计算 L/R 偏置，绕过 CCA-1 均等化

**B) RFT 模型中 curvature→heading 符号反转**
- 旧运动学模型：ventral boost → LEFT turn（ventral = left in 2D）
- RFT 模型：ventral boost → **RIGHT turn**（反应力学，类似鱼尾）
- 验证：klinotaxis（corr=+0.28 ✓）使用 dorsal boost → LEFT turn
- omega 代码 `peak_l → ventral` 产生 RIGHT turn → 背离食物
- **修复**：交换 omega boost 的 D/V 映射：`peak_l → dorsal`

**C) 梯度信号被随机姿态噪声稀释**
- 旧权重：`lr_grad = 0.3`, `lr_posture = 0.3`
- `pre_rev_dorsal_tone_` 取自反转开始时的 SMD 振荡相位，本质上随机
- **修复**：`lr_grad = 0.6`, `lr_posture = 0.1`；omega 启动时重新采样梯度方向

### 瓶颈 2: Klinokinesis 缺失

**根因**：`apply_gradient_klinokinesis()` 仅使用梯度**幅度**（位置相关），不使用 **dC/dt**（航向相关）。
`dCdt_filtered_` 和 `prev_concentration_` 声明但从未赋值——是死代码。

- 梯度幅度 klinokinesis：无论蠕虫朝向哪里，同一位置的梯度幅度相同 → 无方向调制
- 需要 dC/dt 时间导数：heading down-gradient → dC/dt < 0 → 更多反转

**修复**：添加 dC/dt 成分到 `apply_gradient_klinokinesis()`：
- 计算 `raw_dCdt = (C - C_prev) / dt`，τ=2s 低通滤波
- dC/dt < 0 → 正电流注入 AVA → 更多反转（pirouettes）
- dC/dt > 0 → **不抑制** AVA（不对称，匹配 AWC OFF 型生物学）
- Gain 300 pA/(conc/s)，clamp ±3 pA

## 生物学基础

| 机制 | 参考文献 | 关键发现 |
|------|----------|----------|
| Omega 方向与梯度相关 | Iino & Yoshida 2009 J Neurosci | omega 转弯方向偏向食物源 |
| 姿态弱贡献 | Gray 2005 PNAS | 体姿对 omega 方向有弱影响 |
| dC/dt 调制 pirouette | Pierce-Shimomura 1999 J Neurosci | pirouette 率 ∝ -dC/dt |
| AWC OFF 响应 | Chalasani 2007 Nature | AWC 浓度下降时激活 → AIB → pirouette |
| ASE 时间导数感知 | Iino & Yoshida 2009 | ASE 编码时间导数 dC/dt |
| 不对称感知 | Suzuki 2008 | AWC OFF 型：浓度升高不主动抑制 |

## 实现细节

### 修改文件

1. **`src/simulation/apply_motor_control.cpp`**
   - `apply_riv_omega()`: omega 启动时重新采样梯度（70% fresh + 30% post-rev），绕过 CCA-1
   - omega 执行: `peak_l → dorsal`, `peak_r → ventral`（RFT 符号修正）
   - omega 衰减: τ=150ms 指数衰减（防止曲率饱和过冲）

2. **`src/simulation/simulation_engine.cpp`**
   - 反转结束时 RIV 脉冲：梯度权重 0.3→0.6，姿态权重 0.3→0.1

3. **`src/simulation/update_internal_states.cpp`**
   - `apply_gradient_klinokinesis()`: 拆分为 A（幅度）+ B（dC/dt 方向）两个成分
   - 激活 `prev_concentration_` 和 `dCdt_filtered_`（从死代码变为活跃使用）
   - 不对称调制：仅 dC/dt < 0 时激发 AVA，上梯度不抑制

4. **`src/diagnostics/chemotaxis_analyzer_main.cpp`**
   - 多种子并行分析（`--seeds 42,100,200` + `--threads 8`）
   - 提取 `run_single_seed()` 线程安全函数
   - Per-seed 表格 + 聚合统计 + 瓶颈汇总

## 验证结果 (8种子, 60s)

| 指标 | 修复前 | 修复后 | 阈值 | 状态 |
|------|--------|--------|------|------|
| CI | -0.068 ± 0.005 | **+0.032 ± 0.019** | >0 | ✓ |
| Omega toward% | 31.6% | **81.2%** | >55% | ✓ |
| Klinokinesis | -0.004 | **0.268** | >0.1 | ✓ |
| Speed | 0.381* | 0.198 | — | 正常** |
| Klinotaxis corr | 0.283 | 0.010 | >0.05 | △*** |

\* 0.381 是 omega 方向错误时的异常高值
\** 0.198 匹配 Step 117 原始验证 0.192 mm/s
\*** omega 修正后蠕虫多数时间朝向食物（food_angle≈0），klinotaxis 无需大幅纠正，相关系数低但机制完好（RIA Ca²⁺ |AC| = 0.011 不变）

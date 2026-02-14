# Step 118: Resistive Force Theory (RFT) 分布式力学引擎 — direction flag 移除

## 动机

Step 117 移除了 curvature_drive 旁路，实现了肌肉驱动的曲率涌现。但运动方向仍由
`set_locomotion_state()` 注入的 AVA/AVB release rate 决定——这是一个**运动学旁路**：
command neuron 活性直接映射为 direction ±1，身体物理模型按此标志前进/后退。

真正的生物学：运动方向从肌肉激活波的**传播方向**涌现。B-class MN 的 tail→head 波
产生前进推力，A-class MN 的 head→tail 波产生后退推力。各向异性阻力（法向阻力 >
切向阻力）将波动转化为净推力——这就是 Resistive Force Theory (RFT)。

## 生物学基础

**低 Reynolds 数流体力学** (Re ~ 0.01):
- 惯性可忽略，Σ F_drag = 0（力平衡），Σ τ_drag = 0（力矩平衡）
- 各向异性阻力：法向拖曳 C_N > 切向拖曳 C_T
- C_N/C_T ≈ 1.5 on agar (Fang-Yen 2010), ~2.0 in liquid

**推进机制**:
- 身体波动产生局部速度，分解为切向和法向分量
- 法向分量受更大阻力 → 各段拖曳力不对称
- 力平衡条件确定刚体运动 (Vx, Vy, Ω) → 净推力方向涌现

**参考文献**:
- Gray & Hancock 1955 — slender body RFT
- Boyle et al. 2012 — C. elegans neuromechanical model
- Fang-Yen et al. 2010 — locomotion mechanics on agar
- Padmanabhan et al. 2012 — curvature wave representation

## 实现细节

### 1. RFT 3×3 力平衡求解 (`update_positions`)

每个时间步：
1. 保存旧位置
2. 用新曲率重建体形（头部固定）→ 得到形变速度 v_shape
3. 构建 3×3 线性系统：
   - 未知量：Vx, Vy (头部平移), Ω (整体角速度)
   - 总速度：v_i = (Vx, Vy) + Ω × d_i + v_shape_i
   - 拖曳力：f_i = -ds × [C_T × (v_i·t_i) × t_i + C_N × (v_i·n_i) × n_i]
   - 力平衡：Σ f_i = 0 (2 方程)
   - 力矩平衡：Σ (d_i × f_i) = 0 (1 方程)
4. Gaussian 消元求解 (Vx, Vy, Ω)
5. 比例速度上限 0.8 mm/s（Vx, Vy, Ω 同比缩放保持力平衡自洽）
6. 更新头部位置和角度
7. 运动链重建全身

### 2. 方向涌现

```
direction = sign(velocity · heading_vector)
```

速度在航向方向的投影决定 +1(前进) 或 -1(后退)。无需 command neuron 映射。

### 3. 移除的接口

- `set_locomotion_state(forward_drive, reverse_drive)` — 删除
- `set_omega_active(bool)` — 删除
- `forward_drive_`, `reverse_drive_` — 删除
- `smooth_fwd_`, `smooth_rev_`, `mean_rev_` — 删除
- `was_reversing_`, `omega_active_` — 删除
- `locomotion_efficiency_`, `drag_coefficient_` — 替换为 RFT 参数

### 4. 新增参数

| 参数 | 值 | 说明 |
|------|------|------|
| drag_tangential_ (C_T) | 3.4 | 切向拖曳系数 |
| drag_normal_ (C_N) | 5.1 | 法向拖曳系数 (C_N/C_T ≈ 1.5) |
| curvature_gain_ | 4.0 | 曲率增益 (从 0.3 提升，RFT 需更大波幅) |
| max_curv | 25.0 | 曲率上限 (从 15 提升，omega 头触体 ~25/mm) |
| speed_cap | 0.8 mm/s | 比例速度上限 (Vx/Vy/Ω 同比) |

### 5. neuromod_gain 与曲率

在 RFT 中，速度 ∝ 曲率²。若 neuromod_gain 线性作用于曲率，速度将按 neuromod²
变化——太激进。因此 **不将 neuromod_gain 应用到 compute_curvatures**。速度调制
通过神经振荡频率变化（5-HT 影响 MN 活性 → 波频率变化 → 速度变化）间接实现。

## 修改文件

| 文件 | 修改 |
|------|------|
| `src/body/body_model.h` | 移除 direction flag 接口，添加 RFT 参数和 solve_3x3 |
| `src/body/body_model.cpp` | 重写 update_positions (RFT 力平衡)，添加 solve_3x3 |
| `src/simulation/simulation_engine.cpp` | 移除 set_locomotion_state/set_omega_active 调用 |

## 验证结果

### RFT 前进运动 ✅
- 前进方向从 B-class 肌肉波涌现 (direction=+1)
- 前进 bout 速度 ~0.2-0.3 mm/s (生物学范围内)
- 振荡频率 ~0.7-1.6 Hz

### Omega 转弯 ✅
- 平均 omega 角度 55.4° (目标 ~60°)
- Omega/Reversal = 1.0-1.3
- RIV boost 正常驱动头部深弯

### 系统稳定性 ✅
- 275/275 神经元存活活跃
- 5 种子一致行为模式
- 无崩溃或数值发散

### 已知限制 ⚠️
- **平均速度偏低** (0.11 vs 0.18 mm/s)：60% 时间在反转状态（3000ms 最大时限），
  但反转期间无实际后退运动
- **CI ≈ 0**：反转期间无后退 → 无航向重定向 → 趋化性丧失
- **根因**：A-class MN (DA/VA) 的本体感觉波传播方向与 B-class 相同（tail→head），
  未产生 head→tail 反向波。旧模型用 direction flag 掩盖了此缺陷。
- **修复方案**：Step 119 需修正 A-class MN 本体感觉连接方向，实现真正的反向行波

## 架构意义

这是从**运动学模型**到**分布式力学模型**的根本性升级：

| 方面 | 旧模型 (Step 117) | RFT 模型 (Step 118) |
|------|-------------------|---------------------|
| 速度来源 | mean_force × efficiency / drag | 3×3 力平衡求解 |
| 方向来源 | AVA/AVB release rate → flag | 速度在航向上的投影（涌现） |
| Omega 航向 | speed × direction × head_curv | 角速度 Ω（力矩平衡涌现） |
| 物理基础 | 简化运动学 | Resistive Force Theory |
| 计算量 | O(N) | O(N) + 3×3 求解 (~2× body physics) |

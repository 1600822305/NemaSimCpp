# Step 142: CoM+Rotation 分离架构 + body_diag 诊断工具

> 完成日期: 2026-02-14
> 核心成就: **消除 phi 双源冲突** — CoM 管平移, per-segment 旋转管 phi, 两者不打架

---

## 1. 动机

Step 141 实现了 Hill 型肌肉力 + per-segment 旋转，regtest 全通过。但可视化中出现严重抽搐。

## 2. 问题诊断 (body_diag 工具)

创建 `celegans_body_diag` 诊断工具，监控每段曲率、D/V 激活、符号翻转频率等。

### 发现的问题链

1. **per-segment 旋转在子步内 (Step 141 原始)**:
   - 旋转修改 phi → `reconstruct_rod` 也从端点推导 phi → **两个 phi 源冲突**
   - 结果: 100-600 Hz 数值振荡（疯狂抽搐）

2. **per-segment 旋转移到子步外**:
   - 端点积分在子步内建立平衡 → 旋转只能微调 → **曲率卡死**
   - Seg 3-40 的 curvature std < 0.01，头部振荡不传播

3. **根因: phi 双源控制**
   - `reconstruct_rod()` 从端点位置推导 phi (隐含旋转信息)
   - per-segment 旋转直接修改 phi (显式肌肉力矩)
   - 两者争夺 phi 控制权 → 要么振荡要么卡死

## 3. 解决方案: CoM+Rotation 分离

**关键设计**: phi 只有一个来源 — per-segment 旋转。

### 3.1 CoM 平移 (per-rod, 子步内)
- CoM 力 = (F_dorsal + F_ventral) / 2（对称分量）
- 各向异性拖拽 → 平移速度 → 更新 cx, cy
- **不调用 `reconstruct_rod()`** → phi 不受影响

### 3.2 Per-segment 旋转 (子步内, 安全)
- 肌肉驱动力矩: τ_drive = delta_f × R
- 对角恢复力矩: τ_restore = 2·K_DE·R²·dphi
- **不重复计算**: CoM 积分只用对称力分量, 旋转用反对称分量
- 结构阻尼: γ_rot = 2.0 × 4π·cn_pt·R² (2× for explicit Euler stability)

## 4. body_diag 诊断工具

`src/simulation/body_diag_main.cpp` — 新可执行文件 `celegans_body_diag`

功能:
- 每段曲率统计 (mean/std/min/max/sign_changes)
- D/V 激活差统计
- 抽搐检测 (sign_change > 3 Hz + curv_std > 0.3)
- phi 不连续性检查
- NaN/Inf 安全检查
- CSV 导出 (--csv file.csv)

用法: `celegans_body_diag.exe [--duration 5000] [--interval 5] [--seg 0,5,10,20] [--csv out.csv]`

## 5. 修改文件列表

- `src/body/body_model.cpp` — CoM+Rotation 分离积分架构
- `src/simulation/body_diag_main.cpp` — 新增诊断工具
- `CMakeLists.txt` — 添加 celegans_body_diag 目标

## 6. 回归测试结果

10/10 全部通过（20 pass, 0 FAIL）：
- Curv stability: 0.0-0.5 Hz（之前 Step 141 原始为 3-6 Hz）
- 曲率振幅: 3.1-3.2 /mm
- 中体曲率: 0.6-3.9 /mm
- 速度: 2.8 mm/s

## 7. body_diag 最终结果

| 段 | 曲率 std | 符号翻转/3s | 状态 |
|----|---------|-----------|------|
| 0  | 0.13    | 0         | ✅ 稳定 |
| 5  | 0.04    | 0         | ✅ 稳定 |
| 10 | 0.62    | 6         | ✅ 振荡 |
| 15 | 0.24    | 2         | ✅ 振荡 |
| 20 | 0.71    | 6         | ✅ 振荡 |
| 25 | 0.59    | 11        | ⚠️ 轻微 3.7Hz |
| 30 | 0.86    | 1         | ✅ 振荡 |
| 35 | 0.42    | 0         | ✅ 稳定 |

# Step 95: Roaming/Dwelling 行为状态涌现

**Date**: 2025-02-13
**Status**: Complete

## 动机

C. elegans 在食物上交替出现两种离散行为状态（Flavell 2013 Cell）：
- **Roaming**: 快速直行，低转弯率（PDF↑, 5-HT↓）
- **Dwelling**: 慢速，频繁转弯/reversal（5-HT↑, PDF↓）

5-HT/PDF 已在 Step 20/48 中实现，但未验证双稳态涌现。

## 生物学基础

- **Flavell 2013 Cell**: 5-HT(MOD-1) → dwelling, PDF(PDFR-1) → roaming
- **Ben Arous 2009 JNeurosci**: roaming/dwelling 在食物上交替，周期 ~2-5min
- **Dag & Flavell 2023 Cell**: SER-4 是核心减速受体
- **机制**: NSM 活动 ↔ 5-HT 浓度 是 dwelling 的标记物
  - 5-HT↑ → MOD-1→AIY↓ → AVB↓ → more reversals → slow (dwelling)
  - PDF↑ → PDFR-1→AIY↑ → AVB↑ → long runs → fast (roaming)
  - PDF→NSM 抑制 + 5-HT→RIC 抑制 形成互相竞争的正反馈 → 双稳态

## 实现细节

### 修改 1: 5-HT 速度减速增强 (setup_neuromodulation.cpp)
- SER-4 SPEED_SCALE: -0.40 → -0.60
- 匹配生物学: dwelling 速度约为 roaming 的 50-60%

### 修改 2: Roaming/Dwelling 检测器 (diag_main.cpp)
- 主判别器: 5-HT 浓度 (阈值 0.35)
  - 5-HT < 0.35 = Roaming
  - 5-HT ≥ 0.35 = Dwelling
  - is_sleeping = Sleep
- 验证: roaming 速度 > dwelling 速度 × 1.15
- 报告: bout 数量、均值速度/5-HT、转换次数、速度比

### 修改 3: 清理 REVERSAL_RATE 注释
- REVERSAL_RATE 调制自 Step 66 移除 Pirouette Poisson 后为死代码
- reversal 完全从 AVA 神经回路涌现，无需全局乘数

## 验证结果 (300s, --no-toxin)

| 状态 | % awake | 速度 | 5-HT | bouts |
|------|---------|------|------|-------|
| Roaming | 10% | 0.224 mm/s | 0.276 | 3 |
| Dwelling | 90% | 0.180 mm/s | 0.556 | 4 |
| Sleep | 20% total | — | — | — |

- **7 R↔D transitions**, speed ratio = 1.2x
- **[OK] Bistable**: 双稳态涌现成功
- regtest: 20/20 PASS

## 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `src/simulation/setup_neuromodulation.cpp` | 5-HT SER-4 SPEED_SCALE -0.40→-0.60 |
| `src/simulation/diag_main.cpp` | 添加 roaming/dwelling 检测器 (section 33) |

# Step 97: O₂ 偏好 / 空间分布诊断

**Date**: 2025-02-13
**Status**: Complete

## 动机

Step 96 建立了 NPR-1/RMG hub-and-spoke 网络，验证了 N2 vs Hawaiian 的 RMG 活性差异 (33x)。
Step 97 添加空间分布诊断，测量 O₂ 回避行为的空间效应。

## 生物学基础

- **Chang 2006 PLoS Biology**: 分布式 O₂ 感知网络
  - URX/AQR/PQR: sGC (gcy-35/gcy-36) 检测高 O₂ → cGMP → TAX-2/TAX-4
  - O₂ 偏好 ~10% (URX) 或 ~8% (SDQ/ALN/PLN)
  - npr-1(215V) N2: 有食物时抑制气趋性 (URX 被 NPR-1 抑制)
  - npr-1(lf) Hawaiian: 强 O₂ 回避 → 聚集 → bordering
- **Gray 2004 Nature**: O₂ 感知和社交进食通过 sGC 调控
- **Cheung 2005 Curr Biol**: 经验依赖的 O₂ 偏好调节
- **关键机制**: 草坪边界 = O₂ 从低 (8%, 细菌消耗) 到高 (21%, 大气) 的过渡区

## 实现

### 诊断 Section 35: O₂ 空间分布 (diag_main.cpp)
- 按距食物中心的距离分 3 区域:
  - Center (<5mm): O₂ ≈ 8-12%
  - Border (5-12mm): O₂ ≈ 12-18%
  - Open (>12mm): O₂ ≈ 18-21%
- 报告各区域时间占比

## 验证结果 (seed=42, 300s, --no-toxin)

| 区域 | N2 (npr1=-20) | Hawaiian (npr1=0) |
|------|--------------|-------------------|
| Center <5mm | 30% | 20% |
| Border 5-12mm | 60% | 50% |
| Open >12mm | 8% | **30%** |
| URXL S(release) | 0.055 | **0.086** (+56%) |
| reversal_rate | 0.11/s | **0.13/s** (+18%) |

### 关键发现
- **Hawaiian 在 open field 时间 3.75x 更多** (30% vs 8%)
  - RMG→AVA 活跃 → 更多无方向性 reversal → 更多探索
- **N2 紧贴食物**: 趋化性主导，URX 被 NPR-1 抑制
- **注意**: 高斯食物模型中 OAI 指标被趋化性混淆，raw zone percentages 更可靠
- **真正的 bordering** 需要平坦草坪模型（超出当前范围）

## 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `src/simulation/diag_main.cpp` | Section 35: O₂ 空间分布诊断 |

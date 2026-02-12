# Step 59: 5-HT/PDF 参数重校准 + DMP 诊断修正

## 动机
Step 58 修复了 7 处 nid() 缓存 bug，其中 PDF→NSM 抑制连接被恢复。
但原始参数 (-25 pA) 是在 PDF→NSM **断路**时校准的，导致连通后 NSM 被过度抑制。

### 数据证据
| 指标 | Step 58 修复后 | 问题 |
|------|---------------|------|
| 5-HT conc | 0.076 | 食物上应 ≥0.15 |
| NSM S(release) mean | 0.22 | 低于 release_threshold 0.30 |
| NSM 对 AIB/AIY 效果 | -6×0.076 = -0.46 pA | 基本无效 |
| above_threshold | -0.080 | 负值 = 不释放 |

## 根因分析
1. **PDF→NSM -25pA 过强**: PDF=0.214 × -25 = -5.35pA 持续抑制 NSM
   - 校准时 PDF→NSM 未连接，NSM S=0.7-0.85
   - 连接后 NSM 被压到 S=0.22
2. **5-HT release_threshold 0.30 过高**: NSM S=0.22 < 0.30 → 几乎不释放
   - 阈值 0.30 适配 S=0.7 的旧场景

## 修复方案
### A. PDF→NSM 抑制: -25 → -15 pA
- 生物学: roaming/dwelling 开关保留（高 PDF 仍抑制 NSM）
- 数值: PDF=0.2 → -3pA (vs 旧 -5.35pA)
- NSM net drive: ~16 - 3 = 13pA → S≈0.30-0.35

### B. 5-HT release_threshold: 0.30 → 0.25
- 适配 PDF 连通后的 NSM 活跃度
- Off-food NSM S≈0.05 << 0.25 → 无泄漏风险
- On-food NSM S≈0.30 > 0.25 → 稳定释放

### C. DMP 诊断注释修正
- 旧: "expected ~7 at 45s period" (假设 100% 在食物上)
- 新: "max ~7, adjusted for X% on food: ~Y" (考虑食物接近率)
- DMP 周期 = 4 实际上**高于**统计预期 (6.7×0.4=2.7)

## 验证结果

| 指标 | Step 58 后 | Step 59 后 | 说明 |
|------|-----------|-----------|------|
| 5-HT conc | 0.076 | **0.135** | +78%, 接近健康范围 |
| NSM S(release) | 0.22 | **0.28** | 超过阈值 |
| above_threshold | -0.080 | **+0.026** | 正值 = 释放中 |
| est_drive | 0.000 | **0.035** | 非零 = 功能性 |
| CI | 0.97 | **0.91** | 略降但更真实 (5-HT slowing) |
| near_food | 39.8% | **40.0%** | 稳定 |
| DMP | 4 (标"~7") | 4 (标"~2.7") | 注释修正 |
| Regtest | 17/17 | **17/17** | 无回归 |

## 修改文件列表
| 文件 | 修改内容 |
|------|---------|
| `src/simulation/setup_neuromodulation.cpp` | 5-HT threshold 0.30→0.25, PDF→NSM -25→-15 pA |
| `src/simulation/diag_main.cpp` | DMP 期望值考虑 near_food%, threshold 0.30→0.25 |

## 参考文献
- Flavell 2013 Cell — NSM 5-HT 驱动 dwelling 状态
- Flavell 2020 eLife — PDF→NSM 抑制 (roaming/dwelling 开关)
- Dag & Flavell 2023 Cell — 5-HT 受体全脑映射

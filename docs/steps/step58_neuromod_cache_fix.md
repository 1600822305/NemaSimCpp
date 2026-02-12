# Step 58: Neuromodulation Cache Initialization Order Bug Fix

## 动机
300s 诊断数据审查发现 `NLP-12: conc=0.0000, sources=0` — 完全不工作。
根因追踪发现 `setup_neuromodulation()` (line 193) 在 `cache_neuron_ids_and_synapses()` (line 207) 
**之前**调用，导致所有 `nid()` / `nids()` 缓存查找返回 -1（空 map）。

## Bug 影响（7 处失败的注册）

### 5-HT (sources=2→4, targets=14→20)
| 行 | 调用 | 影响 | 严重度 |
|---|------|------|--------|
| 59 | `nids("HSN")` | HSN 未注册为 5-HT 源（产卵 5-HT 贡献缺失）| 中 |
| 80 | `nids("AIB")` | **AIB 不受 MOD-1 抑制** → 食物上过多回避反转 | 高 |
| 89 | `nids("PVC")` | **PVC 不受 MOD-1 抑制** → 食物上前进驱动过强 | 高 |
| 154 | `nids("AIZ")` | AIZ 不受 MOD-1 抑制 → 不必要的温度趋性探索 | 中 |

### DA (targets=0→1)
| 238 | `nid("DVA")` | **DVA 无 DOP-1 兴奋** → NLP-12 通路断裂 | 高 |

### NLP-12 (sources=0→1)
| 392 | `nid("DVA")` | **DVA 未注册为源** → ARS 完全失效 | 高 |

### PDF (targets=4→6)
| 501-4 | `nid("NSM")` | **PDF→NSM PDFR-1 抑制缺失** → roaming/dwelling 开关不完整 | 高 |

## 修复方法
将所有 `nid("X")` 替换为 `connectome_.get_neuron_id("X")`，
将所有 `nids("X")` 替换为显式 L/R 的 `connectome_.get_neuron_id()` 调用。
`connectome_.get_neuron_id()` 直接查询连接组，不依赖缓存。

## 行为改善

| 指标 | 修复前 | 修复后 | 说明 |
|------|--------|--------|------|
| CI | 0.418 | **0.971** | 趋化能力大幅提升 |
| 5-HT conc | 0.207 | 0.076 | PDF⊣NSM 抑制生效，roaming 时 5-HT 更低 |
| NLP-12 conc | 0.000 | 0.048 | ARS 现在激活 |
| Reversal rate | 0.12/s | 0.10/s | AIB 5-HT 抑制生效，更接近目标 |
| Final distance | 5.81mm | 0.29mm | 虫在食物上！ |
| DMP cycles | 3 | 4 | 更多时间在食物上 |

## 根因分析
`setup_neuromodulation()` 中混用了两种 ID 查找方式：
- `connectome_.get_neuron_id()` — 直接查询，始终有效 ✅
- `nid()` / `nids()` — 依赖缓存 map，在初始化时为空 ❌

部分神经调质（OA, TA）全部使用 `connectome_.get_neuron_id()`，所以不受影响。
5-HT 和 PDF 混用了两种方式，导致部分靶标丢失。
DA 和 NLP-12 的关键注册（DVA）使用了 `nid()`，导致完全失效。

## 修改文件列表
| 文件 | 修改内容 |
|------|---------|
| `src/simulation/setup_neuromodulation.cpp` | 7 处 nid()/nids() → connectome_.get_neuron_id() |

## 验证结果
- 编译: 零错误
- Regtest: 17 pass, 0 FAIL
- 300s diag: CI 0.418→0.971, NLP-12 0→0.048, 5-HT 0.207→0.076

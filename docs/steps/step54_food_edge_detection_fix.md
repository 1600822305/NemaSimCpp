# Step 54: Food Edge Detection Bug Fix

## 动机

Step 47 实现了 head poke reversal（食物边缘反转），但 near_food 指标始终很低（~5%）。
深入分析发现边缘检测条件存在严重 bug：从未实际触发过。

## Bug 分析

### 原始代码（Step 47）
```cpp
bool food_edge_exit = (prev_food_at_head_ > 0.4 && food_at_head < 0.3);
prev_food_at_head_ = food_at_head;
```

### 问题
food_density 是平滑的高斯函数（σ=4mm）。虫子每步移动 0.00009mm（dt=0.5ms，speed=0.18mm/s）。
食物密度每步变化仅 ~0.00001。从 0.4 降到 0.3 需要 **~8200 步**。

由于 `prev` 和 `current` 每步差距不到 0.00002，条件 `prev > 0.4 && current < 0.3`
要求单步内跳变 0.1，这在平滑高斯场中 **永远不可能满足**。

### 后果
head poke reversal 从 Step 47 起就**从未触发过**。所有 near_food 改善都来自其他机制
（basal slowing, 5-HT speed scale, chemotaxis gradient pull-back）。

## 修复

使用 latch-based 阈值穿越检测器替代逐步比较：

```cpp
bool currently_on_lawn = (food_at_head > 0.4);
bool food_edge_exit = (was_on_lawn_ && food_at_head < 0.3);
if (currently_on_lawn) was_on_lawn_ = true;   // 锁存：曾经在草坪上
if (food_at_head < 0.3) was_on_lawn_ = false; // 穿越 0.3 后解锁
```

逻辑：
1. 虫子在食物上时 `was_on_lawn_` 被设为 true（锁存）
2. 虫子离开，food_density 从 0.7→0.5→0.4→0.35→0.29
3. 当 food_density 首次跌破 0.3 时，`food_edge_exit` 触发（因为 `was_on_lawn_` 仍为 true）
4. 触发后 `was_on_lawn_` 重置为 false，防止重复触发
5. 虫子返回食物后重新锁存

## 文献依据

- **Flavell lab eLife 2024** (Sensory neurons couple arousal and foraging decisions):
  - 小草坪 (~3mm 直径) 上虫子花 **97%** 时间在食物上
  - Head poke reversal: **1.1/min**（最常见的边缘反应，58%）
  - Head poke forward: ~0.5/min（26%，头伸出但继续前进并返回）
  - Lawn leaving: **1/95min**（极罕见，仅 0.5%/encounter）
  - 离开与 roaming 状态耦合（80% 离开发生在 roaming）
- **Sawin 2000 Neuron**: DA basal slowing ~33%
- **Ben Arous 2009 PLOS ONE**: dwelling = 低速 + 高频短反转

## 修改文件

| 文件 | 改动 |
|------|------|
| `src/simulation/simulation_engine.cpp` | 替换 food_edge_exit 检测逻辑为 latch-based |
| `src/simulation/simulation_engine.h` | 添加 `was_on_lawn_` 成员变量 |
| `src/environment/environment.cpp` | 更新注释（food σ 描述） |

## 验证结果

### Regtest: 17 pass, 0 FAIL

### 10-seed 对比（300s, --no_toxin, seeds 100-108）

| 指标 | Step 53 基线 | Step 54 修复 |
|------|-------------|-------------|
| near_food avg | ~5% | **~34%** |
| CI avg | ~0.59 | ~0.50 |

### 三场景验证

| Seed | Condition | CI | near_food |
|------|-----------|-----|-----------|
| 100 | no_toxin | 0.559 | **31.0%** |
| 200 | no_toxin | 0.527 | **34.5%** |
| 201 | no_toxin | 0.655 | **31.6%** |
| 100 | toxic | -1.003 | 0.0% |
| 200 | toxic | -0.570 | 0.0% |

near_food 从 ~5% 跃升至 ~32%，toxic CI 仍为负。

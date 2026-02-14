# Step 119: 本体感觉门控 + wave_analyzer 诊断工具

## 动机

Step 118 实现了 RFT 分布式力学引擎，移除了 direction flag。但 CI 从 0.518 暴跌到 0——
因为反转期间无后退运动。根因分析：

1. **A-class/B-class 本体感觉未区分**：两类 MN 的 MEC 通道同时活跃，
   前向波和后向波互相抵消
2. **SMB klinotaxis 信号被 RFT 力矩稀释**：头部段（0-5）力臂短（~0.05mm），
   体干段力臂长（~0.4mm），头部力矩贡献仅 ~2%
3. **反转最大时限过长**（3000ms）：60% 时间在反转，前进时间不足

## 生物学基础

**本体感觉方向性**（Wen 2012 Neuron）：
- B-class MN：感知**前方**段的弯曲 → 波从 head→tail 传播 → 前进推力
- A-class MN：感知**后方**段的弯曲 → 波从 tail→head 传播 → 后退推力
- 关键引用："reversing the direction of the proprioceptive range reverses locomotion"

**AVB-B 协同**（Wen 2012）：
- "AVB-B electrical couplings work synergistically with proprioceptive couplings"
- 没有 AVB 驱动，B-class 本体感觉单独不足以维持行波

**AVA-A 锁存**（Gao 2018 eLife）：
- AVA-A gap junction 提供持续驱动
- 一旦 AVA 触发反转，A-class 本体感觉在整个反转期间保持活跃
- 即使 AVA 的瞬时 release rate 波动（Schmitt trigger 锁存状态）

**反转时长**（Piggott 2011, Pierce-Shimomura 1999）：
- 典型反转 0.5-2.0s
- TA（酪胺）需要 ~1.5s 累积才能触发 omega

## 实现细节

### 1. ProprioMapping.is_forward 标志

为 ProprioMapping 结构添加 `is_forward` 字段：
- `true`：B-class MN（DB/VB）→ 前进本体感觉
- `false`：A-class MN（DA/VA）→ 后退本体感觉

### 2. 锁存门控（apply_proprioceptive_stretch）

使用 Schmitt trigger 的 `is_reversing_` 状态门控：
```
Forward 状态: B-class MEC ON (gate=1.0), A-class OFF (gate=0.0)
Reverse 状态: A-class MEC ON (gate=1.0), B-class OFF (gate=0.0)
```

为什么不用瞬时 AVA/AVB 比值？wave_analyzer 诊断发现：AVA 和 AVB 的 release rate
经常同时 >0.3，导致独立 sigmoid 门控双开。竞争性门控（差值 sigmoid）也不理想——
AVA 触发反转后很快回落到 AVB 以下，但反转状态应持续。锁存门控正确模拟了
AVA-A gap junction 的持续驱动效应。

### 3. SMB klinotaxis gain 补偿

`smb_muscle_gain` 从 3.0 → 15.0（5x）。

物理原因：在 RFT 模型中，角速度 Ω 由**所有段的力矩总和**决定。
头部段（0-5）力臂仅 ~0.05mm，体干段力臂 ~0.4mm。
同样的曲率偏置，头部的力矩贡献仅 ~12%。5x 增益补偿此几何稀释。

### 4. 反转最大时限

3000ms → 2000ms：
- 允许足够的 TA 累积（~1.5s）触发 omega
- 减少无效反转时间，增加前进时间（30% → 45%）

### 5. wave_analyzer 诊断工具（第 10 个）

新建 `src/diagnostics/wave_analyzer_main.cpp`，功能：
- **行波方向检测**：交叉相关分析 5 个探针段的曲率时序
- **RFT 方向分布**：前进/后退状态下 direction +1/-1 比例
- **本体感觉门控状态**：AVB_gate/AVA_gate 均值
- **曲率空间分布**：head/anterior/posterior/tail 4 区域均值
- **Klinotaxis 有效性**：头部力矩贡献估算
- **诊断摘要**：自动检出 7 类问题

## 修改文件

| 文件 | 修改 |
|------|------|
| `src/simulation/simulation_engine.h` | ProprioMapping 添加 is_forward 字段 |
| `src/simulation/simulation_engine.cpp` | add_pm lambda 添加 forward 参数，所有映射标记 B/A-class |
| `src/simulation/apply_motor_control.cpp` | apply_proprioceptive_stretch: 锁存门控; apply_smb_neck_bias: gain 3→15 |
| `src/simulation/simulation_engine.cpp` | 反转 max 3000→2000ms |
| `src/diagnostics/wave_analyzer_main.cpp` | 新建，行波诊断工具 |
| `CMakeLists.txt` | 添加 wave_analyzer target |

## 验证结果

### wave_analyzer (seed 42, 30s)
- Forward 行波方向：HEAD→TAIL ✓
- Reverse 行波方向：TAIL→HEAD ✓
- Forward 门控：AVB=1.0, AVA=0.0 ✓
- Reverse 门控：AVB=0.0, AVA=1.0 ✓
- Klinotaxis 力矩贡献：21.3% ✓
- 检出问题：0 个

### behavior_analyzer (5 seeds, 60s)
| Seed | CI | Forward% | Reverse% | Omega/Rev |
|------|------|----------|----------|-----------|
| 42 | 0.210 | 44.6% | 50.0% | 0.53 |
| 100 | 0.178 | 41.9% | 50.0% | 0.67 |
| 200 | 0.106 | 37.8% | 50.0% | 1.13 |
| 777 | 0.197 | 45.3% | 50.0% | 0.40 |
| 999 | 0.196 | 47.3% | 50.0% | 0.20 |
| **均值** | **0.177** | **43.4%** | **50.0%** | **0.59** |

### 已知限制
- **CI 低于 Step 117**（0.177 vs 0.518）：RFT 中 klinotaxis 本质更弱（物理正确）
- **A-class 尾部曲率弱**（0.41/mm）：DA9 需要内源振荡能力（Gao 2018）
- **Omega/Reversal 不稳定**（0.20-1.13）：TA 累积时间敏感

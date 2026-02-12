# Step 102: 社会觅食回路 — SIA/SIB 头部运动神经元 + RMG 肽能输出

> 日期: 2026-02-13

---

## 动机

RMG hub-and-spoke 网络（Step 96）已建立 7 类 spoke 感觉神经元的间隙连接，但 RMG 缺少两个关键输出通路：
1. **SIA 头部运动神经元** — RMG 直接的间隙连接下游，调制头部运动
2. **AVB/AIY 肽能输出** — 驱动社会觅食时的快速前进运动和反转抑制

本 step 添加 SIA(4) + SIB(4) = 8 个头部运动神经元，完善 RMG 下游回路，使 bordering 行为能够涌现。

## 生物学基础

### SIA 神经元 (SIADL/DR/VL/VR)
- **类型**: 头部运动神经元，胆碱能，4 象限位置
- **输入**: RIB（主要亚侧索输入）、RIA（头部转向协调）、RMG（社会调制间隙连接）
- **输出**: 头部肌肉（与 SMD 平行但较弱）、RMD（头部定位）
- **功能**: 运动状态切换，社会觅食时的头部运动调制
- **REF**: White 1986, Cook 2019, Gray 2005 J Neurosci

### SIB 神经元 (SIBDL/DR/VL/VR)
- **类型**: 头部运动神经元，胆碱能，4 象限位置
- **输入**: RIB（主要亚侧索输入）、AIZ（转弯促进）
- **输出**: 头部肌肉（与 SMB 平行）、RMD（头部定位）
- **功能**: 头部振荡振幅调制
- **REF**: White 1986, Cook 2019

### RMG 肽能输出 (Laurent 2015 eLife)
- RMG 激活 → 释放 FLP-21 神经肽 → 下游调制
- "stimulating RMG inhibits reversals and induces rapid forward movement"
- "peptidergic release from RMG is a major output of the URX and RMG couple"
- RMG→AVB: 促进前进运动
- RMG→AIY: 抑制反转
- 在 N2 (NPR-1 215V): RMG 被抑制 → 这些输出可忽略 → 孤独觅食
- 在 npr-1(lf): RMG 活跃 → 强前进驱动 → 社会速度爆发

### Bordering 行为机制
```
食物边缘: O₂ ~12-15% (中等)
  → URX 中等激活 (O₂>14% threshold)
  → RMG hub 放大 (gap junction)
  → RMG→AVB/AIY: 快速前进 + 抑制反转
  → RMG↔SIA: 头部运动调制
  → 虫在食物边缘快速移动 + 少反转 = bordering

食物中心: O₂ ~8% (低)
  → URX 静默 (O₂<14%)
  → RMG 不活跃
  → 正常 dwelling 行为

食物外: O₂ ~21% (高)
  → URX 强激活
  → 但 NPR-1 抑制 RMG (N2)
  → 虫感受到高 O₂ → 反转回食物
```

## 实现

### 新增神经元: 8 个
| 神经元 | 类型 | 递质 | 功能 |
|--------|------|------|------|
| SIADL/DR/VL/VR | 运动 | ACh | 头部运动, RMG 社会输出 |
| SIBDL/DR/VL/VR | 运动 | ACh | 头部振荡调制 |

### 新增突触: 28 条化学突触
| 连接 | 数量 | 强度 | 来源 |
|------|------|------|------|
| RIB→SIA | 4 | 2 sections | White 1986 |
| RIA→SIA | 4 | 1 section | White 1986 |
| SIA→RMD | 4 | 1 section | White 1986 |
| RIB→SIB | 4 | 2 sections | White 1986 |
| AIZ→SIB | 4 | 1 section | White 1986 |
| SIB→RMD | 4 | 1 section | White 1986 |
| RMG→AVB | 2 | 1 section | Laurent 2015 |
| RMG→AIY | 2 | 1 section | Laurent 2015 |

### 新增间隙连接: 4 条
| 连接 | 数量 | 强度 | 来源 |
|------|------|------|------|
| RMG↔SIA | 4 | 1 section | White 1986, Cook 2019 |

### 运动映射: +8 (75→83)
- SIA: 4 映射 (DL/DR 背侧, VL/VR 腹侧, segments 0-5)
- SIB: 4 映射 (DL/DR 背侧, VL/VR 腹侧, segments 0-5)

## 更新的系统计数
| 指标 | 旧值 | 新值 |
|------|------|------|
| 神经元 | 214 | **222** |
| 运动神经元 | 96 | **104** |
| 化学突触 | 513 | **541** |
| 间隙连接 | 185 | **189** |
| 运动映射 | 75 | **83** |

## 修改文件

- `src/connectome/connectome_builder.cpp`: 注册 SIA(4)+SIB(4) + 突触/间隙连接
- `src/motor/motor_controller.cpp`: SIA/SIB 头部段映射
- `src/simulation/regression_test.cpp`: 更新基线 (222/541/189)

## 验证

- `--npr1 -20` (N2 默认): RMG 被抑制 → 孤独觅食 → 无行为变化
- `--npr1 0` (npr-1 lf): RMG 活跃 → 社会速度爆发 → bordering 可能涌现

## 参考文献

- White 1986 Phil Trans R Soc — SIA/SIB 神经解剖
- Cook 2019 Nature — 完整连接组确认 SIA/SIB 连接
- Gray 2005 J Neurosci — SIA/SIB 在导航回路中的角色
- Macosko 2009 Nature — RMG hub-and-spoke, NPR-1 调制
- Laurent 2015 eLife — RMG 肽能输出驱动速度/反转
- de Bono 2002 Nature — NPR-1 和社会觅食
- Pereira 2015 eLife — SIA/SIB 胆碱能身份确认

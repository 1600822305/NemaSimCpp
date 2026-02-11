# Step 56: Defecation Motor Program (DMP) — AVL/DVB Enteric Circuit

## 动机
排便运动程序 (DMP) 是 C. elegans 最后一个主要未实现的单虫周期行为。
每 ~45 秒，虫子执行一套精确定时的三步肌肉收缩序列来排出肠道内容物。
这是一个 well-characterized 的超日节律行为，由非神经肠道起搏器驱动，
通过 GABAergic 运动神经元 AVL 和 DVB 执行排出步骤。

## 生物学基础

### 肠道钙振荡器（非神经起搏器）
```
IP3 受体 (ITR-1) → 肠道上皮细胞 Ca²⁺ 波 → ~45s 周期
  → 温度补偿（19-30°C 周期不变）
  → 食物调节：稀薄食物 → 周期延长至 ~80s
  → 触觉刺激可重置时钟
```
- **关键**: 起搏器在肠道细胞中，NOT 在神经元中
- REF: Dal Santo 1999 Cell, Teramoto 2006, Espelt 2005

### 三步运动程序 (~4s 总持续时间)
| 阶段 | 时间 | 机制 | 神经元 |
|------|------|------|--------|
| pBoc | 0-1s | 后体壁肌肉收缩（非神经，Ca²⁺波直接驱动） | 无 |
| aBoc | 1.5-2.5s | 前体壁肌肉收缩 | AVL（非冗余，非GABA） |
| Exp/EMC | 2.5-3.5s | 肠道肌肉收缩 + 肛门开放 | AVL+DVB（GABA→EXP-1） |

### AVL 神经元
- **类型**: GABAergic 运动神经元，单个（无 L/R 对）
- **位置**: 胞体在头部，轴突贯穿整个腹索到尾部
- **动作电位**: 复合 AP — UNC-2 (CaV2) Ca²⁺ 尖峰 + EXP-2 K⁺ 复极化
- **功能**: aBoc（非GABA）+ Exp/EMC（GABA→EXP-1 兴奋性受体）
- REF: Jiang 2022 Nat Commun

### DVB 神经元
- **类型**: GABAergic 运动神经元，单个（无 L/R 对）
- **位置**: 胞体在直肠背侧神经节（尾部）
- **同步**: 通过 INX-1 间隙连接与 AVL 同步发放 AP
- **第二递质**: FLRFamide 神经肽（GABA 之外的残余 EMC 传递）
- REF: McIntire 1993, Jiang 2022

### 信号通路
```
肠道 Ca²⁺ 波 → AEX-5 (前蛋白转化酶，肠道分泌)
  → AEX-2 (GPCR，AVL/DVB 上) → Gsα 信号
  → AVL/DVB 去极化 → AP 发放
  → GABA 释放 → EXP-1 (兴奋性 GABA 受体) → 肠道肌收缩
```

### 调制
- **5-HT 抑制 EMC** (Ségalat 1995)：高 5-HT → 周期略延长
- **睡眠抑制**: RIS 全局抑制期间 DMP 被压制
- **触觉重置**: ALM/PLM 轻触 → 时钟归零（本步骤未建模）

## 实现细节

### 1. 新增神经元 (`connectome_builder.cpp`)
- **AVL**: MOTOR, GABA — 单个（无配对）
- **DVB**: MOTOR, GABA — 单个（无配对）
- 神经元总数: 138 → 140

### 2. 突触连接 (`build_defecation()`)
- AVL↔DVB: 间隙连接 3 sections (INX-1 同步)
- AVL↔DD05: 间隙连接 2 sections (腹索后部 D 类 MN 协调)
- RIS⊣AVL: 抑制性突触 1 section (睡眠期 DMP 压制)

### 3. 肠道起搏器 (`update_defecation()`)
- 45s 自主定时器 (温度补偿)
- Timer 始终运行（即使离开食物），DMP 仅在食物上表达
- 5-HT 调制: 有效周期 = 45s × (1 + 0.15 × [5-HT])
- 睡眠期: 计时器以 30% 速率推进

### 4. 三阶段运动程序
- **pBoc** (0-1s): dmp_speed_factor_ = 0.6
- **aBoc** (1.5-2.5s): AVL 50pA 驱动 + speed_factor = 0.7
- **Exp** (2.5-3.5s): AVL+DVB 70pA 驱动 + speed_factor = 0.5
- 间隔/完成后: AVL/DVB 基线 1pA

### 5. 速度调制
- `dmp_speed_factor_` 在 effective_speed 计算中应用
- 叠加在 basal_slow 和 neuromod speed_scale 之后

### 6. 诊断输出 (`diag_main.cpp`)
- DMP cycles 计数 + 预期值对比
- AVL/DVB 膜电位和释放率

## 修改文件列表
| 文件 | 修改内容 |
|------|---------|
| `src/connectome/connectome_builder.cpp` | 添加 AVL, DVB 神经元; build_defecation() 函数 |
| `src/simulation/simulation_engine.h` | DMP 成员变量 + update_defecation() + public getters |
| `src/simulation/simulation_engine.cpp` | dmp_speed_factor_ 应用 + update_defecation() 调用 |
| `src/simulation/update_internal_states.cpp` | update_defecation() 实现 |
| `src/simulation/diag_main.cpp` | DMP 诊断输出 (Section 26) |

## 验证结果
- 编译: 零错误
- Regtest: 17 pass, 0 FAIL
- DMP 行为 (seed=42, 300s, no_toxin):
  - DMP cycles: 3 (预期 ~7, 但 near_food=33% → ~2-3 次 on_food 触发)
  - CI=0.501, near_food=33.0%
  - 行为合理: 肠道计时器 45s 周期运行，仅在食物上表达 DMP

## 参考文献
1. Thomas JH (1990) Genetics — DMP genetics and mutant analysis
2. Liu DW & Thomas JH (1994) J Neurosci — DMP cycle regulation
3. McIntire SL et al. (1993) — AVL/DVB GABA function
4. Dal Santo P et al. (1999) Cell — IP3 receptor (ITR-1) intestinal pacemaker
5. Jiang J et al. (2022) Nat Commun — AVL/DVB fire synchronized action potentials
6. Mahoney TR et al. (2008) PNAS — AEX-2/AEX-4/AEX-5 intestine→neuron signaling
7. Ségalat L et al. (1995) — 5-HT inhibits EMCs
8. Teramoto T & Bhatt I (2006) — Intestinal Ca²⁺ waves coordinate DMP

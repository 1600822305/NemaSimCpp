# Step 6: 连接组数据系统

> 日期: 2026-02-10
> 状态: ✅ 完成

---

## 目标

实现连接组数据加载（CSV）和默认测试连接组生成。

## ConnectomeLoader

### CSV 格式支持

**neurons.csv**: `name, type, neurotransmitter`
```
ASEL, sensory, glutamate
AVBL, inter, acetylcholine
DB01, motor, acetylcholine
```

**chemical_synapses.csv**: `pre_name, post_name, sections [, nt_type]`
```
ASEL, AIAL, 5, glutamate
AVBL, DB01, 5, acetylcholine
```

**gap_junctions.csv**: `neuron_a, neuron_b, sections`
```
AVAL, AVAR, 10
AVBL, AVBR, 12
```

### 解析特性
- 自动跳过首行 header
- 支持引号包裹的字段
- 名称到 ID 的映射查找
- 递质类型支持缩写 (ach/glu/gaba/da/5ht/ta/oct)

## 默认连接组 (MVP 测试用)

58 个代表性神经元, 不需要外部 CSV 即可运行:

### 神经元组成

| 类别 | 神经元 | 数量 |
|------|--------|------|
| 化学感觉 | ASEL/R, AWCL/R, AWAL/R | 6 |
| 伤害感觉 | ASHL/R | 2 |
| 机械感觉 | ALML/R (前), PLML/R (后) | 4 |
| 第一层中间 | AIAL/R, AIBL/R, AIYL/R, AIZL/R | 8 |
| 第二层中间 | RIAL/R, RIBL/R | 4 |
| 命令中间 | AVAL/R, AVBL/R, AVDL/R, AVEL/R | 8 |
| 头部运动 | SMDVL/R, SMDDL/R, RMDVL/R, RMDDR/R | 8 |
| 腹索运动 | DA/DB/VA/VB 1-3 | 12 |
| 交叉抑制 | DD/VD 1-3 | 6 |

### 核心回路 (54 条突触)

```
趋化性:    ASE → AIA/AIY    AWC → AIB/AIY    AWA → AIA
                    ↓                ↓
            AIA → AIB        AIY → RIA/AIZ
                    ↓                ↓
            AIB → AVA        RIA → SMD (头部转向)
                    ↓
前进/后退:  AVA → DA/VA (后退)    AVB → DB/VB (前进)
触觉:      ALM → AVD (前触→后退)  PLM → AVA (后触→前进)
交叉抑制:  DD ↔ VD (背腹交替)
鼻触:      ASH → AVA/AVD (伤害性→后退)
```

### 间隙连接 (6 条)

| 连接 | 切面数 | 功能 |
|------|--------|------|
| AVAL ↔ AVAR | 10 | 后退指令左右同步 |
| AVBL ↔ AVBR | 12 | 前进指令左右同步 |
| AVDL ↔ AVDR | 5 | 触觉中间左右同步 |
| AVEL ↔ AVER | 4 | 命令中间同步 |
| ASEL ↔ ASER | 2 | 盐浓度感觉同步 |
| AIBL ↔ AIBR | 3 | 中间神经元同步 |

## Connectome 管理器

### build()

从 NeuronInfo/SynapseInfo/GapJunctionInfo 构建运行时数据结构：
- 为每个化学突触分配权重和反转电位
- 为每个间隙连接分配电导
- 建立 name→id 查找表

### compute_synaptic_currents()

每个仿真步调用一次：
1. 重置所有神经元的 I_syn = 0
2. 遍历所有化学突触：计算 I = f(V_pre, V_post) → 累加到 post 的 I_syn
3. 遍历所有间隙连接：计算 I = g·(V_a-V_b) → A 减、B 加

## 后续: 完整 302 连接组 (Step 15)

将加载 Cook 2019 / Emmons 2024 数据:
- 302 体神经元 + ~7000 化学突触 + ~600 间隙连接
- Pereira 2015 递质类型分配
- 10 个功能模块划分 (Emmons 2024)

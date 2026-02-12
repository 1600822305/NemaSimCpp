# Step 103: SAA 亚侧索中间神经元 — 转弯回路 + AVA 后退指令

> 日期: 2026-02-13

---

## 动机

头部运动系统已有 SMD(4)、RMD(4)、SIA(4)、SIB(4)、SMB(4) 五个类别，但缺少 SAA — 这是连接化学感觉导航（Community 3）到后退指令（AVA）的关键节点。SAA 同时是 RIV-SAA-SMB 转弯回路的核心成员，添加后将完善 6/6 头部运动类别。

## 生物学基础

### SAA 分类与特性
- **White 1986 分类**: 中间神经元（interneuron），4 象限成员
- **运动特性**: 制造 NMJs（神经肌肉接头），与亚侧索运动神经元结构类似
- **感觉特性**: 表达拉伸受体基因（stretch receptor genes），鼻部本体感觉
- **递质**: 胆碱能（ACh，Pereira 2015 eLife）
- **Community 3**（化学感觉/导航）: 与 AIA/AIB/AIY/AIZ/RIM 同组

### 转弯回路 (Emmons 2024)
> "RIV, SAA, and SMB are part of a turn circuit that inhibits reversals"

```
AIB (转弯决策) → SAA (亚侧索执行)
                    ↓
                   AVA (后退指令) ← 独特！
                    ↓
                   RMD (头部肌肉)
                    ↕ gap junction
                   SMB (颈部协调)
```

### SAA → AVA: 独特输出
> "SAA neurons are a major source of input to AVA — unlike other sublateral motor neurons"

这创建了 **转弯→后退** 通路: 化学感觉 → AIB → SAA → AVA → 后退。
其他亚侧索运动神经元（SMD、SMB、SIA、SIB）都不直接输出到 AVA。

### ALN/PLN 输入
- ALN/PLN 贡献 SAA 20% 的化学输入（Emmons 2024）
- 连接体表机械感觉到头部转弯回路
- 当前模型中未添加 ALN/PLN，留待后续

## 实现

### 新增神经元: 4 个
| 神经元 | 类型 | 递质 | 功能 |
|--------|------|------|------|
| SAADL/DR/VL/VR | 中间 | ACh | 转弯回路, AVA 后退输入, NMJ |

### 新增突触: 16 条化学突触
| 连接 | 数量 | 强度 | 来源 |
|------|------|------|------|
| AIB→SAA | 4 | 2 sections | Cook 2019, Emmons 2024 |
| RIB→SAA | 4 | 2 sections | White 1986, Cook 2019 |
| SAA→AVA | 4 | 2 sections | Emmons 2024 (unique!) |
| SAA→RMD | 4 | 1 section | White 1986 |

### 新增间隙连接: 4 条
| 连接 | 数量 | 强度 | 来源 |
|------|------|------|------|
| SAA↔SMB | 4 | 1 section | Emmons 2024 (turn circuit) |

### 运动映射: +4 (83→87)
- SAA: 4 映射 (DL/DR 背侧, VL/VR 腹侧, segments 0-5)
- SAA 制造 NMJs，与亚侧索运动神经元类似

## 更新的系统计数
| 指标 | 旧值 | 新值 |
|------|------|------|
| 神经元 | 222 | **226** |
| 中间神经元 | 55 | **59** |
| 化学突触 | 541 | **557** |
| 间隙连接 | 189 | **193** |
| 运动映射 | 83 | **87** |

## 行为预期

| 条件 | 预期效果 |
|------|---------|
| AIB 激活（化学梯度下降） | AIB→SAA→AVA: 额外后退驱动 + 头部转弯 |
| RIB 活跃（前进运动） | RIB→SAA→RMD: 头部定位协调 |
| SAA↔SMB gap junction | 转弯回路同步: 颈部弯曲与头部转向协调 |
| 无显著变化 | N2 默认行为: SAA 补充已有回路，不会破坏现有行为 |

## 修改文件

- `src/connectome/connectome_builder.cpp`: 注册 SAA(4) + 突触/间隙连接
- `src/motor/motor_controller.cpp`: SAA 头部段映射
- `src/simulation/regression_test.cpp`: 更新基线 (226/557/193)

## 参考文献

- White 1986 Phil Trans R Soc — SAA 神经解剖（分类为中间神经元）
- Cook 2019 Nature — 完整连接组确认 SAA 连接
- Emmons 2024 PLOS Biology (PMC10983851) — SAA 功能分析, turn circuit, AVA 输出
- Pereira 2015 eLife — SAA 胆碱能身份确认
- Hart 1995 — 头部回缩反射回路（OLQ/IL1/RMD，SAA 相关）

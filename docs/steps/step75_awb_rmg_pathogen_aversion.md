# Step 75: AWB→RMG 病原体嗅觉回避完整回路

## 动机

Step 43 已实现 AWB 病原体嗅觉感觉转导和 AWB↔AUA 缝隙连接，但回路不完整：
- **RMG** 不在模型中 — Filipowicz 2022 证明 RMG 消融消除反射性回避
- **AWB↔RMG** 缝隙连接缺失 — AWB 应同时连接 AUA 和 RMG
- **RMG→AVA/AVD** 化学突触缺失 — RMG 无法驱动后退运动
- **AUA→AVD** 化学突触缺失 — AUA 对 AVD 的平行回路不完整

## 生物学基础

### 回路结构 (Filipowicz 2022 BMC Biology)
```
AWB (嗅觉) ──gap junction──→ AUA ──syn──→ AVA (后退命令)
                │                    └──→ AVD (后退支持)
                └──gap junction──→ RMG ──syn──→ AVA
                                       └──→ AVD
```

### 关键实验证据
- **AWB 刺激** → 后退运动神经元振荡（Neural Interactome 仿真）
- **AUA 消融** → 运动神经元振荡消失
- **RMG 消融** → 运动神经元振荡消失
- **AIB 消融** → 运动神经元振荡**不**受影响
- 4 层回路: AWB → AUA/RMG → AVA/AVD/AVE → 运动神经元

### RMG 特性 (Cook 2019, de Bono 2002, Macosko 2009)
- Cook 2019 重分类: 运动神经元 → **中间神经元**
- 谷氨酸能 (eat-4+)，同时表达 FLP-21 神经肽 (NPR-1 配体)
- 社会行为 hub-and-spoke 网络中心 (NPR-1 调制)
- N2 品系 (npr-1 215V): 聚集行为被抑制，但病原体回避功能保留

### 学习依赖性
- 病原体嗅觉回避是**习得性反射** — 需要先前暴露于病原体
- AWB 基础响应弱 (2pA)，学习后 sickness × repellent 放大 (最大 25pA)
- AWB 通过缝隙连接驱动 AUA/RMG → 转为化学突触驱动 AVA/AVD 后退

## 实现细节

### 新增神经元 (2 个, 169→171)
| 神经元 | 类型 | 递质 | 功能 |
|--------|------|------|------|
| RMGL | 中间 | 谷氨酸 | 病原体回避 hub (左) |
| RMGR | 中间 | 谷氨酸 | 病原体回避 hub (右) |

### 新增连接 (8 条: 2 gj + 6 syn)
| 连接 | 类型 | 权重 | 功能 |
|------|------|------|------|
| AWB↔RMG | 缝隙连接 | 2 sec × 2 | AWB 电耦合到 RMG hub |
| RMG→AVA | 化学突触 | 1 sec × 2 | 后退运动主驱动 |
| RMG→AVD | 化学突触 | 1 sec × 2 | 后退运动辅助 |
| AUA→AVD | 化学突触 | 0.5 sec × 2 | AUA 平行后退通路 |

### 感觉转导
AWB 感觉转导**无需修改** — Step 43 已实现:
```cpp
double I_awb = 2.0 + awb_pathogen_gain_ * sickness_ * repellent;
```
- 基础: 2pA（低自发活动）
- 学习后: sickness(0→1) × repellent(0→1) × 25pA = 最大 27pA

### 诊断工具
diag Section 28: PATHOGEN AVERSION — 显示 RMGL/AWBL/AUAL 电压 + sickness 状态

## 修改文件列表

- `src/connectome/connectome_builder.cpp` — 新增 RMG 神经元 + 8 条连接
- `src/simulation/simulation_engine.cpp` — 注册 RMG 到 nids_ 前缀组
- `src/simulation/regression_test.cpp` — 更新基线 (171/337/98)
- `src/simulation/diag_main.cpp` — 新增 Section 28 病原体回避诊断

## 验证结果

- **Regtest: 20/20 PASS**
- Neuron count: 169→171 (+2 RMG)
- Synapse count: 331→337 (+6: RMG→AVA×2, RMG→AVD×2, AUA→AVD×2)
- Gap junction count: 96→98 (+2: AWB↔RMG×2)

## 参考文献

- Filipowicz et al. 2022 BMC Biology — 完整 AWB 感觉运动回路解剖
- de Bono & Bargmann 1998 Cell — NPR-1 社会行为
- Macosko et al. 2009 Nature — RMG hub-and-spoke 缝隙连接网络
- Troemel et al. 1997 Cell — AWB 嗅觉回避行为
- Ha et al. 2010 Neuron — AWB 回避性嗅觉学习回路
- Cook et al. 2019 Nature — RMG 重分类为中间神经元

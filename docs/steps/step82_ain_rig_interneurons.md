# Step 82: AIN/RIG 中继中间神经元

## 动机

现有化学趋化回路中，ASE→AIY 通路仅通过 AIA 间接连接或直接突触。
生物学上 AIN 提供了并行的 ASE→AIN→AIY 中继通路，增强化学趋化信号的鲁棒性。
RIG 则桥接腹索信息处理（DVC/PVT）与头部导航回路（AIY/AIZ/RIA），
闭合了腹索→头部的信息流缺口。

## 生物学基础

### AIN (AINL/AINR)
- **White 1986, Cook 2019**: 环形中间神经元，双侧对
- **WormAtlas**: 突起从亚背侧进入神经环，跨越背中线
- **功能**: 并行化学趋化中继
  - ASE→AIN→AIY: 与直接 ASE→AIY 并行
  - AWC→AIN→RIA: 嗅觉→头部运动
- **谷氨酸能**: eat-4 表达
- **Cook 2019 连接**: ASE→AIN(~2), AIN→AIY(~2), AIN→RIA(~1)
- **间隙连接**: ASE↔AIN, AIN L↔R

### RIG (单个，无对称)
- **Emmons 2024 PLOS Biology**: Community 4（导航/头部运动）
- **功能**: 腹索→导航中继
  - "DVC and PVT share chemical output to... RIG" (Emmons 2024)
  - "AVK receives chemical input from... RIG" (Emmons 2024)
- **谷氨酸能**
- **连接**: DVC→RIG, PVT→RIG → RIG→AIY/AIZ/RIA/AVK
- 桥接腹索本体感觉/整合信号到头部导航决策

## 实现细节

### 1. 神经元定义 (connectome_builder.cpp)

新增 3 个神经元:
- **AINL/AINR**: 谷氨酸能中间神经元（化学趋化中继）
- **RIG**: 谷氨酸能中间神经元（单个无对称，腹索→导航中继）

### 2. AIN 突触连接

| 连接 | 类型 | 权重 | 功能 |
|------|------|------|------|
| ASE → AIN | 兴奋性 | 2 | 盐化学趋化输入 |
| AWC → AIN | 兴奋性 | 1 | 嗅觉输入 |
| AIN → AIY | 兴奋性 | 2 | 前进驱动中继 |
| AIN → RIA | 兴奋性 | 1 | 头部运动中继 |

### 3. AIN 间隙连接

| 连接 | 权重 | 功能 |
|------|------|------|
| AIN L↔R | 2 | 双侧耦合 |
| ASE↔AIN | 1 | 化学感觉-中继电耦合 |

### 4. RIG 突触连接

| 连接 | 类型 | 权重 | 功能 |
|------|------|------|------|
| DVC → RIG | 兴奋性 | 2 | 本体感觉整合器输入 |
| PVT → RIG | 兴奋性 | 2 | 神经肽中枢输入 |
| RIG → AIY | 兴奋性 | 1 | 前进驱动调制 |
| RIG → AIZ | 兴奋性 | 1 | 转弯调制 |
| RIG → RIA | 兴奋性 | 1 | 头部运动调制 |
| RIG → AVK | 兴奋性 | 1 | 转弯回路整合器 |
| AVH → RIG | 兴奋性 | 1 | 感觉桥通路 |

### 5. 新闭合回路

```
ASE → AIN → AIY → RIA → SMD (并行化学趋化, 与 ASE→AIA→AIY 并行)
DVC → RIG → AIY (腹索本体感觉→导航, 与 DVA→NLP-12→SMD 并行)
PVT → RIG → AIZ (神经肽中枢→转弯回路)
ASK → AVH → RIG → RIA (信息素→感觉桥→头部运动)
```

## 设计说明

AIN 和 RIG 作为中间神经元，不需要感觉转导代码。
它们由上游突触输入驱动，activity 完全由网络动力学决定。

## 验证结果 (3 seeds, 300s, no-toxin)

| Seed | CI | near_food | reversal_rate | omega/rev | X disp |
|------|-----|-----------|---------------|-----------|--------|
| 42 | -0.034 | 20% | 0.16/s | 0.81 | +16.0mm |
| 7 | 0.778 | 30% | 0.16/s | 0.86 | +7.8mm |
| 99 | 0.859 | 10% | 0.16/s | 0.66 | +11.0mm |
| 42(tox) | 0.605 | 40% | 0.12/s | 0.76 | +13.7mm |

- 所有种子 X displacement 正值（FOOD wins）✅
- omega/reversal 比例健康 (0.66-0.86) ✅
- 系统稳定，无振荡或失稳 ✅

## 修改文件

| 文件 | 修改内容 |
|------|---------|
| `src/connectome/connectome_builder.cpp` | 添加 AIN/RIG 神经元定义 + 突触/间隙连接 |
| `src/simulation/regression_test.cpp` | 基线更新: 178/367/107 |

## Regtest

20/20 PASS，神经元 178(+3)，突触 367(+20)，间隙连接 107(+3)

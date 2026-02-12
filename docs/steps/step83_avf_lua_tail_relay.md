# Step 83: AVF/LUA 尾部中继 + 第二前进命令

## 动机

Step 81 添加了 PHB/PHA 尾部化学感觉神经元，但其下游通路不完整。
生物学上，PHB 信号通过 LUA 中继传递到命令中间神经元（AVD/PVC），
PHA 信号通过 AVF 汇聚到 AVB 前进命令。添加 AVF 和 LUA 闭合了
Step 81 PHB/PHA 的下游回路。

## 生物学基础

### AVF (AVFL/AVFR)
- **Emmons 2024 PLOS Biology**: Community 9（腹索整合器）
  - "AVF collects input... directs output to... AVB"
- **功能**: 第二前进命令通路（并行于 PVC→AVB）
  - PHA→AVF: 尾部食物/信息素→前进驱动
  - PVC→AVF: 前进命令中继
  - AVF→AVB: 第二前进命令输出
- **乙酰胆碱能**
- **Cook 2019**: AVF↔PVQ gap junction（PVQ 不在模型中，跳过）

### LUA (LUAL/LUAR)
- **White 1986, Cook 2019**: 尾部中间神经元，preanal ganglion
- **功能**: 尾部感觉中继
  - PHB→LUA: 尾部排斥物中继
  - PLM→LUA: 后部触觉中继
  - LUA→AVD: 反转命令（尾部感觉→反转）
  - LUA→PVC: 前进命令（尾部感觉→逃逸）
- **谷氨酸能**
- **间隙连接**: LUA↔PHB（局部尾部回路）

## 实现细节

### 1. 神经元定义

新增 4 个神经元:
- **AVFL/AVFR**: 乙酰胆碱能中间神经元（第二前进命令）
- **LUAL/LUAR**: 谷氨酸能中间神经元（尾部感觉中继）

### 2. LUA 突触连接

| 连接 | 类型 | 权重 | 功能 |
|------|------|------|------|
| PHB → LUA | 兴奋性 | 2 | 尾部排斥物中继 |
| PLM → LUA | 兴奋性 | 2 | 后部触觉中继 |
| LUA → AVD | 兴奋性 | 2 | 反转命令 |
| LUA → PVC | 兴奋性 | 1 | 前进逃逸命令 |

### 3. AVF 突触连接

| 连接 | 类型 | 权重 | 功能 |
|------|------|------|------|
| PHA → AVF | 兴奋性 | 2 | 尾部食物→前进 |
| PVC → AVF | 兴奋性 | 2 | 前进命令中继 |
| AVF → AVB | 兴奋性 | 3 | 第二前进命令输出 |

### 4. 间隙连接

| 连接 | 权重 | 功能 |
|------|------|------|
| LUA L↔R | 2 | 双侧耦合 |
| LUA↔PHB | 1 | 尾部局部回路 |
| AVF L↔R | 2 | 双侧耦合 |

### 5. 闭合的回路

```
PHB(尾部排斥物) → LUA → AVD → AVA → 反转  (尾部感觉→反转通路完整)
PHB(尾部排斥物) → LUA → PVC → AVB → 前进  (尾部感觉→逃逸通路完整)
PHA(尾部食物)   → AVF → AVB → 前进          (第二前进命令)
PVC → AVF → AVB                              (前进命令增强)
PLM(后部触觉)   → LUA → AVD/PVC             (触觉→命令中继完整)
```

## 验证结果 (3 seeds, 300s, no-toxin)

| Seed | CI | near_food | reversal_rate | omega/rev | X disp |
|------|-----|-----------|---------------|-----------|--------|
| 42 | 0.738 | 30% | 0.16/s | 0.70 | +10.2mm |
| 7 | 0.286 | 60% | 0.16/s | 0.75 | +3.8mm |
| 99 | 0.632 | 30% | 0.16/s | 0.74 | +7.1mm |

- 所有种子 X displacement 正值（FOOD wins）✅
- CI 正值 (0.29-0.74) ✅
- omega/reversal 比例健康 (0.70-0.75) ✅

## 修改文件

| 文件 | 修改内容 |
|------|---------|
| `src/connectome/connectome_builder.cpp` | 添加 AVF/LUA 神经元定义 + 突触/间隙连接 |
| `src/simulation/regression_test.cpp` | 基线更新: 182/381/111 |

## Regtest

20/20 PASS，神经元 182(+4)，突触 381(+14)，间隙连接 111(+4)

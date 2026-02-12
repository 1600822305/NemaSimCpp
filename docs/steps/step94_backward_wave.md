# Step 94: 后退波传播修复 — A-class 本体感觉方向反转

**Date**: 2025-02-13
**Status**: Complete

## 动机

后退运动波应该从尾部向头部传播（与前进波相反），但之前 A-class (DA/VA) 的
本体感觉映射方向与 B-class 相同（感知前方曲率），导致后退波也从头→尾传播。

## 生物学基础

- **Kawano 2011 JNeurosci**: 后退波从尾部起始，向头部传播
- **Wen 2012 Neuron**: A-class 使用与 B-class 相同的 MEC 拉伸受体机制
- **Gao 2018 eLife**: A-class 本体感觉耦合驱动后退运动
- **机制**: B-class 感知前方(anterior)邻居 → HEAD→TAIL 波
         A-class 感知后方(posterior)邻居 → TAIL→HEAD 波

## 实现细节

### 修改 1: A-class 本体感觉方向反转 (simulation_engine.cpp)
- DA01-09: 每个 DA 感知其**后方邻居**的领地曲率
  - DA01 感知 seg 10 (DA02 领地 8-12) 而非之前的 seg 0
  - DA09 感知 seg 44 (尾部拉伸，启动波)
- VA01-12: 同样感知后方邻居，12 个完整单元

### 修改 2: VB08-11 前向本体感觉补全
- Step 87 添加了 VB08-11 神经元但遗漏了本体感觉映射

### 修改 3: regtest 速度基线更新
- Speed baseline: 0.30 → 0.20 mm/s (backward proprioception 增强后减速)

## 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `src/simulation/simulation_engine.cpp` | A-class 方向反转 + VB08-11 补全 |
| `src/simulation/regression_test.cpp` | 速度基线更新 |

## 验证

- regtest: 20/20 PASS
- diag 300s: Wave quality GOOD, 波传播到尾部
- proprio_mappings: 25 (B-class) + 25 (A-class) = 50 个映射

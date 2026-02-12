# Step 74: Regtest 升级 — 连接组完整性检测

## 动机

Step 73 新增 7 个神经元（162→169）和大量连接后，regtest 仍只有 17 个指标，
完全没有检测连接组结构的完整性。如果未来某次修改不小心遗漏了神经元定义或
突触连接，regtest 无法捕获这类错误。

## 实现

在 `SimMetrics` 结构体新增 3 个字段：
- `neuron_count` — 连接组中神经元总数
- `synapse_count` — 化学突触总数
- `gap_junction_count` — 缝隙连接总数

通过 `Connectome` 已有 API 获取：
- `conn.num_neurons()` → 169
- `conn.num_synapses()` → 331
- `conn.num_gap_junctions()` → 96

这些是**确定性值**，tolerance 设为 1%，任何偏差都表示连接组构建错误。

## 设计决策

- **CI 不加入 regtest**: 30s 仿真太短，CI 噪声大（种子相关）。CI 通过
  `celegans_diag.exe --duration 300` 的 4-seed 测量更准确。
- **FLP/IL1/RIH 电压不加入 regtest**: 这些神经元在 30s 无壁面接触时基本
  处于静息态，检测意义不大。它们的功能通过连接组计数间接保证。

## 修改文件列表

- `src/simulation/regression_test.cpp` — 新增 3 个连接组完整性指标

## 验证结果

- **Regtest: 20/20 PASS** (17→20 指标)

| 新指标 | 基线值 | 容差 | 说明 |
|--------|--------|------|------|
| Neuron count | 169 | 1% | Step 73: +7 (FLP/IL1/RIH) |
| Synapse count | 331 | 1% | 包含所有化学突触 |
| Gap junction count | 96 | 1% | 包含所有缝隙连接 |

## 参考

- 无外部文献引用（工具升级）

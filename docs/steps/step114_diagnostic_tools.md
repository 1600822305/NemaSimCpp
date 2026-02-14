# Step 114: 模块化诊断工具套件 — 9 个独立分析工具

> 日期: 2026-02-14

---

## 动机

`diag_main.cpp` 承担了过多职责（神经元监测、行为分析、健康检查、回路探测等），已成为难以维护的巨型文件。将其功能提取为独立的、可组合的诊断工具，每个工具专注一个分析维度。

## 实现

### 新建文件

| 文件 | 工具名 | 用途 |
|------|--------|------|
| `src/diagnostics/health_check_main.cpp` | health_check | 快速健康扫描 |
| `src/diagnostics/neuron_monitor_main.cpp` | neuron_monitor | 神经元数据追踪 |
| `src/diagnostics/circuit_probe_main.cpp` | circuit_probe | 回路信号分析 |
| `src/diagnostics/behavior_analyzer_main.cpp` | behavior_analyzer | 科研级行为分析 |
| `src/diagnostics/event_logger_main.cpp` | event_logger | 离散事件记录 |
| `src/diagnostics/causal_analyzer_main.cpp` | causal_analyzer | 因果分析 (TE+GC+干预) |
| `src/diagnostics/emergence_detector_main.cpp` | emergence_detector | 涌现检测 (Φ+MSE+亚稳态) |
| `src/diagnostics/perf_profiler_main.cpp` | perf_profiler | 性能剖析 |
| `src/diagnostics/param_sweep_main.cpp` | param_sweep | 参数扫描 |

### 修改文件

- `CMakeLists.txt` — 添加 9 个 `add_executable` + `target_link_libraries`

## 各工具功能

### 1. health_check
- 神经元死亡率、突触连接完整性、基本行为指标
- 输出: PASS/WARN/FAIL 状态

### 2. neuron_monitor
- 指定神经元的电压/释放率时间序列
- 统计: 均值、标准差、活跃度

### 3. circuit_probe
- 信号在特定回路中的传播追踪
- 突触电流、间隙连接电流分解

### 4. behavior_analyzer
- 互斥状态机 (Forward/Reverse/Omega/Pause)
- Bout 统计 (时长均值±标准差)
- 转角分析、头部曲率频率

### 5. event_logger
- 边沿检测所有状态变化 (反转、DMP、泵、睡眠、5-HT 等)
- 5 个事件类别: BEHAVIOR, STATE, MOTOR, ENVIRONMENT, NEUROMOD
- 时间线 + 摘要 + CSV 导出
- 测试: 120s 仿真采集 95 个事件

### 6. causal_analyzer
- **Transfer Entropy**: 信息论因果, 4-bin 离散化, lag=2
- **Granger Causality**: 简化 AR 模型 F-ratio
- **干预因果**: ablation 反事实对比 (wild-type vs ablated)
- 连接组验证: TE 检测到的因果边 vs 已知突触
- 测试: ASER→AIBL (chem exc) 正确识别

### 7. emergence_detector
- **Phi (IIT)**: 协方差矩阵 + Cholesky 行列式 + 最小信息分割
- **多尺度熵 (MSE)**: Costa 2002, 粗粒化 + 样本熵
- **亚稳态**: Kuramoto R(t) 标准差 (Shanahan 2010)
- **涌现事件标记**: SYNC_BURST, PHI_SPIKE, METASTABLE_TRANSITION
- 测试: Emergence Score 4/4 (Phi=0.15, Metastability=0.09)

### 8. perf_profiler
- 逐步 chrono 计时 (mean/P50/P95/P99/max)
- 瓶颈排名: 突触 37.4%, 神经元 28.7%, 体物理 10.4%
- Windows PSAPI 内存追踪
- 可扩展性预测表
- 测试: 108 us/step, 9236 steps/sec, 4.6x 实时

### 9. param_sweep
- 参数范围: `start:end:step` 或 `v1,v2,v3`
- 9 个可调参数: synapse_scale, speed_scale, sensory_gain 等
- 灵敏度分析: 线性回归 + R² + 归一化偏导数
- ASCII 图表 + CSV 导出
- 测试: synapse_scale 扫描 — speed R²=0.91 STRONG, 最优 CI 在 1.5

## 验证

- 9 个工具全部编译通过 (MSVC Release)
- 各工具独立运行测试通过
- 无回归: 不修改仿真核心代码

# Step 99: 多种子并行运行能力

**Date**: 2025-02-13
**Status**: Complete

## 动机

之前测试多种子需要手动用 shell 脚本并行启动多个进程，不方便且输出混乱。
直接在 diag 工具内部实现多种子并行，自动聚合统计结果。

## 实现

### CLI 参数
- `--seeds N`: 运行 N 个种子（默认 4，与 --fitness 或独立使用）
- `--jobs M` / `-j M`: 并行线程数（默认 min(8, hardware_concurrency)，上限 8）

### 并行架构
- 使用 `std::async(std::launch::async, ...)` + `std::future` 实现真并行
- 批次调度：每次启动 min(jobs, remaining) 个线程，等待完成后启动下一批
- `std::mutex` 保护结果写入和进度输出
- `std::chrono` 计时报告总耗时

### 两种模式

#### 1. `--fitness --seeds N -j M`
- 每个线程运行 1 个 seed × 3 scenarios (notox/toxic/nofood)
- 原有串行循环替换为并行批次

#### 2. `--seeds N -j M` (无 --fitness)
- 每个线程运行 1 个 seed 的 run_eval()
- 输出聚合统计：mean ± stddev
- 逐 seed 明细表格

### 输出格式
```
========================================
  MULTI-SEED RESULTS (16 seeds, 118.8s wall time)
========================================
  CI:           0.288 ± 0.339
  Speed:        0.174 ± 0.008 mm/s
  Reversals:    39.2 ± 1.2
  ...
  Per-seed:
  seed    CI     speed  rev  omega  near%
   123  -0.130  0.169   40     18   24.6
   ...
```

## 验证结果

| 配置 | 耗时 | 加速比 |
|------|------|--------|
| 8 seeds, 8 jobs, 300s | 74.2s | ~8x vs 串行 |
| 16 seeds, 16→8 jobs, 300s | 118.8s | ~6.4x |

## 修改文件列表

| 文件 | 修改内容 |
|------|----------|
| `src/simulation/diag_main.cpp` | --jobs/-j CLI, 并行 fitness 模式, 多种子聚合模式 |

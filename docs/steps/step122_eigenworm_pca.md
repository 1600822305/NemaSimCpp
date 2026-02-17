# Step 122: Eigenworm PCA 分析工具

> 日期: 2026-02-17
> 状态: ✅ 完成

## 目标

实现 Eigenworm 主成分分析工具，验证仿真虫的体姿是否具有与真实 C. elegans 一致的低维表示。

REF: Stephens et al. 2008 PLoS Comput Biol — "Dimensionality and dynamics in the behavior of C. elegans"

## 决策

### PCA 方法选择

Stephens 2008 使用切线角 θ(s) 的协方差矩阵 PCA，发现前 4 个 eigenworms 解释 >95% 方差。

本项目使用 **曲率谱的相关矩阵 PCA** (correlation-based PCA)：
- **曲率 κ(s)** 而非切线角：曲率是航向不变的 (heading-invariant)，避免航向漂移污染
- **相关矩阵** 而非协方差矩阵：标准化每个体段的贡献，防止头部转向曲率 (SMD/RIV) 主导方差
- **前进帧过滤**: 仅分析稳态前进运动帧 (排除反转和反转后 0.5s 过渡期)

### 频率分析

- 4 个体段 (seg 8/16/24/36) 分别测量自相关频率和过零频率
- 取 4 段的中位频率作为最终估计（robust to outliers）
- 步态分类: <0.3 Hz 静止, 0.3-1.0 Hz 爬行, 1.0-1.4 Hz 过渡, >1.4 Hz 游泳

### Jacobi 特征分解

- 48×48 对称矩阵的 Jacobi 旋转法
- 无外部依赖 (纯 C++ 实现)
- 收敛条件: 非对角元素平方和 < 1e-20
- 典型迭代次数: 10-13 次

## 实现

### 新文件
- `src/diagnostics/eigenworm_analyzer_main.cpp` — 完整诊断工具

### 修改文件
- `CMakeLists.txt` — 添加 `eigenworm_analyzer` 构建目标

### CLI 参数
```
--duration N    仿真时长 (默认 60s)
--seed N        随机种子 (默认 42)
--viscosity V   介质黏度 (默认 1.0, 配合 Step 123)
--verbose       显示 eigenworm 形态预览
--export FILE   导出 eigenworms 到 CSV
```

## 验证结果

### 琼脂 (v=1.0, seed=42, 30s)
```
Top-4 Eigenworms: 78.3% (相关矩阵)
均匀基准: 8.3%
集中度: 9.4x → 高度低维
中位频率: 0.35 Hz → 爬行步态 ✓
```

### 水 (v=0.01, seed=42, 30s)
```
Top-4 Eigenworms: 86.5% (相关矩阵)
均匀基准: 8.3%
集中度: 10.4x → 高度低维
中位频率: 1.01 Hz → 过渡步态
```

### 评估标准
- 相关矩阵 PCA 的均匀基准 = 100%/D × 4 = 8.3%
- 集中度 >5x 表示高度低维 ✓
- 集中度 >3x 表示低维 ✓
- 集中度 <2x 表示接近均匀（无结构）

## 关键指标
- PCA 维度: D=48 (全部体段曲率)
- Jacobi 收敛: 10-13 次迭代
- 数据源: forward-only (排除反转)
- 采样间隔: 10ms, 5s warmup

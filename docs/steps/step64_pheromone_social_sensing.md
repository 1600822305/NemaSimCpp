# Step 64: 信息素社会感知 (Pheromone Social Sensing)

## 动机
ADL 感觉神经元 (Step 61 新增) 是 ascaroside 信息素的主要传感器，
但尚未连接到信息素化学场。文献 (Srinivasan 2008, Jang 2012) 证明
ascaroside 信息素调控聚集、回避和交配行为。

## 生物学基础

### Jang 2012 — ADL 是 ascr#3 主要传感器
- ADL 钙成像显示对 ascr#3/C9 的持续响应
- 回路: ADL → AVA (反转回避)
- 雌雄同体中: ascr#3 引发回避行为
- 雄性中: ascr#3 引发吸引行为 (性二态)

### Srinivasan 2008 — ascaroside 作为社会信号
- ascaroside 是水溶性小分子信息素
- 控制: dauer 进入/退出、聚集行为、交配竞争
- 浓度依赖: 低浓度吸引, 高浓度排斥

## 实现细节

### 1. 信息素化学场 (Environment)
- 新增 `pheromone_field_` (ChemicalField)
- σ²=36mm² (σ=6mm): 水溶性，中等扩散范围
- `set_pheromone_source(pos, intensity)`: 模拟附近同种个体
- `sample_pheromone(pos)`: 采样当前位置浓度

### 2. ADL 信息素转导 (simulation_engine.cpp)
- TONIC 型响应: 持续驱动 (Jang 2012 钙成像)
- 增益: 40 pA × pheromone/(pheromone+0.2)
- 半饱和浓度: 0.2 (低阈值感知)
- **叠加**在现有 repellent ON 转导之上 (ADL 多模态)

### 3. ADL→AVA 回避回路
- ADL→AVA 连接已存在 (Step 61 connectome)
- 信息素激活 ADL → AVA 驱动 → 反转 → 远离信息素源
- 无需新增突触，纯涌现行为

### 4. CLI 参数
- `--pheromone`: 启用信息素源 (默认位置 15,25)
- `--pheromone_x/y <f>`: 信息素源位置
- `--pheromone_intensity <f>`: 信息素强度 (默认 0.8)

## 验证结果

### Regtest: 17/17 pass ✅

### 信息素回避实验 (no_toxin, seed=42, 300s)
| 条件 | CI | near_food | 说明 |
|------|-----|-----------|------|
| 无信息素 | 0.113 | 41.7% | 基线 |
| 信息素@食物(35,25) | 0.152 | **36.8%** | 信息素排斥减少食物停留 |
| 信息素@对面(15,25) | **0.382** | 31.0% | 避开信息素→推向食物 |

**关键发现**:
- 信息素@食物: near_food 从 41.7%→36.8% — ADL 回避与食物吸引竞争 ✅
- 信息素@对面: CI 从 0.11→0.38 — 线虫被推离信息素方向 ✅
- ADL→AVA 回避行为纯涌现 (无需新突触) ✅

## 修改文件列表
| 文件 | 修改内容 |
|------|---------|
| `src/environment/environment.h` | +pheromone_field_ + API |
| `src/environment/environment.cpp` | +初始化/step/sample/set_pheromone_source |
| `src/simulation/simulation_engine.cpp` | +ADL 信息素转导 (additive on repellent) |
| `src/simulation/diag_main.cpp` | +--pheromone CLI 参数 |

## 参考文献
- Jang 2012 — ADL ascr#3 回路和行为
- Srinivasan 2008 — ascaroside 社会信号
- Troemel 1997 — ADL 多模态化学感觉

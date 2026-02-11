# Step 55: Light Avoidance — ASJ/LITE-1 Photophobia Circuit

## 动机
C. elegans 虽然没有眼睛，但能通过 LITE-1 光受体蛋白感知紫外/蓝光并产生回避行为。
这是一种重要的保护性行为——紫外线对无色素保护的线虫具有致命性。
Ward 2008 (Nat Neurosci) 首次鉴定 ASJ 为光感受神经元；Liu 2010 进一步发现
ASJ、ASK、AWB、ASH 四种感觉神经元均表达 LITE-1，协同介导光回避。

本步骤实现完整的光回避闭环回路：
环境光场 → LITE-1 光转导 → ASJ/ASK/AWB/ASH 感觉输入 → 中间神经元 → 运动输出

## 生物学基础

### LITE-1 信号通路
```
UV/blue photon → LITE-1 (味觉受体同源物)
  → Gα (GOA-1/GPA-3) → guanylate cyclase → cGMP↑
  → TAX-2/TAX-4 CNG channel → depolarization
```
- **LITE-1**: Edwards 2008 遗传筛选鉴定，是 Gr 家族 GPCR
- **TAX-2/TAX-4**: 环核苷酸门控通道，也用于化学感觉
- REF: Edwards 2008 PLoS Biol, Ward 2008 Nat Neurosci

### 光感觉神经元
| 神经元 | 角色 | LITE-1 | 增益(pA) | 神经递质 |
|--------|------|--------|---------|---------|
| ASJ L/R | 主要光感受器 | ✓ | 60 | 谷氨酸 |
| ASK L/R | 次要光感受器 | ✓ | 30 | 谷氨酸 |
| AWB L/R | 三级 + 挥发性排斥 | ✓ | 20 | 乙酰胆碱 |
| ASH L/R | 四级 + 伤害感受 | ✓ | 15 | 谷氨酸 |

### 下游回路 (Cook 2019 connectome)
```
ASJ → AIA(2) — 抑制路径：AIA ⊣ AIB → 抑制反转
ASJ → AIB(1) — 兴奋路径：AIB → AVA → 促进反转 (dominant)
ASJ → RIA(2) — 转向路径：RIA → SMD → 头部运动偏转
ASK → AIA(2), AIB(1), AIY(1)
AWB → AIZ(1) — 已有 AWB→AIB 通路
```
净效果：强光 → ASJ/ASK 激活 → AIB 优势 → 反转 + omega转向 → 远离光源

### 间隙连接
- ASJ ↔ ASK: 同侧耦合 (2 sections) — 共享感觉环境
- ASJ L↔R, ASK L↔R: 双侧对称耦合 (1 section)

## 实现细节

### 1. 环境光场 (`environment.h/cpp`)
- `sample_light(pos)`: 高斯衰减光场，σ=8mm (σ²=64mm²)
- `set_light_source(pos, intensity)`: 配置光源位置和强度
- `has_light()`: 快速检查是否有光源

### 2. 新增神经元 (`connectome_builder.cpp`)
- ASJL/ASJR: SENSORY, GLUTAMATE — 主要光感受器
- ASKL/ASKR: SENSORY, GLUTAMATE — 次要光感受器
- 神经元总数: 134 → 138

### 3. 突触连接 (`build_phototaxis()`)
化学突触 (14条):
- ASJ→AIA(2×2), ASJ→AIB(1×2), ASJ→RIA(2×2)
- ASK→AIA(2×2), ASK→AIB(1×2), ASK→AIY(1×2)
- AWB→AIZ(1×2)

间隙连接 (6条):
- ASJ↔ASK(2×2), ASJ L↔R(1), ASK L↔R(1)

### 4. 光感觉转导 (`simulation_engine.cpp`)
- ASJ/ASK 从 `other_sensory_ids_` 排除 → 由光场驱动
- `update_sensory_input()` 新增光感觉块：
  - 采样头部位置光强度
  - ASJ: I = 1.0 + 60.0 × light (主要)
  - ASK: I = 1.0 + 30.0 × light (次要)
  - AWB: I += 20.0 × light (叠加在病原体驱动上)
  - ASH: I += 15.0 × light (叠加在伤害感觉驱动上)

### 5. CLI 支持 (`diag_main.cpp`)
- `--light`: 启用光源 (默认位置 25,25)
- `--light_x/y <f>`: 光源位置
- `--light_intensity <f>`: 光强度 0-1

## 修改文件列表
| 文件 | 修改内容 |
|------|---------|
| `src/environment/environment.h` | 添加 sample_light, set_light_source, has_light, light_pos_, light_intensity_ |
| `src/environment/environment.cpp` | 实现 sample_light (高斯衰减), set_light_source |
| `src/connectome/connectome_builder.cpp` | 添加 ASJL/R, ASKL/R 神经元; build_phototaxis() 函数 |
| `src/simulation/simulation_engine.cpp` | ASJ/ASK 排除; 光感觉转导逻辑 (LITE-1→CNG) |
| `src/simulation/diag_main.cpp` | --light/--light_x/y/--light_intensity CLI 参数 |

## 验证结果
- 编译: 零错误
- Regtest: 17 pass, 0 FAIL (无光场时行为不变)
- 光回避验证 (seed=42, 300s, --light @food):
  - 无光: CI=0.457, near_food=39.0%, reversals=34
  - 有光: CI=0.561, near_food=35.7%, reversals=28
  - 光与食物重叠时 near_food 降低，光产生排斥效果但食物吸引力仍占优

## 参考文献
1. Ward A et al. (2008) Nat Neurosci — ASJ identified as photoreceptor
2. Edwards SL et al. (2008) PLoS Biol — LITE-1 genetic screen
3. Liu J et al. (2010) — Multi-neuron light avoidance (ASJ+ASK+AWB+ASH)
4. Cook SJ et al. (2019) Nature — C. elegans connectome (synapse counts)
5. eLife 2025 — LITE-1 as gustatory chemoreceptor (multi-modal)
6. Bargmann CI & Horvitz HR (1991) — ASJ/ASK amphid neuron function

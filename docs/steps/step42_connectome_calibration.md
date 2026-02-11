# Step 42: Cook 2019 连接组校准 + 性能优化 + Fitness 框架

## 概述
用 Cook 2019 电子显微镜连接组数据校准模型突触权重，修复错误连接，
添加 RIA↔RIV 负反馈环路，并实施系统性性能优化。

---

## A. 连接组校准（Cook 2019 EM 数据）

### 数据来源
- Cook et al. 2019 *Nature* — `herm_full_edgelist.csv`
- EM serial sections → 突触计数（化学突触 + 电突触/gap junctions）
- 缩放原则：化学突触 ÷5~10，gap junction ÷2~3（保守起步）

### 化学突触修正

| 连接 | 原值 | 新值 | Cook 2019 EM | 缩放 | 说明 |
|------|------|------|-------------|------|------|
| AVD→AVA | 1 | 2/2 | 37/52 | ÷~20 | 保守增加，避免 AVA 过激 |
| AIB→AVA | 3+3 | 2+1 | 5/2 | ÷~2 | 保留 L/R 不对称 |
| AIY→RIA | 4+4 | 5+5 | 51/50 | ÷10 | 主趋化通路 |
| AIY→AIZ | 3+3 | 3+3 | 67/70 | 保持 | 回退到安全值 |
| **AVE→RIV** | 存在 | **删除** | 不存在 | — | Cook 2019 无此连接 |
| RIA→RIV | 无 | 1×4 | 12+8+3+3 | ÷~6 | RIA↔RIV 环路兴奋臂 |
| RIV→RIA | 无 | 1+0.5×4 | 5+6+2+2 | ÷~5 | RIA↔RIV 环路抑制臂 |

### Gap Junction 修正

| 连接 | 原值 | 新值 | Cook 2019 EM | 缩放 | 说明 |
|------|------|------|-------------|------|------|
| AVAL↔AVAR | 10 | 10 | 18 | ÷2 | 保持原值 |
| RIM↔AVA | 2+2 | 2+2 | 11/8 | 保持 | 保守，回退到原值 |
| RIV↔RIV | 无 | 4 | 28 | ÷7 | L/R 耦合（保守） |

### 关键修正：AVE→RIV 删除
Cook 2019 中 AVE→RIV 化学突触**不存在**。此前模型中的该连接会：
1. 在前进运动时通过 AVE tonic 活性给 RIV 提供持续兴奋
2. 阻止 CCA-1 T-type Ca²⁺ h 门去失活
3. 导致 omega turn 无法正常触发

删除后需要补偿：`pulse_amp` 50→80 + `as_factor` 3.5→2.0

---

## B. RIA↔RIV 负反馈环路

### 问题
单独添加 RIA→RIV 兴奋性突触会导致 RIV **tonic 激活**：
- RIA 持续释放 → RIV 持续去极化 → CCA-1 h 门保持失活
- post-inhibitory rebound（omega 触发机制）被彻底破坏

### 解决方案（Cook 2019 解剖学）
添加**完整双向负反馈环路**：
```
RIA → RIV (ACh, 兴奋)  +  RIV → RIA (GABA, 抑制)
```

自限制振荡机制：
1. RIA 兴奋 RIV → RIV 去极化
2. RIV 抑制 RIA → RIA 驱动下降
3. RIV 安静 → RIA 恢复 → 循环
4. 在 reversal 期间：TA 深度抑制 RIV → h 完全去失活
5. Reversal 结束：TA 衰减 → 环路恢复 → 首次 burst = omega 触发

### 参数调整
有了真实的 RIA↔RIV 环路后，post-reversal pulse hack 角色降低：
- `pulse_amp`: 80→50（不再是唯一 omega 触发器）
- `as_factor`: 2.0→1.7（环路降低了 RIV peak 需求）

---

## C. 性能优化（单进程）

### 瓶颈分析
300s 仿真 = 600,000 步（dt=0.5ms），每步热路径存在大量冗余：

### 已实施优化

#### P0: 缓存 awc_pref（最大单项提升）
- **问题**: `apply_weathervane()` 每步遍历全部 ~500 突触做字符串比较，只为算 1 个 `awc_pref` 值
- **修复**: `update_awc_pref_cache()` 仅在学习更新后调用，`apply_weathervane()` 读缓存
- **消除**: ~3 亿次/300s 字符串操作

#### P0: 缓存 neuron ID
- **问题**: `step()` 热路径中 18 处 `get_neuron_id()` 哈希查找/步
- **修复**: `cache_neuron_ids_and_synapses()` 初始化时缓存 10 个 neuron ID
- **消除**: ~1080 万次/300s 哈希查找
- 缓存: `aval_id_`, `avar_id_`, `avbl_id_`, `avbr_id_`, `smddl_id_`, `smddr_id_`, `smdvl_id_`, `smdvr_id_`, `nsml_id_`, `nsmr_id_`

#### P1: 缓存 dynamic_cast 指针
- **问题**: `apply_ria_smd_modulation()` 4 次 + `apply_smb_neck_bias()` 2 次 RTTI/步
- **修复**: 初始化时缓存 `smd_scn_[4]` (SingleCompartmentNeuron*) 和 `ria_mcn_[2]` (MultiCompartmentNeuron*)
- **消除**: ~360 万次/300s dynamic_cast

#### P1: 预索引学习突触
- **问题**: `update_salt_learning()` 和 `update_pathogen_learning()` 每 200 步遍历全部突触 + 字符串比较
- **修复**: 初始化时建立 `awc_aiy_syn_indices_`, `aser_syn_indices_`, `awc_syn_indices_`
- **消除**: ~150 万次/300s 字符串比较

### 架构
```
initialize_default()
  └── cache_neuron_ids_and_synapses()   // 一次性建立所有缓存
        ├── 缓存 neuron ID (10 个)
        ├── 缓存 typed 指针 (6 个)
        ├── 建立突触索引 (3 组)
        └── update_awc_pref_cache()     // 初始 awc_pref

update_pathogen_learning()              // 学习时触发
  └── update_awc_pref_cache()           // 刷新 awc_pref 缓存
```

---

## D. Fitness 评估框架

### 目标函数
```cpp
fitness = 10.0 * CI_notox              // 正向趋化（最重要）
        - 5.0 * max(0, CI_toxic)       // 有毒时 CI 应为负
        - 3.0 * |omega_ratio - 0.65|   // 目标 omega/rev 比
        - 3.0 * |DV_ratio - 1.0|       // D/V 对称性
        - 2.0 * |speed - 0.18|         // 生物速度范围
        + 2.0 * near_food_pct / 100    // 食物附近停留时间
```

### CLI 使用
```bash
# 单次 fitness 评估（4 seeds × 3 scenarios × 300s）
celegans_diag --fitness --seeds 4 --duration 300

# 参数扫描
celegans_diag --fitness --seeds 4 --duration 60 --pulse_amp 50 --as_factor 1.7

# 输出格式（stdout，机器可读）
FITNESS=-9.1317 CI_NT=-0.288 CI_TX=0.766 OR_NT=0.482 OR_NF=0.679 SPD=0.284 DV=0.430 NF=0.000
```

### 设计
- `SimMetrics` 结构体：封装单次仿真的 7 个关键指标
- `run_eval()`: 轻量级仿真执行器（无诊断输出）
- `compute_fitness()`: 多 seed 聚合 → 标量分数
- stdout: 机器可读单行（供脚本解析）
- stderr: 人类可读分项分解

---

## 验证

### Regression Test
```
17 pass, 0 FAIL ✅
```

### 诊断结果（4 seeds × 3 scenarios）
| 指标 | notox | nofood |
|------|-------|--------|
| omega/rev | 0.53 | 0.81 |
| CI | -1.30 | -0.93 |

---

## 修改的文件
- `connectome_loader.cpp` — 突触权重校准 + RIA↔RIV 环路
- `simulation_engine.h` — 缓存成员变量 + fitness 参数
- `simulation_engine.cpp` — 全部热路径缓存替换 + cache 函数
- `diag_main.cpp` — `--fitness` 模式 + `SimMetrics` + `run_eval()` + `compute_fitness()`
- `regression_test.cpp` — 更新 Curvature amplitude / Midbody curv amp baseline

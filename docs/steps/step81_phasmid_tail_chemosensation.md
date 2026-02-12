# Step 81: Phasmid Tail Chemosensation (PHB/PHA)

## 动机

此前所有化学感觉输入均来自头部（amphid）神经元。C. elegans 尾部有一对 phasmid 感觉器官，
包含 PHB 和 PHA 两对神经元。Hilliard 2002 发现 PHB/PHA **负调制**头部排斥物触发的反转，
实现定向逃逸：排斥物在前→反转，排斥物在后→继续前进。

## 生物学基础

- **Hilliard 2002 Curr Biol**: PHB/PHA 为化学感觉细胞，负调制反转
  - 头部 ASH 检测排斥物 → 激活反转
  - 尾部 PHB 检测同一排斥物 → **抑制**反转
  - 头尾拮抗整合 → 定向逃逸行为
- **Zou 2017 Sci Rep**: PHB/PHA 为多模态感觉神经元
  - 化学刺激: SDS, IAA, 碱性溶液
  - 机械刺激: 20μm 位移（harsh touch）
  - 高渗刺激: 2M glycerol
  - 铜离子: 抑制性响应（calcium 下降）
  - OSM-9 (TRPV) 和 TAX-4 (CNG) 必需
- **Cook 2019 Nature**: 完整连接组数据
  - PHB→AVA: ~4 EM sections（抑制性，GLC-3 Cl⁻通道）
  - PHB→PVC: ~2 EM sections（兴奋性，促进前进）
  - PHA→AVD: ~1 EM section
  - PHB↔PHA: 间隙连接

## 实现细节

### 1. 神经元定义 (connectome_builder.cpp)

新增 4 个神经元:
- **PHBL/PHBR**: 谷氨酸能感觉神经元（尾部排斥物）
- **PHAL/PHAR**: 谷氨酸能感觉神经元（尾部食物/信息素）

### 2. 突触连接

| 连接 | 类型 | 权重 | 功能 |
|------|------|------|------|
| PHB ⊣ AVA | 抑制性 | 3 | 抑制反转（排斥物在尾部时）|
| PHB → PVC | 兴奋性 | 2 | 促进前进（逃逸）|
| PHB → AVD | 兴奋性 | 1 | 弱反转调制 |
| PHA → AVD | 兴奋性 | 1 | 弱反转调制 |
| PHA → AVH | 兴奋性 | 1 | 信息素→感觉桥 |

### 3. 间隙连接

| 连接 | 权重 | 功能 |
|------|------|------|
| PHB L↔R | 2 | 双侧耦合 |
| PHA L↔R | 2 | 双侧耦合 |
| PHB↔PHA | 1 | phasmid 内协调 |
| PHB↔AVH | 1 | 感觉桥连接 |

### 4. 尾部感觉转导 (apply_tail_chemosensation)

```cpp
// PHB: TONIC response to repellent at TAIL position
double rep_at_tail = environment_.sample_repellent(tail_pos);
double phb_drive = 40.0 * rep / (rep + 0.3) + 2.0;  // gain=40, baseline=2pA

// PHA: TONIC response to food at TAIL position
double food_at_tail = environment_.sample_food_density(tail_pos);
double pha_drive = 10.0 * food / (food + 0.5) + 1.0;  // gain=10, baseline=1pA
```

- PHB gain=40（弱于头部 ASH gain=80）— 尾部为次要伤害感知器
- PHB half_max=0.3（比 ASH 0.5 更灵敏）— 更低阈值
- PHA gain=10（弱）— 主要为神经内分泌功能

### 5. nids_ 前缀缓存

添加 "PHB" 和 "PHA" 到 `prefixes[]` 数组，确保 `nids("PHB")` 正确返回。

## 关键闭环回路

```
排斥物在前:  ASH(头) →(+) AVA → 反转 ✓
排斥物在后:  PHB(尾) →(-) AVA → 抑制反转 → 继续前进 ✓
食物在尾:    PHA(尾) →(+) AVH → 感觉桥 → 行为调制
```

头尾拮抗通过 AVA 整合: ASH 兴奋 vs PHB 抑制 → 定向逃逸涌现

## 验证结果 (3 seeds, 300s, no-toxin)

| Seed | CI | near_food | reversal_rate | omega/rev | X disp |
|------|-----|-----------|---------------|-----------|--------|
| 42 | 0.346 | 20% | 0.16/s | 0.67 | +3.5mm |
| 7 | -1.32 | 0% | 0.27/s | 0.79 | +7.4mm |
| 99 | 0.344 | 30% | 0.16/s | 0.78 | +5.9mm |
| 42(tox) | 0.646 | 50% | 0.14/s | 0.76 | +13.3mm |

- 习惯化保持: 5/5 → 0/5（所有种子）✅
- PHB/PHA 诊断: Section 32 显示尾部位置、排斥物/食物浓度、膜电位

## 修改文件

| 文件 | 修改内容 |
|------|---------|
| `src/connectome/connectome_builder.cpp` | 添加 PHB/PHA 神经元定义 + 突触/间隙连接 |
| `src/simulation/simulation_engine.h` | 添加 apply_tail_chemosensation() 声明 |
| `src/simulation/simulation_engine.cpp` | 实现 apply_tail_chemosensation() + PHB/PHA 排除 + nids_ 缓存 |
| `src/simulation/diag_main.cpp` | Section 32: 尾部化学感觉诊断 |
| `src/simulation/regression_test.cpp` | 基线更新: 175/347/104, SMDVL 30→50 |

## Regtest

20/20 PASS，神经元 175(+4)，突触 347(+10)，间隙连接 104(+6)

# Step 43: 病原体回避机制 + 趋化修复

## 概述
实现三条病原体回避神经通路，修复两个关键 bug（5-HT 浓度、weathervane 覆盖），
使模型具备完整的趋化→感染→回避→印记学习行为链。

---

## A. Bug 修复

### A1. 5-HT 浓度 bug
- **症状**: 虫子在食物上 5-HT=0.019，应为 0.7+
- **根因**: `chemo_sat_gain`（fed=0.15）错误应用于 NSM/CEP
  - NSM 检测物理食物接触，不应受饱足度抑制
  - 饱足度调控应在下游（ASE/AWC 趋化增益），不在 NSM 释放端
- **修复**: NSM/CEP 的 `gain_mod` 设为 1.0，仅对趋化感觉神经元应用 `chemo_sat_gain`
- **文件**: `simulation_engine.cpp` L601-607

### A2. Weathervane 趋化 bug
- **症状**: CI(notox)=-1.6，健康虫子远离食物
- **根因**: `apply_smb_neck_bias()` 每步用 RIA Ca2+ 信号（max ±0.5）**覆盖** `apply_weathervane()` 设置的梯度驱动 curvature_bias（max ±7.5）
- **修复**: SMB neck bias **叠加**到现有 curvature_bias，而非替换
  - 添加 `get_curvature_bias()` getter
  - `set_curvature_bias(get_curvature_bias() + curvature_offset)`
- **效果**: CI(notox) 从 -1.6 提升到 +0.39
- **文献验证**: 
  - Iino & Yoshida 2009: curving rate = 12.7 deg/s per mM/mm
  - Hums 2016 eLife: curving bias = 0.025-0.075 rad/mm
  - Matsumoto 2024 PNAS: SMB 是 klinotaxis 关键运动神经元
- **文件**: `simulation_engine.cpp` L942-949, `body_model.h` L38-39

---

## B. 病原体回避通路

### B1. ADF 5-HT → MOD-1 ⊣ AIY/AIZ（Step 43a）
- **机制**: ADF 感觉神经元释放 5-HT → MOD-1（5-HT 门控 Cl- 通道）→ 抑制 AIY/AIZ
- **实现**: 通过 neuromodulation 系统的 5-HT target，非直接突触
  - AIY: -2.5 pA × [5-HT]（之前 -5.0，移除 buggy ADF→AIY 兴奋突触后校准）
  - AIZ: -3.0 pA × [5-HT]
- **效果**: 5-HT 高时（on food）抑制趋近通路 → dwelling 行为
- **REF**: Harris 2009 J Neurosci, Liang 2006

### B2. AWB 排斥嗅觉回路（Step 43b）
- **机制**: AWB 检测病原体挥发物（如 1-undecene）→ AUA → AVA → 后退
- **连接类型**: AWB↔AUA 为**电突触**（gap junction），非化学突触
- **修复**: `add_syn` → `add_gj`（每侧 2 sections）
- **REF**: Filipowicz 2022 BMC Biology — "AWB electrically synapses onto AUA and RMG"
- **文件**: `connectome_loader.cpp` L565-569

### B3. RIM TA → SER-2 → AIY 印记学习（Step 43c）
- **机制**: 
  - 记忆形成（L1）: RIM + AIB 活跃
  - 记忆提取（成虫）: TA → SER-2（Gαi GPCR）→ 抑制 AIY → 趋近行为受抑
  - TA 桥接形成回路（RIM/AIB）和提取回路（AIY/RIA）
- **实现**: TA neuromodulator 新增 target
  - AIY L/R: SER-2, -10 pA（与 AVB LGC-55 -10 pA 一致）
- **REF**: Jin, Pokala & Bargmann 2016 Cell 164:632-643
         Bowitch 2018 G3 — SER-2 on AIY necessary for memory retrieval
- **文件**: `simulation_engine.cpp` L1779-1790

---

## C. Regtest 基线更新

趋化修复后曲率正常增大（主动转向的自然结果）：

| 指标 | 旧值 | 新值 | 原因 |
|------|------|------|------|
| SMDVL V swing | 45 mV | 75 mV | weathervane bias 电流生效 |
| Curvature amplitude | 0.05 /mm | 0.14 /mm | 主动梯度转向 |
| Midbody curv amp | 0.10 /mm | 0.20 /mm | 波传播增强 |

---

## D. 诊断结果（60s, seed=default）

```
CI = +0.131（notox，趋化方向正确）
5-HT = 0.005（远离食物，正确低值）
TA = 0.363（正常逃逸信号水平）
omega/reversal = 0.85
speed = 0.17 mm/s
Wave: GOOD
TA targets: 14（含新增 SER-2 × 2）
```

---

## 修改的文件
- `simulation_engine.cpp` — 5-HT bug 修复 + weathervane 叠加修复 + SER-2 target
- `body_model.h` — 添加 `get_curvature_bias()` getter
- `connectome_loader.cpp` — AWB↔AUA gap junction 修正
- `regression_test.cpp` — 基线更新（SMDVL/curv/midbody）

# Step 57: Ion Channel Expansion (8→14 Types)

## 动机
当前仿真使用 8 种离子通道，覆盖了 C. elegans 最核心的电导机制。
但 C. elegans 基因组编码 ~80 种 K⁺ 通道（含 49 个 TWK）、多种 Ca²⁺ 和阳离子通道。
补齐 6 种重要通道可以：
- 改善静息电位稳定性（IRK, TWK）
- 增强去极化后复极化（EGL-36, SLO-2）
- 支持感觉转导（OSM-9/TRPV）
- 实现 DMP 动作电位复极化（EXP-2）

## 新增通道

### 1. EGL-36 — Shaw/Kv3 延迟整流 K⁺ 通道
- **功能**: 非失活持续 K⁺ 电流，强去极化时复极化
- **激活**: V_half = -8 mV, k = 12 mV（比 SHL-1 更正）
- **动力学**: tau_m = 5-20 ms（电压依赖）
- **无失活**: 持续提供复极化电流
- **表达**: 感觉(0.5)、中间(0.4)、运动(0.6)、头部运动(0.3)
- REF: Johnstone 1997, Elkes 1997, Santi 2003 JBC

### 2. IRK — 内向整流 K⁺ 通道 (Kir)
- **功能**: 超极化时导通→稳定静息电位
- **整流**: Mg²⁺/多胺阻塞，V > E_K+10 时关闭
- **动力学**: 瞬时（无门控延迟）
- **表达**: 所有类型(0.15-0.25)
- REF: Döring 2002 Mol Biol Cell

### 3. TWK — 双孔域 K⁺ 通道 (K₂P)
- **功能**: 电压无关背景漏 K⁺，设定基线兴奋性
- **门控**: 无电压依赖，常开（可外部调制）
- **调制**: 温度(TWK-18)、pH、麻醉剂
- **表达**: 默认(0.08)、感觉(0.12)
- REF: Salkoff 2001 ("49 K⁺ channels in C. elegans"), Bhatt 2014

### 4. SLO-2 — Na⁺ 激活 K⁺ 通道
- **功能**: 持续去极化时 Na⁺ 内流→SLO-2 开放→防止过度兴奋
- **激活**: 双重门控：电压(V_half=+5mV) × Na⁺ 代理(V_half=-20mV)
- **动力学**: tau_m = 10 ms
- **对比 SLO-1**: Ca²⁺ 激活 vs Na⁺ 激活（互补机制）
- **表达**: 中间(0.6)、运动(0.8)、B类运动(0.8)
- REF: Yuan 2000 Neuron, Yuan 2003 JBC, Salkoff 2006

### 5. OSM-9 — TRPV 阳离子通道
- **功能**: 多模态感觉转导（渗压、伤害、嗅觉）
- **异聚体**: OSM-9/OCR-2 在 ASH、OLQ、AWA
- **门控**: 刺激依赖（非电压），半最大 stimulus=0.5
- **选择性**: 非选择性阳离子 (E_rev ≈ 0 mV)
- **动力学**: tau_m = 20 ms（感觉转导延迟）
- REF: Colbert 1997 J Neurosci, Tobin 2002 Neuron, Kahn-Kirby 2004 Cell

### 6. EXP-2 — 肠道 Kv K⁺ 通道
- **功能**: AVL/DVB 复合动作电位的快速复极化
- **激活**: V_half = -10 mV, k = 8 mV, tau = 2 ms（快）
- **部分失活**: h_inf = 0.4×Boltzmann + 0.6（持续分量）
- **特化**: 仅 AVL/DVB (g_max=2.5)
- REF: Davis 1999 Science, Jiang 2022 Nat Commun

## 通道分配表

| 神经元类型 | 原有通道 | 新增通道 (Step 57) |
|-----------|---------|-------------------|
| 默认 | EGL-19, SHL-1, NCA | +IRK(0.2), TWK(0.08) |
| 感觉 | EGL-19, SHL-1, KQT-3, NCA | +EGL-36(0.5), IRK(0.25), TWK(0.12) |
| 中间 | EGL-19, SHL-1, KQT-3, SLO-1, NCA | +EGL-36(0.4), SLO-2(0.6), IRK(0.2) |
| 运动 | EGL-19, UNC-2, SHL-1, KQT-3, NCA | +EGL-36(0.6), SLO-2(0.8), IRK(0.15) |
| B类运动 | 运动 + MEC | +EGL-36(0.6), SLO-2(0.8), IRK(0.15) |
| 头部运动 | EGL-19, CCA-1, SHL-1, KQT-3, SLO-1, NCA | +EGL-36(0.3) |
| AVL/DVB | 运动 | +EXP-2(2.5) |

## 电生理效果
- **ASEL**: -36.5 → -39.7 mV（IRK+TWK+EGL-36 增加 K⁺ 电导→更负静息电位）
- **ASER**: -42.6 → -46.7 mV（同上）
- **SMD swing**: 维持 ~70 mV（CCA-1/SLO-1 主导，EGL-36 辅助）
- **Speed**: 维持 0.3 mm/s（运动输出稳定）
- 整体：更符合生物学——感觉神经元静息更超极化（-60 mV 范围），信噪比更好

## 修改文件列表
| 文件 | 修改内容 |
|------|---------|
| `src/neuron/ion_channel.h` | 新增 6 个通道类: EGL36, IRK, TWK, SLO2, OSM9, EXP2 |
| `src/neuron/neuron_factory.cpp` | 按类型分配新通道 + AVL/DVB EXP-2 特化 |

## 验证结果
- 编译: 零错误
- Regtest: 17 pass, 0 FAIL
- 神经元动力学稳定，静息电位更符合生物学

## 参考文献
1. Johnstone DB et al. (1997) — EGL-36 Shaw K⁺ channel cloning
2. Elkes DA et al. (1997) — EGL-36 in C. elegans neurons
3. Santi CM et al. (2003) JBC — EGL-36 electrophysiology
4. Döring F et al. (2002) Mol Biol Cell — IRK-1/2/3 Kir channels
5. Salkoff L et al. (2001) — 49 TWK channels in C. elegans
6. Bhatt D (2014) — TWK-18 temperature sensitivity
7. Yuan A et al. (2000) Neuron — SLO-2 Na⁺-activated K⁺
8. Yuan A et al. (2003) JBC — SLO-2 activation mechanism
9. Colbert HA et al. (1997) J Neurosci — OSM-9 TRPV
10. Tobin DM et al. (2002) Neuron — OSM-9/OCR-2 polymodal nociception
11. Davis MW et al. (1999) Science — EXP-2 enteric K⁺ channel
12. Jiang J et al. (2022) Nat Commun — EXP-2 in AVL compound APs

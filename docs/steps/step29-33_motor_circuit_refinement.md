# Step 29-33: 运动回路精化

> 本文档为中文档，合并 Step 29-33 的完整一级内容。

---

## Step 29: 本体感觉波传播 (Proprioceptive Wave)

- **生物学**: Wen 2012 Neuron — B类MN自身转导本体感觉; Boyle 2012 — 双稳态+拉伸受体沿轴突整合
- **B类顺序感知**: DB01→seg2(SMD领地), DB02→seg7(DB01领地), DB03→seg15(DB02领地)
- **D/V交替接力**: DB01(+curv) → VB02(-curv) → DB03(+curv) = S波
- **A类保持同步**: seg 0/5/15 原始映射不变 (提供基础肌肉驱动力)
- **曲率扩散**: 0.5 体节间弹性耦合 (Boyle 2012)
- **曲率数值稳定性**: 半隐式Euler替代Forward Euler (stiffness×dt=5.25>>2, 无条件稳定)
- **关键教训**: A类映射不可改; Forward Euler对刚性ODE不稳定
- **结果**: regtest 17 pass; speed 0.3, heading 17.1; diag speed 0.076→0.192 (+153%)
- **REF**: Wen 2012 Neuron, Boyle 2012 Frontiers, Yeon 2018 PLOS Biology
- **文档**: [step29_proprioceptive_wave.md](step29_proprioceptive_wave.md)

---

## Step 30: Tyramine — RIM 逃逸反应协调

- **生物学**: RIM 是酪胺能神经元 (TDC-1+, TBH-1-), 通过 gap junction 与 AVA 共激活
- **TA 第4种调质**: 源 RIM L/R, τ_rise=500ms, τ_decay=2s (逃逸时间尺度)
- **LGC-55→SMD**: -25pA Cl⁻ 抑制 → 逆转期间头部摆动停止 (Pirri 2009)
- **LGC-55→AVB**: -10pA 抑制前进 → 促进长逆转 (Pirri 2009)
- **TYRA-3→ASH**: +5pA 增敏伤害感觉 → 碰壁增敏涌现 (Rex 2005)
- **TA→OA耦合**: RIC +2pA 模拟 TBH-1 底物供给 (Alkema 2005)
- **涌现**: 承诺式逆转(~200ms延迟) + 碰壁增敏(TA累积→ASH敏化)
- **结果**: regtest 17 pass; speed 0.3, heading 18.2; TA conc 0.27
- **REF**: Alkema 2005 Neuron, Pirri 2009 Neuron, Donnelly 2013 PLOS Biology
- **文档**: [step30_tyramine_rim.md](step30_tyramine_rim.md)

---

## Step 31: RIV-Driven Omega Turn — 硬编码→涌现

- **生物学**: RIV 是 GABA能运动神经元，控制腹侧头部弯曲，启动 omega turn (Gray 2005)
- **RIVL/RIVR 神经元**: GABA能, 84→88 neurons
- **突触**: AIB→RIV (1 section, L/R 梯度不对称) + RIV⊣RMD dorsal (1 section)
- **TA→RIV**: LGC-55 -20pA 抑制 (复用 Step 30 受体)
- **Post-reversal pulse**: 幅度=60×[TA], tau=400ms, L/R±30%梯度不对称
- **删除硬编码**: P(omega)公式 + 固定方向 + 固定持续时间 → 全部从 RIV burst 涌现
- **涌现特性**: 长逎转(高TA)→强脉冲→CCA-1 burst→omega; 短逎转(低TA)→弱脉冲→无omega
- **结果**: regtest 17 pass; omega=4, heading=11.0, speed=0.3
- **REF**: Gray 2005 PNAS, Donnelly 2013 PLOS Biology, Ouellette 2022 eLife, Neural Sequences 2024
- **文档**: [step31_riv_omega_turn.md](step31_riv_omega_turn.md)

---

## Step 32: AS Motor Neurons — 背侧偏置

- **生物学**: AS 是谷氨酸能运动神经元，专门投射背侧体壁肌肉，打破背腹对称
- **AS01-AS05**: 5个运动神经元, 88→93 neurons, 覆盖 dorsal seg 2-40
- **突触输入**: AVA→AS + AVB→AS (始终活跃) + DD⊣AS (交叉抑制) + DB↔AS (gap junction)
- **AS 背侧抵抗→omega 门控**: pre-reversal dorsal tone 快照 + RIV burst peak detection
  - 逆转开始时记录 dorsal tone (SMD 随机相位) → burst 峰值时评估
  - effective_riv = peak_release - pre_rev_tone × as_factor (1.0)
  - 高 dorsal tone → 阻断 omega; 低 dorsal tone → 允许 omega
- **运行时参数系统**: TuningParams CLI 覆盖, sweep_as_factor.ps1 参数扫描脚本
- **涌现**: omega/reversal 从 100% (Step 31) 降至 67% (Step 32); SMD 相位门控
- **结果**: regtest 17 pass; omega/reversal=0.67, speed=0.18, wave=GOOD
- **REF**: White 1986, Haspel 2010, Chen 2006
- **文档**: [step32_as_motor.md](step32_as_motor.md)

---

## Step 33: OLQ 鼻触 + RME 头部抑制

- **生物学**: RME 是 GABAergic 头部增益控制; OLQ 是鼻尖机械感觉 (唇部纤毛)
- **RMED/RMEV**: 对侧投射 (RMED⊣ventral, RMEV⊣dorsal) — 与 SMD 推拉配合
- **SMD⇌RME 突触外传递**: GAR-2 毒蕈碱 + GBB-1/2 GABA_B (sections=0.3, ~0.03nS)
- **OLQ (4个)**: 壁距<0.3mm 鼻触 → RMD 头缩回 + RIC 间接后退
- **修复不对称**: head curv D/V ratio 从 3.6× → 1.06× (RMEV 对抗 AS01 背侧偏置)
- **weathervane 40% SMD fraction**: 防止多通道对齐压死 SMD 振荡器
- **as_factor 1.0→3.5**: RMEV 降低 dorsal tone → 需更高 factor 维持 omega 门控
- **结果**: regtest 17 pass; D/V ratio=1.06, omega/reversal=0.58, wave=GOOD
- **REF**: White 1986, Huang 2016 eLife, Hart 1995, Kaplan & Horvitz 1993
- **文档**: [step33_olq_rme.md](step33_olq_rme.md)

---

## 本组总结

| 指标 | Step 28 (之前) | Step 33 (之后) |
|------|---------------|----------------|
| 神经元 | ~84 | **~105** (+RIV×2, AS×5, OLQ×4, RME×2 等) |
| 调质 | 3 (5-HT/DA/OA) | **4** (+TA) |
| 波传播 | 无 | **B类顺序本体感觉 + 体节扩散** |
| Omega turn | 硬编码概率 | **RIV burst 涌现** (TA门控) |
| D/V 对称 | 3.6× 偏 | **1.06×** (RME 矫正) |
| omega/reversal | 100% | **58%** (AS 背侧门控) |
| speed | 0.076 | **0.18** (+137%) |

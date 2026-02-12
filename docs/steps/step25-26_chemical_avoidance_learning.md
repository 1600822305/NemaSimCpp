# Step 25-26: 化学回避与病原体学习

> 本文档为中文档，合并 Step 25-26 的完整一级内容。

---

## Step 25: 化学回避 + ASH 伤害感觉 + 排斥 Weathervane

- **排斥化学场**: Environment 添加独立 repellent_field_ (σ²=25mm² 局部化毒物)
- **ASH TONIC型**: gain=80, baseline=3pA, half_max=0.5, clamp=80pA (排斥物中心52pA)
- **新增突触**: ASH→AIB(3, GLR-1), ASH→RIM(1, 促omega), ASH→AVA(3, 从2恢复)
- **5-HT→AIB**: MOD-1 Cl⁻通道 -6pA 抑制 (在食物上时压制回避=冒险觅食)
- **排斥 Weathervane**: ∇C_repellent⊥ → 反向曲率偏置, 持续偏转绕路 (不受satiety调制)
- **涌现绕路**: 排斥物挡路→不穿过(r_dist≥1.4mm)→向北偏转(y:25→32)→从侧面到达食物
- **结果**: regtest 12 pass; CI=0.655, time_near_food=18.5%
- **REF**: Summers 2015, Cook 2019, Bargmann & Kaplan 1998, Iino & Yoshida 2009
- **文档**: [step25_chemical_avoidance.md](step25_chemical_avoidance.md)

---

## Step 26: 条件性病原体回避学习

- **生物学**: Zhang 2005 Nature — 吃致病菌→生病→学会回避同种气味 (条件性味觉厌恶)
- **新增神经元**: ADF L/R (5-HT源) — 85总神经元
- **新增突触**: ADF→AIY(2, MOD-1抑制), ADF→AIZ(1) — ~120总突触
- **Sickness状态**: 在有毒食物区进食时累积 (τ_rise=30s, τ_decay=600s 持久记忆)
- **ADF驱动**: I_ext = 2 + 30×sickness_ (最高32pA → 5-HT释放 → MOD-1抑制AIY)
- **AWC突触翻转**: lr=0.003(15x), AWC→AIY w_mod↓0.1(底限), AWC→AIB w_mod↑2.3(+130%)
- **Weathervane AWC偏好翻转**: awc_pref=(w_mod-0.55)×3.0, 学后=-1.35(排斥力>引诱力)
- **疾病性厌食**: sick_suppression=1-0.85×sickness(化学感觉降到15%)
- **多化学物种**: soluble_field_基础设施(ASE独立通道就绪)
- **fmem双重保护**: 快速衰减(tau 90s→5s) + 充值门控(生病时不记好食物)
- **三层化学回避**: 先天ASH(即时) + 学习AWC翻转(~60s) + 5-HT调制(秒级)
- **结果**: regtest 12 pass; **CI=-0.18(反向!)**, fmem=0.000, near_food=29%
- **REF**: Zhang 2005 Nature, Ha 2010 Neuron, Bargmann 2006
- **文档**: [step26_pathogen_learning.md](step26_pathogen_learning.md)

---

## 本组总结

| 指标 | 之前 | 之后 |
|------|------|------|
| 化学场 | 1 (food_odor) | **3** (+repellent +soluble) |
| 回避机制 | 无 | **三层**: 先天ASH + 学习AWC翻转 + 5-HT调制 |
| 毒素场景 CI | N/A | **-0.18** (学会回避) |
| 内部状态 | satiety, food_memory | +**sickness** |
